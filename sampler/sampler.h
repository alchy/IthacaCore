#ifndef SAMPLER_H       // Include guard pro prevenci duplikátního includu
#define SAMPLER_H

#include <cstdint>      // Pro uint8_t a size_t
#include <cstddef>      // Pro size_t (pokud není v cstdint)
#include <string>       // Pro std::string
#include <vector>       // Pro std::vector
#include <memory>       // Pro std::unique_ptr

#include "core_logger.h"  // Pro Logger (pro předání reference)
#include "sampler_io.h"   // Pro SamplerIO třídu a SampleInfo strukturu

/*
 * ===============================================================================
 * POUŽITÍ MODULŮ V VLASTNÍM C++ PROGRAMU
 * ===============================================================================
 * 
 * Tento sampler systém je navržen modulárně pro snadné použití v jiných projektech.
 * 
 * ZÁKLADNÍ STRUKTURA MODULŮ:
 * 
 * 1. CORE_LOGGER MODULE (core_logger.h/cpp)
 *    - Nezávislý logging systém
 *    - Thread-safe operace
 *    - Automatická správa souborů a složek
 *    - Použití: Logger logger("./"); logger.log("component", "level", "message");
 * 
 * 2. SAMPLER_IO MODULE (sampler_io.h/cpp)
 *    - Čistě IO operace s WAV soubory
 *    - Parsování názvů podle patternu mXXX-velY-ZZ.wav
 *    - Načítání metadat pomocí libsndfile
 *    - Vyhledávání samples podle MIDI noty a velocity
 *    - Závislosti: core_logger, libsndfile
 * 
 * 3. SAMPLER MODULE (sampler.h/cpp)
 *    - Hlavní koordinátor všech audio modulů
 *    - Řídí SamplerIO a připraven pro další moduly (efekty, sekvencer)
 *    - Poskytuje jednotné API pro celý systém
 *    - Závislosti: core_logger, sampler_io
 * 
 * PŘÍKLAD POUŽITÍ V NOVÉM PROJEKTU:
 * 
 * // Minimální použití pouze SamplerIO:
 * #include "core_logger.h"
 * #include "sampler_io.h"
 * 
 * Logger logger("./logs");
 * SamplerIO io(logger);
 * io.loadSamples("path/to/samples");
 * int index = io.findSample(60, 5);  // Middle C, velocity 5
 * 
 * // Plné použití přes Sampler koordinátor:
 * #include "sampler.h"  // Automaticky includuje vše potřebné
 * 
 * Logger logger("./logs");
 * Sampler sampler(logger);
 * sampler.loadSamples("path/to/samples");
 * int index = sampler.findSample(60, 5);
 * const auto& samples = sampler.getSampleList();
 * 
 * // Nebo použití hotové runSampler() funkce:
 * Logger logger("./logs");
 * int result = runSampler(logger);  // Obsahuje celý workflow
 * 
 * POŽADOVANÉ ZÁVISLOSTI PRO CMAKE:
 * - libsndfile (pro WAV operace)
 * - C++17 standard (pro std::filesystem)
 * 
 * CMAKE SETUP:
 * add_executable(MyProject main.cpp sampler/core_logger.cpp sampler/sampler_io.cpp sampler/sampler.cpp)
 * target_include_directories(MyProject PRIVATE sampler/)
 * target_link_libraries(MyProject PRIVATE sndfile)
 * 
 * ===============================================================================
 */

// Forward deklarace SamplerIO - skutečná definice je v sampler_io.h
class SamplerIO;

// Hlavní třída Sampler pro řízení všech audio modulů
// Řídí SamplerIO a v budoucnu další moduly (efekty, sekvencer, atd.)
// Používá logger pro koordinaci všech operací
class Sampler {
public:
    // Konstruktor: Inicializuje sampler s loggerem
    // @param logger Reference na Logger pro logování všech operací
    explicit Sampler(Logger& logger);

    // Destruktor: Loguje ukončení instance
    ~Sampler();

    // Metoda pro načtení samples z adresáře pomocí SamplerIO
    // @param directoryPath Cesta k adresáři se samples
    void loadSamples(const std::string& directoryPath);

    // Metoda pro vyhledání sample podle MIDI noty a velocity
    // Deleguje na SamplerIO modul
    // @param midi_note MIDI nota (0-127)
    // @param velocity Velocity (0-7)
    // @return Index v seznamu nebo -1 pokud nenalezeno
    int findSample(uint8_t midi_note, uint8_t velocity) const;

    // Getter pro přístup k sample seznamu přes SamplerIO
    // @return Konstantní reference na vektor SampleInfo
    const std::vector<SampleInfo>& getSampleList() const;

private:
    Logger& logger_;                           // Reference na logger
    std::unique_ptr<SamplerIO> samplerIO_;     // SamplerIO modul pro práci se soubory
    
    // V budoucnu zde budou další moduly:
    // std::unique_ptr<EffectProcessor> effects_;
    // std::unique_ptr<Sequencer> sequencer_;
    // std::unique_ptr<MidiController> midi_;
    // atd.
};

// Deklarace funkce pro řízení hlavního sampleru
// Volána z main.cpp; vytváří Sampler instanci a řídí celkový workflow
// Obsahuje kompletní demonstraci všech funkcí systému
// @param logger Reference na Logger pro logování
// @return 0 při úspěchu, 1 při chybě (chyby jsou řešeny ukončením programu)
int runSampler(Logger& logger);

#endif  // SAMPLER_H