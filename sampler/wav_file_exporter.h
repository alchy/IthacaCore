/*
THIS FILE IS LOCKED, IT IS FUNCTIONAL AND WILL NOT BE CHANGED
*/

#ifndef WAV_FILE_EXPORTER_H
#define WAV_FILE_EXPORTER_H

#include <string>       // Pro std::string
#include <chrono>       // Pro měření času (std::chrono)
#include <sndfile.h>    // Pro SF_INFO, SNDFILE* (zápis WAV)
#include <cstdint>      // Pro int16_t
#include "core_logger.h" // Pro Logger

/**
 * @enum ExportFormat
 * @brief Formát exportu WAV souboru.
 * Pcm16: 16-bit PCM (default – konverze float → int16 s clippingem, široká kompatibilita).
 * Float: 32-bit float (přímý zápis, zachovává přesnost pro JUCE).
 */
enum class ExportFormat {
    Pcm16,  // Default
    Float
};

/**
 * @class WavExporter
 * @brief Třída pro export WAV souboru po blocích (JUCE-like audio block processing).
 * 
 * Používá se pro testování načtených samples: Naplníte buffer float daty ze sample (např. z InstrumentLoader),
 * pak zapisujete po blocích. Podporuje měření času kopírování do bufferu i zápisu do souboru.
 * 
 * Klíčové vlastnosti:
 * - Inicializuje WAV soubor (libsndfile) s danou frekvencí, stereo/mono a formátem (default Pcm16).
 * - Vstupní buffer vždy float (interleaved pro stereo: [L,R,L,R...]).
 * - dummy_write: Pokud false, měří čas, ale nic nezapisuje (pro profilování rychlosti kopírování).
 * - Zápis po blocích (buffer_size: 32–1024, jako v JUCE processBlock).
 * - Pro Pcm16: Automatická konverze float → int16 (škálování -1.0..1.0 na -32768..32767 s clippingem).
 * - Logování: Default zapnuté (přes Logger). Pro vypnutí definuj PERFORMANCE_TEST (makro v CMake) – pak jen měření času bez logů.
 * 
 * Příklad použití:
 * WavExporter exporter("./exports", logger);  // Default Pcm16
 * float* buffer = exporter.wavFileCreate("test_export.wav", 44100, 512, true, true); // stereo, reálný zápis
 * // Naplňte buffer daty ze sample (např. memcpy z Instrument bufferu)
 * exporter.wavFileWriteBuffer(buffer, 512);
 * // Opakujte pro celý sample... Destruktor uzavře soubor.
 */
class WavExporter {
public:
    /**
     * @brief Konstruktor: Inicializuje logger, výstupní adresář a formát exportu (default Pcm16).
     * Vytvoří složku, pokud neexistuje. Loguje inicializaci (pokud ne PERFORMANCE_TEST).
     * @param outputDir Cesta k adresáři pro export (např. "./exports").
     * @param logger Reference na Logger pro logování (ignorován při PERFORMANCE_TEST).
     * @param exportFormat Formát exportu (default Pcm16).
     */
    explicit WavExporter(const std::string& outputDir, Logger& logger, ExportFormat exportFormat = ExportFormat::Pcm16);

    /**
     * @brief Vytvoří WAV soubor a alokuje float buffer pro zápis.
     * Nastaví SF_INFO podle formátu (Pcm16/float). Alokuje temp int16 pro Pcm16.
     * Loguje (pokud ne PERFORMANCE_TEST). Při chybě: exit(1).
     * @param filename Název souboru (např. "export.wav") – plná cesta: outputDir/filename.
     * @param frequency Frekvence vzorkování (např. 44100 Hz).
     * @param bufferSize Velikost bufferu v samples (např. 512 – JUCE-like).
     * @param stereo True pro stereo (interleaved [L,R,L,R...]), false pro mono.
     * @param dummy_write True: Reálný zápis; False: Jen měření (nic se nezapisuje).
     * @return Pointer na alokovaný float buffer (velikost: bufferSize * channels).
     */
    float* wavFileCreate(const std::string& filename, int frequency, int bufferSize, bool stereo, bool dummy_write);

    /**
     * @brief Zapisuje naplněný float buffer do souboru po blocích.
     * Pro Pcm16: Konvertuje na int16 (s clippingem), zapisuje. Pro Float: Přímý zápis.
     * Měří čas (vždy). Loguje (pokud ne PERFORMANCE_TEST). Při chybě: return false (log + exit(1)).
     * @param buffer_ptr Pointer na naplněný float buffer (z wavFileCreate).
     * @param buffer_size Počet samples (může být menší pro poslední blok).
     * @return True při úspěchu.
     */
    bool wavFileWriteBuffer(float* buffer_ptr, int buffer_size);

    /**
     * @brief Destruktor: Uzavře soubor (sf_close), uvolní buffery (free).
     * Zaloguje celkový čas a formát (pokud ne PERFORMANCE_TEST).
     */
    ~WavExporter();

private:
    Logger& logger_;                    // Reference na logger (použit při !PERFORMANCE_TEST)
    std::string outputDir_;             // Výstupní adresář
    ExportFormat exportFormat_;         // Formát exportu (default Pcm16)
    SNDFILE* sndfile_ = nullptr;        // Handle pro libsndfile zápis
    SF_INFO sfinfo_;                    // Metadata WAV (frekvence, kanály)
    float* buffer_ = nullptr;           // Alokovaný float buffer (pro uživatele)
    int16_t* tempPcmBuffer_ = nullptr;  // Temp buffer pro Pcm16 konverzi (interní)
    int bufferSize_;                    // Velikost bufferu
    int channels_;                      // Počet kanálů (1=mono, 2=stereo)
    bool dummy_write_;                  // Režim měření bez zápisu
    std::chrono::steady_clock::time_point startTime_;  // Pro měření celkového času

    // Pomocná: Měří čas a loguje (jen pokud !PERFORMANCE_TEST)
    void logTime(const std::string& operation, std::chrono::steady_clock::time_point start);

    // Pomocná: Konverze float → int16 s clippingem (pro Pcm16)
    void convertFloatToInt16(float* src, int16_t* dst, int numSamples);
};

// Makro pro vypnutí logování v testech rychlosti (definuj v CMake: add_definitions(-DPERFORMANCE_TEST))
#ifndef PERFORMANCE_TEST
#define LOG_ENABLED 1
#else
#define LOG_ENABLED 0
#endif

#endif // WAV_FILE_EXPORTER_H