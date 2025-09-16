/*
THIS FILE IS LOCKED, IT IS FUNCTIONAL AND WILL NOT BE CHANGED
*/

#include "core_logger.h"
#include <fstream>      // Pro std::ofstream a zápis do souboru
#include <filesystem>   // Pro kontrolu adresáře, vytvoření složky a smazání souboru (C++17)
#include <chrono>       // Pro získání aktuálního času
#include <iomanip>      // Pro formátování času
#include <sstream>      // Pro sestavení stringu
#include <iostream>     // Pro std::cerr při chybě inicializace
#include <cstdlib>      // Pro std::exit
#include <string>       // Pro std::string
#include <cstdio>       // Pro printf

/**
 * @brief Konstruktor loggeru.
 * Kontroluje existenci a přístup k adresáři, vytváří složku core_logger,
 * smaže existující log soubor a otevře nový v append módu.
 * VÝSTUP: Vypisuje inicializační zprávy na konzoli pomocí printf.
 * @param path Cesta k adresáři.
 * Při selhání: Tiskne chybu do std::cerr a volá std::exit(1).
 */
Logger::Logger(const std::string& path) {
    printf("[Logger] Initializing logger subsystem...\n");
    printf("[Logger] Target directory: %s\n", path.c_str());
    
    std::filesystem::path dirPath(path);
    if (!std::filesystem::exists(dirPath)) {
        printf("[Logger] ERROR: Directory '%s' does not exist.\n", path.c_str());
        std::cerr << "Logger error: Directory '" << path << "' does not exist." << std::endl;
        std::exit(1);
    }
    if (!std::filesystem::is_directory(dirPath)) {
        printf("[Logger] ERROR: '%s' is not a valid directory.\n", path.c_str());
        std::cerr << "Logger error: '" << path << "' is not a valid directory." << std::endl;
        std::exit(1);
    }

    printf("[Logger] Directory validation: OK\n");

    // Kontrola přístupu k zápisu (pokus o vytvoření test souboru)
    printf("[Logger] Testing write access...\n");
    std::filesystem::path testFile = dirPath / "test_write.tmp";
    std::ofstream testStream(testFile);
    if (!testStream.is_open()) {
        printf("[Logger] ERROR: No write access to directory '%s'.\n", path.c_str());
        std::cerr << "Logger error: No write access to directory '" << path << "'." << std::endl;
        std::exit(1);
    }
    testStream.close();
    std::filesystem::remove(testFile);  // Vyčištění test souboru
    printf("[Logger] Write access: OK\n");

    // Vytvoření složky core_logger
    std::filesystem::path loggerDir = dirPath / "core_logger";
    printf("[Logger] Creating/verifying core_logger directory...\n");
    try {
        bool created = std::filesystem::create_directory(loggerDir);
        if (created) {
            printf("[Logger] Created new directory: %s\n", loggerDir.string().c_str());
        } else {
            printf("[Logger] Using existing directory: %s\n", loggerDir.string().c_str());
        }
    } catch (const std::filesystem::filesystem_error& e) {
        printf("[Logger] ERROR: Failed to create core_logger directory.\n");
        std::cerr << "Logger error creating core_logger directory: " << e.what() << std::endl;
        std::exit(1);
    }

    // Cesta k log souboru
    logFilePath_ = (loggerDir / "core_logger.log").string();
    printf("[Logger] Log file path: %s\n", logFilePath_.c_str());

    // Smazání existujícího log souboru, pokud existuje
    if (std::filesystem::exists(logFilePath_)) {
        std::filesystem::remove(logFilePath_);
        printf("[Logger] Removed existing log file for clean start\n");
    }

    // Otevření nového souboru v append módu
    printf("[Logger] Opening log file for writing...\n");
    logFile_.open(logFilePath_, std::ios::app);
    if (!logFile_.is_open()) {
        printf("[Logger] ERROR: Cannot open log file '%s'.\n", logFilePath_.c_str());
        std::cerr << "Logger error: Cannot open log file '" << logFilePath_ << "'." << std::endl;
        std::exit(1);
    }

    // Počáteční záznam v angličtině
    std::string initialLog = getTimestamp() + " [Logger/constructor] [info]: Logger initialized successfully at " + logFilePath_;
    logFile_ << initialLog << std::endl;
    logFile_.flush();  // Okamžitý zápis

    printf("[Logger] SUCCESS: Logger fully operational\n");
    std::string absolutePath = std::filesystem::absolute(logFilePath_).string();
    printf("[Logger] Log file can be found at: %s\n", absolutePath.c_str());
    printf("[Logger] To monitor logs in real-time, use: Get-Content -Path \"%s\" -Tail 10 -Wait\n", absolutePath.c_str());
}

/**
 * @brief Loguje zprávu do souboru s timestampem.
 * Thread-safe: Používá mutex pro zápis.
 * @param component Název komponenty.
 * @param severity Závažnost.
 * @param message Zpráva.
 */
void Logger::log(const std::string& component, const std::string& severity, const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex_);  // Thread-safety

    if (!logFile_.is_open()) {
        return;  // Ignorovat, pokud soubor není otevřený
    }

    std::string logEntry = "[" + getTimestamp() + "] [" + component + "] [" + severity + "]: " + message + "\n";
    logFile_ << logEntry;
    logFile_.flush();  // Okamžitý zápis pro debugging
}

/**
 * @brief Získává aktuální timestamp ve formátu YYYY-MM-DD HH:MM:SS.
 * @return Formátovaný timestamp jako std::string.
 */
std::string Logger::getTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm timeStruct;
    localtime_s(&timeStruct, &time_t);  // Bezpečné pro MSVC
    std::stringstream ss;
    ss << std::put_time(&timeStruct, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

/**
 * @brief Destruktor: Zaloguje ukončení a uzavře souborový stream.
 */
Logger::~Logger() {
    if (logFile_.is_open()) {
        // Finální záznam před uzavřením
        std::string finalLog = getTimestamp() + " [Logger/destructor] [info]: Logger shutting down, closing log file";
        logFile_ << finalLog << std::endl;
        logFile_.flush();
        
        logFile_.close();
        
        // Konzolová zpráva o ukončení
        printf("[Logger] Logger destructor called - log file closed\n");
    }
}