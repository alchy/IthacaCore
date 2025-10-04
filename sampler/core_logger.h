/**
 * @file core_logger.h
 * @brief Unified thread-safe logger with RT-safe support
 *
 * Provides centralized logging for entire IthacaCore and Ithaca plugin:
 * - Thread-safe file logging (mutex-protected)
 * - RT-safe logging (lock-free ring buffer)
 * - Severity-based filtering
 * - Console + File output modes
 * - Automatic timestamp formatting
 *
 * IMPORTANT: All logging (including initialization messages) goes through
 * this unified logger. No direct printf/cout/cerr usage anywhere in project.
 */

#ifndef CORE_LOGGER_H
#define CORE_LOGGER_H

#include <string>
#include <mutex>
#include <fstream>
#include <atomic>
#include <array>
#include <cstdint>

/**
 * @enum LogSeverity
 * @brief Log message severity levels (for filtering)
 */
enum class LogSeverity : uint8_t {
    Debug = 0,      ///< Detailed debug information
    Info = 1,       ///< General informational messages
    Warning = 2,    ///< Warning messages (non-critical issues)
    Error = 3,      ///< Error messages (critical issues)
    Critical = 4    ///< Critical errors (may lead to program termination)
};

/**
 * @class Logger
 * @brief Unified thread-safe logger with RT-safe capabilities
 *
 * Design:
 * - Non-RT context: Uses mutex-protected file/console writes
 * - RT context: Uses lock-free ring buffer (flushed periodically)
 * - All output centralized (no printf/cout/cerr elsewhere)
 * - Severity filtering to reduce log volume
 *
 * Thread Safety:
 * - log() is thread-safe but NOT RT-safe (uses mutex)
 * - logRT() is RT-safe (lock-free, bounded execution time)
 * - flushRTBuffer() must be called from non-RT thread
 *
 * Usage Examples:
 *
 * Non-RT context (initialization, setup):
 * @code
 * logger.log("VoiceManager/initialize", LogSeverity::Info, "System ready");
 * logger.log("SamplerIO/loadSample", LogSeverity::Error, "File not found");
 * @endcode
 *
 * RT context (processBlock, voice processing):
 * @code
 * logger.logRT("Voice/processBlock", LogSeverity::Warning, "Buffer overflow");
 * @endcode
 *
 * Format Convention:
 * - component: "ClassName/methodName" (e.g., "VoiceManager/setNoteState")
 * - severity: LogSeverity enum (Debug/Info/Warning/Error/Critical)
 * - message: Descriptive text (no component/severity prefix needed)
 */
class Logger {
public:
    /**
     * @brief Constructor - initializes logger subsystem
     * @param path Directory path for log files (creates core_logger/ subdirectory)
     * @param minSeverity Minimum severity level to log (default: Info)
     * @param useConsole Enable console output (default: false in production)
     * @param useFile Enable file output (default: true)
     *
     * @note Constructor logs initialization messages using the logger itself
     * @note If initialization fails, logs error and calls std::exit(1)
     */
    explicit Logger(const std::string& path,
                   LogSeverity minSeverity = LogSeverity::Info,
                   bool useConsole = false,
                   bool useFile = true);

    /**
     * @brief Non-RT safe logging (thread-safe, uses mutex)
     * @param component Component identifier (format: "ClassName/methodName")
     * @param severity Message severity level
     * @param message Log message content
     *
     * @note Blocks until write completes - NOT suitable for RT context
     * @note Respects minSeverity filter and output mode settings
     */
    void log(const std::string& component,
            LogSeverity severity,
            const std::string& message);

    /**
     * @brief RT-safe logging (lock-free ring buffer)
     * @param component Component identifier (max 63 chars, will be truncated)
     * @param severity Message severity level
     * @param message Log message (max 255 chars, will be truncated)
     *
     * @note Lock-free, bounded execution time - safe for RT context
     * @note Messages buffered in ring buffer, flushed by flushRTBuffer()
     * @note If buffer full, oldest messages are overwritten (no blocking)
     *
     * @warning Component and message strings MUST be null-terminated
     * @warning Caller responsible for lifetime of string literals
     */
    void logRT(const char* component,
              LogSeverity severity,
              const char* message);

    /**
     * @brief Flush RT buffer to file/console (call from non-RT thread)
     * @return Number of messages flushed
     *
     * @note Should be called periodically (e.g., every 100ms from timer)
     * @note Safe to call even if buffer is empty
     */
    int flushRTBuffer();

    /**
     * @brief Set minimum severity level for logging
     * @param level New minimum severity (messages below this are ignored)
     *
     * @note Thread-safe, atomic operation
     * @note Affects both log() and logRT()
     */
    void setMinSeverity(LogSeverity level);

    /**
     * @brief Get current minimum severity level
     * @return Current minimum severity
     */
    LogSeverity getMinSeverity() const;

    /**
     * @brief Configure output destinations
     * @param useConsole Enable/disable console output
     * @param useFile Enable/disable file output
     *
     * @note Thread-safe, atomic operations
     */
    void setOutputMode(bool useConsole, bool useFile);

    /**
     * @brief Convert severity enum to string representation
     * @param severity Severity level
     * @return String name (e.g., "INFO", "ERROR")
     */
    static const char* severityToString(LogSeverity severity);

    /**
     * @brief Destructor - flushes pending messages and closes log file
     */
    ~Logger();

private:
    // ========================================================================
    // RT-Safe Ring Buffer
    // ========================================================================

    /**
     * @brief Lock-free ring buffer entry for RT logging
     */
    struct LogEntry {
        char component[64];         ///< Component identifier
        LogSeverity severity;       ///< Message severity
        char message[256];          ///< Log message
        uint64_t timestamp;         ///< Microsecond timestamp
        std::atomic<bool> ready;    ///< Entry ready flag (for ABA prevention)

        LogEntry() : severity(LogSeverity::Info), timestamp(0), ready(false) {
            component[0] = '\0';
            message[0] = '\0';
        }
    };

    static constexpr size_t RT_BUFFER_SIZE = 1024;  ///< Ring buffer capacity
    std::array<LogEntry, RT_BUFFER_SIZE> rtBuffer_; ///< Lock-free ring buffer
    std::atomic<size_t> rtWriteIndex_;              ///< Write position (RT thread)
    std::atomic<size_t> rtReadIndex_;               ///< Read position (flush thread)

    // ========================================================================
    // File + Console Output
    // ========================================================================

    std::string logFilePath_;       ///< Full path to log file
    std::ofstream logFile_;         ///< Log file stream
    mutable std::mutex logMutex_;   ///< Mutex for non-RT logging

    // ========================================================================
    // Configuration
    // ========================================================================

    std::atomic<LogSeverity> minSeverity_;  ///< Minimum severity to log
    std::atomic<bool> useConsole_;          ///< Console output enabled
    std::atomic<bool> useFile_;             ///< File output enabled

    // ========================================================================
    // Helper Methods
    // ========================================================================

    /**
     * @brief Get formatted timestamp string
     * @return Timestamp in format "[YYYY-MM-DD HH:MM:SS.mmm]"
     */
    std::string getTimestamp() const;

    /**
     * @brief Get high-resolution timestamp (microseconds since epoch)
     * @return Timestamp in microseconds
     */
    uint64_t getTimestampMicros() const;

    /**
     * @brief Format timestamp from microseconds
     * @param micros Timestamp in microseconds
     * @return Formatted timestamp string
     */
    std::string formatTimestamp(uint64_t micros) const;

    /**
     * @brief Write formatted log entry to file
     * @param component Component identifier
     * @param severity Message severity
     * @param message Log message
     * @param timestamp Optional timestamp (if 0, uses current time)
     */
    void writeToFile(const std::string& component,
                    LogSeverity severity,
                    const std::string& message,
                    uint64_t timestamp = 0);

    /**
     * @brief Write formatted log entry to console
     * @param component Component identifier
     * @param severity Message severity
     * @param message Log message
     * @param timestamp Optional timestamp (if 0, uses current time)
     */
    void writeToConsole(const std::string& component,
                       LogSeverity severity,
                       const std::string& message,
                       uint64_t timestamp = 0);

    /**
     * @brief Check if message should be logged based on severity
     * @param severity Message severity
     * @return true if message passes severity filter
     */
    bool shouldLog(LogSeverity severity) const;

    /**
     * @brief Initialize logger subsystem (called from constructor)
     * @param path Directory path
     * @return true on success, false on failure
     */
    bool initialize(const std::string& path);
};

// ============================================================================
// Legacy Compatibility Macros (DEPRECATED - use LogSeverity enum directly)
// ============================================================================

#define LOG_DEBUG LogSeverity::Debug
#define LOG_INFO LogSeverity::Info
#define LOG_WARNING LogSeverity::Warning
#define LOG_ERROR LogSeverity::Error
#define LOG_CRITICAL LogSeverity::Critical

#endif  // CORE_LOGGER_H
