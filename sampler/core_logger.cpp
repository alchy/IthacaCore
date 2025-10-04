/**
 * @file core_logger.cpp
 * @brief Implementation of unified thread-safe logger with RT-safe support
 */

#include "core_logger.h"
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace fs = std::filesystem;

// ============================================================================
// Constructor / Destructor
// ============================================================================

Logger::Logger(const std::string& path,
               LogSeverity minSeverity,
               bool useConsole,
               bool useFile)
    : rtWriteIndex_(0)
    , rtReadIndex_(0)
    , minSeverity_(minSeverity)
    , useConsole_(useConsole)
    , useFile_(useFile)
{
    if (!initialize(path)) {
        // Critical failure - log error and exit
        std::cerr << "[Logger] CRITICAL: Failed to initialize logger!" << std::endl;
        std::exit(1);
    }
}

Logger::~Logger() {
    // Flush any pending RT messages
    flushRTBuffer();

    // Log shutdown message
    log("Logger/destructor", LogSeverity::Info, "Logger shutting down");

    // Close file
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

// ============================================================================
// Public API - Non-RT Logging
// ============================================================================

void Logger::log(const std::string& component,
                LogSeverity severity,
                const std::string& message) {
    // Check severity filter
    if (!shouldLog(severity)) {
        return;
    }

    // Acquire mutex for thread safety
    std::lock_guard<std::mutex> lock(logMutex_);

    // Write to configured outputs
    if (useFile_.load(std::memory_order_relaxed)) {
        writeToFile(component, severity, message, 0);
    }

    if (useConsole_.load(std::memory_order_relaxed)) {
        writeToConsole(component, severity, message, 0);
    }
}

// ============================================================================
// Public API - RT-Safe Logging
// ============================================================================

void Logger::logRT(const char* component,
                  LogSeverity severity,
                  const char* message) {
    // Check severity filter (atomic, RT-safe)
    if (!shouldLog(severity)) {
        return;
    }

    // Get write position (lock-free)
    size_t writeIdx = rtWriteIndex_.load(std::memory_order_relaxed);
    size_t nextIdx = (writeIdx + 1) % RT_BUFFER_SIZE;

    // Get timestamp immediately (RT-safe)
    uint64_t timestamp = getTimestampMicros();

    // Write to ring buffer entry
    LogEntry& entry = rtBuffer_[writeIdx];

    // Copy component (with truncation)
    std::strncpy(entry.component, component, 63);
    entry.component[63] = '\0';

    // Copy message (with truncation)
    std::strncpy(entry.message, message, 255);
    entry.message[255] = '\0';

    // Set metadata
    entry.severity = severity;
    entry.timestamp = timestamp;

    // Mark as ready (release semantics for visibility)
    entry.ready.store(true, std::memory_order_release);

    // Advance write index (atomic)
    rtWriteIndex_.store(nextIdx, std::memory_order_release);
}

// ============================================================================
// Public API - RT Buffer Flush
// ============================================================================

int Logger::flushRTBuffer() {
    int flushedCount = 0;

    // Process all ready entries
    size_t readIdx = rtReadIndex_.load(std::memory_order_acquire);
    size_t writeIdx = rtWriteIndex_.load(std::memory_order_acquire);

    while (readIdx != writeIdx) {
        LogEntry& entry = rtBuffer_[readIdx];

        // Check if entry is ready (acquire semantics)
        if (entry.ready.load(std::memory_order_acquire)) {
            // Acquire mutex for file/console write
            std::lock_guard<std::mutex> lock(logMutex_);

            // Write to configured outputs
            if (useFile_.load(std::memory_order_relaxed)) {
                writeToFile(entry.component, entry.severity,
                           entry.message, entry.timestamp);
            }

            if (useConsole_.load(std::memory_order_relaxed)) {
                writeToConsole(entry.component, entry.severity,
                              entry.message, entry.timestamp);
            }

            // Mark as processed
            entry.ready.store(false, std::memory_order_release);
            flushedCount++;
        }

        // Advance read index
        readIdx = (readIdx + 1) % RT_BUFFER_SIZE;
        rtReadIndex_.store(readIdx, std::memory_order_release);
    }

    return flushedCount;
}

// ============================================================================
// Public API - Configuration
// ============================================================================

void Logger::setMinSeverity(LogSeverity level) {
    minSeverity_.store(level, std::memory_order_relaxed);
}

LogSeverity Logger::getMinSeverity() const {
    return minSeverity_.load(std::memory_order_relaxed);
}

void Logger::setOutputMode(bool useConsole, bool useFile) {
    useConsole_.store(useConsole, std::memory_order_relaxed);
    useFile_.store(useFile, std::memory_order_relaxed);
}

const char* Logger::severityToString(LogSeverity severity) {
    switch (severity) {
        case LogSeverity::Debug:    return "DEBUG";
        case LogSeverity::Info:     return "INFO";
        case LogSeverity::Warning:  return "WARNING";
        case LogSeverity::Error:    return "ERROR";
        case LogSeverity::Critical: return "CRITICAL";
        default:                    return "UNKNOWN";
    }
}

// ============================================================================
// Private Methods - Initialization
// ============================================================================

bool Logger::initialize(const std::string& path) {
    // This method uses direct console output ONLY during initialization
    // After initialization completes, all logging goes through log()

    std::cout << "[Logger] Initializing logger subsystem..." << std::endl;
    std::cout << "[Logger] Target directory: " << path << std::endl;

    // Check directory existence
    fs::path dirPath(path);
    if (!fs::exists(dirPath)) {
        std::cerr << "[Logger] ERROR: Directory '" << path << "' does not exist." << std::endl;
        return false;
    }

    if (!fs::is_directory(dirPath)) {
        std::cerr << "[Logger] ERROR: '" << path << "' is not a valid directory." << std::endl;
        return false;
    }

    std::cout << "[Logger] Directory validation: OK" << std::endl;

    // Test write access
    fs::path testFile = dirPath / "test_write.tmp";
    std::ofstream testStream(testFile);
    if (!testStream.is_open()) {
        std::cerr << "[Logger] ERROR: No write access to directory '" << path << "'." << std::endl;
        return false;
    }
    testStream.close();
    fs::remove(testFile);

    std::cout << "[Logger] Write access: OK" << std::endl;

    // Create core_logger subdirectory
    fs::path loggerDir = dirPath / "core_logger";
    try {
        if (!fs::exists(loggerDir)) {
            fs::create_directory(loggerDir);
            std::cout << "[Logger] Created directory: " << loggerDir.string() << std::endl;
        } else {
            std::cout << "[Logger] Using existing directory: " << loggerDir.string() << std::endl;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[Logger] ERROR: Failed to create core_logger directory: "
                  << e.what() << std::endl;
        return false;
    }

    // Set log file path
    logFilePath_ = (loggerDir / "core_logger.log").string();
    std::cout << "[Logger] Log file path: " << logFilePath_ << std::endl;

    // Remove old log file for clean start
    if (fs::exists(logFilePath_)) {
        fs::remove(logFilePath_);
        std::cout << "[Logger] Removed existing log file for clean start" << std::endl;
    }

    // Open log file
    logFile_.open(logFilePath_, std::ios::out | std::ios::app);
    if (!logFile_.is_open()) {
        std::cerr << "[Logger] ERROR: Cannot open log file '" << logFilePath_ << "'." << std::endl;
        return false;
    }

    // Write initial log entry
    std::string initMsg = "=== Logger initialized - severity filter: " +
                         std::string(severityToString(minSeverity_.load())) + " ===";
    log("Logger/initialize", LogSeverity::Info, initMsg);

    // Print success message to console
    fs::path absolutePath = fs::absolute(logFilePath_);
    std::cout << "[Logger] SUCCESS: Logger fully operational" << std::endl;
    std::cout << "[Logger] Log file: " << absolutePath.string() << std::endl;

    return true;
}

// ============================================================================
// Private Methods - Output Writers
// ============================================================================

void Logger::writeToFile(const std::string& component,
                        LogSeverity severity,
                        const std::string& message,
                        uint64_t timestamp) {
    if (!logFile_.is_open()) {
        return;
    }

    // Format: [timestamp] [component] [SEVERITY]: message
    std::string timestampStr = (timestamp > 0)
        ? formatTimestamp(timestamp)
        : getTimestamp();

    logFile_ << timestampStr
             << " [" << component << "]"
             << " [" << severityToString(severity) << "]: "
             << message << std::endl;

    // Flush immediately for critical messages
    if (severity >= LogSeverity::Error) {
        logFile_.flush();
    }
}

void Logger::writeToConsole(const std::string& component,
                           LogSeverity severity,
                           const std::string& message,
                           uint64_t timestamp) {
    // Format: [timestamp] [component] [SEVERITY]: message
    std::string timestampStr = (timestamp > 0)
        ? formatTimestamp(timestamp)
        : getTimestamp();

    // Use stderr for warnings and errors, stdout for info/debug
    std::ostream& out = (severity >= LogSeverity::Warning) ? std::cerr : std::cout;

    out << timestampStr
        << " [" << component << "]"
        << " [" << severityToString(severity) << "]: "
        << message << std::endl;
}

// ============================================================================
// Private Methods - Helpers
// ============================================================================

bool Logger::shouldLog(LogSeverity severity) const {
    return severity >= minSeverity_.load(std::memory_order_relaxed);
}

std::string Logger::getTimestamp() const {
    return formatTimestamp(getTimestampMicros());
}

uint64_t Logger::getTimestampMicros() const {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

std::string Logger::formatTimestamp(uint64_t micros) const {
    // Convert microseconds to time_point
    auto duration = std::chrono::microseconds(micros);
    auto timepoint = std::chrono::system_clock::time_point(duration);

    // Get time_t for formatting
    auto time_t_now = std::chrono::system_clock::to_time_t(timepoint);

    // Get milliseconds component
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count() % 1000;

    // Format: [YYYY-MM-DD HH:MM:SS.mmm]
    std::stringstream ss;
    ss << "[";

#ifdef _WIN32
    // Windows: Use localtime_s
    struct tm timeinfo;
    localtime_s(&timeinfo, &time_t_now);
    ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
#else
    // Unix: Use localtime_r
    struct tm timeinfo;
    localtime_r(&time_t_now, &timeinfo);
    ss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
#endif

    ss << "." << std::setfill('0') << std::setw(3) << ms << "]";

    return ss.str();
}
