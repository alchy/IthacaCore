/*
THIS FILE IS LOCKED, IT IS FUNCTIONAL AND WILL NOT BE CHANGED
*/

#ifndef CORE_LOGGER_H  // Include guard pro prevenci duplikátního includu
#define CORE_LOGGER_H

#include <string>       // Pro std::string
#include <mutex>        // Pro thread-safety při zápisu
#include <fstream>      // Pro std::ofstream (definice logFile_)
#include <stdexcept>    // Pro výjimky při chybách (pouze interně)

/**
 * @class Logger
 * @brief Jednoduchý souborový logger bez JUCE závislostí.
 * 
 * Loguje zprávy s timestampem, komponentou a závažností do souboru core_logger.log.
 * Konstruktor bere cestu k adresáři, kde vytvoří složku core_logger a log soubor.
 * Při startu smaže existující core_logger.log pro čistý restart.
 * Thread-safe díky mutexu. Selhání inicializace vede k tisku do std::cerr a ukončení programu.
 * 
 * DŮLEŽITÉ: Logger vypisuje inicializační zprávy na konzoli (printf).
 * Pokud inicializace selže, program se ukončí s exit(1).
 */
class Logger {
public:
    /**
     * @brief Konstruktor loggeru.
     * @param path Cesta k adresáří (absolutní nebo relativní, např. "./").
     * Kontroluje existenci adresáře a přístup; vytváří složku core_logger a log soubor.
     * Smaže existující core_logger.log, pokud existuje.
     * VÝSTUP: Vypisuje inicializační zprávy na konzoli pomocí printf.
     * Při selhání: Tiskne chybu do std::cerr a volá std::exit(1).
     */
    explicit Logger(const std::string& path);

    /**
     * @brief Loguje zprávu do souboru.
     * Formát: [YYYY-MM-DD HH:MM:SS] [component] [severity]: message
     * @param component Název třídy/metody (např. "SamplerIO/loadSamples").
     * @param severity Závažnost (např. "info", "error", "warn").
     * @param message Zpráva k logování.
     */
    void log(const std::string& component, const std::string& severity, const std::string& message);

    /**
     * @brief Destruktor: Uzavře souborový stream.
     */
    ~Logger();

private:
    std::string logFilePath_;  // Cesta k log souboru
    std::ofstream logFile_;    // Souborový stream pro zápis
    mutable std::mutex logMutex_;  // Mutex pro thread-safety

    // Pomocná metoda pro získání timestampu
    std::string getTimestamp() const;
};

#endif  // CORE_LOGGER_H    