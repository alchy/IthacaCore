#ifndef SAMPLER_H
#define SAMPLER_H

// Globální flag pro testovací modul - definován vždy
#define ENABLE_TESTS 1

#include <cstdint>
#include <string>
#include <vector>
#include <sndfile.h>

#include "core_logger.h"
#include "envelopes/envelope_static_data.h"  // NOVÝ INCLUDE pro refaktorovaný envelope systém

// Configuration constants pro VoiceManager s EnvelopeStaticData
#define DEFAULT_SAMPLE_DIR R"(C:\Users\nemej992\AppData\Roaming\IthacaPlayer\instrument)"
#define DEFAULT_SAMPLE_RATE 44100
#define ALTERNATIVE_SAMPLE_RATE 48000

// Test configuration constants - rozšířeno pro envelope testování
#define TEST_MIDI_NOTE 108
#define TEST_VELOCITY 5
#define TEST_VELOCITY_FULL 100
#define EXPORT_DIR "./exports"
#define EXPORT_FILENAME "export_test.wav"
#define EXPORT_FRAMES_PER_BUFFER 512
#define AUDIO_BLOCK_SIZE 512
#define POLYPHONY_TEST_NOTES 3

// Voice testing constants
#define VOICE_TEST_BLOCKS 5
#define VOICE_RELEASE_BLOCKS 3
#define MIDI_NOTE_SEARCH_START 50
#define MIDI_NOTE_SEARCH_END 80

// JUCE integration constants
#define DEFAULT_JUCE_BLOCK_SIZE 512
#define MAX_JUCE_BLOCK_SIZE 2048

// Envelope testing constants - NOVÉ pro refaktorovaný systém
#define ENVELOPE_TEST_ATTACK_MIDI 64
#define ENVELOPE_TEST_RELEASE_MIDI 64
#define ENVELOPE_TEST_SUSTAIN_MIDI 90
#define ENVELOPE_SAMPLE_RATE_SWITCH_TEST_BLOCKS 10
#define ENVELOPE_MEMORY_TEST_INSTANCES 16

/**
 * @struct SampleInfo
 * @brief Metadata pro jeden audio sample soubor
 * Obsahuje všechny potřebné informace für načtení a zpracování samples
 * NEZMĚNĚNO - kompatibilní s refaktorovaným envelope systémem
 */
struct SampleInfo {
    char filename[256];              // Plná cesta k souboru
    uint8_t midi_note;              // MIDI nota (0-127)
    uint8_t midi_note_velocity;     // Velocity layer (0-7)
    int frequency;                  // Sample rate v Hz
    sf_count_t sample_count;        // Počet frames (stereo pairs)
    double duration_seconds;        // Délka v sekundách
    int channels;                   // Počet kanálů (1 = mono, 2 = stereo)
    bool is_stereo;                // True pokud channels >= 2
    bool interleaved_format;        // True pro interleaved stereo (standard)
    bool needs_conversion;          // True pokud potřebuje konverzi do float
};

/**
 * @class SamplerIO
 * @brief Třída pro skenování a metadata management audio samples
 * 
 * LOCKED CLASS - neměnit implementaci!
 * Poskytuje rozhraní pro načtení metadat ze sample adresáře
 * a vyhledávání samples podle MIDI noty, velocity a sample rate.
 * KOMPATIBILNÍ s refaktorovaným EnvelopeStaticData systémem.
 */
class SamplerIO {
public:
    SamplerIO();
    ~SamplerIO();

    /**
     * @brief Prohledá adresář s WAV soubory a naplní seznam metadat
     * @param directoryPath Cesta k adresáři se samples
     * @param logger Reference na logger pro zaznamenávání
     */
    void scanSampleDirectory(const std::string& directoryPath, Logger& logger);

    /**
     * @brief Vyhledá index sample v interním seznamu podle MIDI noty, velocity a požadované frekvence vzorkování
     * @param midi_note MIDI nota (0-127)
     * @param velocity Velocity (0-7)
     * @param sampleRate Požadovaná frekvence vzorkování v Hz (např. 44100)
     * @return Index v seznamu nebo -1 pokud nenalezeno
     */
    int findSampleInSampleList(uint8_t midi_note, uint8_t velocity, int sampleRate) const;

    /**
     * @brief Getter pro přístup k načtenému seznamu samples
     * @return Konstantní reference na vektor SampleInfo
     */
    const std::vector<SampleInfo>& getLoadedSampleList() const;

    // Gettery pro přístup k metadatům na konkrétním indexu
    const char* getFilename(int index, Logger& logger) const;
    uint8_t getMidiNote(int index, Logger& logger) const;
    uint8_t getMidiNoteVelocity(int index, Logger& logger) const;
    int getFrequency(int index, Logger& logger) const;
    sf_count_t getSampleCount(int index, Logger& logger) const;
    double getDurationInSeconds(int index, Logger& logger) const;
    int getChannelCount(int index, Logger& logger) const;
    bool getIsStereo(int index, Logger& logger) const;
    bool getIsInterleavedFormat(int index, Logger& logger) const;
    bool getNeedsConversion(int index, Logger& logger) const;

private:
    std::vector<SampleInfo> sampleList;
    
    // Private helper methods
    bool detectInterleavedFormat(const char* filename, Logger& logger) const;
    bool detectFloatConversionNeed(const char* filename, Logger& logger) const;
};

// Forward declarations pro testovací systém s EnvelopeStaticData podporou
#ifdef ENABLE_TESTS
class VoiceManagerTester;
class InstrumentLoader;

/**
 * @brief Spustí VoiceManager testy s předaným test managerem
 * @param testManager Reference na VoiceManagerTester instanci
 * @param loader Reference na InstrumentLoader pro inicializaci
 * @param logger Reference na Logger pro výstupy
 * @return int Počet selhání (0 = úspěch)
 * 
 * AKTUALIZOVÁNO pro refaktorovaný envelope systém:
 * - Validuje EnvelopeStaticData inicializaci
 * - Používá novou architekturu s sdílenými envelope daty
 * - Testuje sample rate switching bez reinicializace
 */
int runVoiceManagerTests(VoiceManagerTester& testManager, InstrumentLoader& loader, Logger& logger);

/**
 * @brief NOVÁ funkce pro testování envelope memory optimizations
 * @param logger Reference na Logger
 * @return int Počet selhání při memory testech
 */
int testEnvelopeMemoryOptimizations(Logger& logger);

/**
 * @brief NOVÁ funkce pro testování sample rate switching s EnvelopeStaticData
 * @param logger Reference na Logger
 * @return int Počet selhání při sample rate testech
 */
int testEnvelopeSampleRateSwitching(Logger& logger);

#endif

/**
 * @brief REFAKTOROVÁNO: runSampler s EnvelopeStaticData inicializací
 * 
 * Nahrazuje původní monolitickou runSampler funkci.
 * NOVÁ ARCHITEKTURA s envelope memory optimization:
 * 
 * FÁZE 0: KRITICKÁ - EnvelopeStaticData::initialize() (globálně, jednou pro program)
 * FÁZE 1: VoiceManager instance creation (s validací statických dat)
 * FÁZE 2: System initialization (skenování adresáře - envelope data už jsou sdílená)
 * FÁZE 3: Loading for sample rate (načtení dat - používá sdílená envelope data)
 * FÁZE 4: JUCE preparation (buffer sizes)
 * FÁZE 5: Comprehensive testing (framework + envelope-specific testy)
 * FÁZE 6: Demo testy s refaktorovaným envelope systémem
 * FÁZE 7: System statistics + memory optimization metrics
 * FÁZE 8: Cleanup (EnvelopeStaticData::cleanup())
 * 
 * KLÍČOVÉ ZMĚNY:
 * - EnvelopeStaticData je inicializována jednou pro celý program
 * - Všechny VoiceManager instance sdílejí stejná envelope data
 * - Sample rate switching je O(1) operace bez reinicializace
 * - Dramatické paměťové úspory pro multi-instance scénáře
 * 
 * @param logger Reference na Logger pro zaznamenání
 * @return 0 při úspěchu, 1 při chybě
 */
int runSampler(Logger& logger);

/**
 * @brief JUCE integration helper pro AudioProcessor s EnvelopeStaticData
 * 
 * Ukázkový pattern pro integraci VoiceManager do JUCE AudioProcessor.
 * AKTUALIZOVÁNO pro refaktorovaný envelope systém:
 * - Ukazuje správnou EnvelopeStaticData inicializaci na začátku aplikace
 * - Demonstruje sdílení envelope dat mezi více AudioProcessor instancemi
 * - Testuje sample rate changes bez envelope reinicializace
 * - Ukazuje memory-efficient multi-timbral setup
 * 
 * CRITICAL: EnvelopeStaticData::initialize() MUSÍ být voláno PŘED vytvořením
 * jakýchkoli VoiceManager instancí!
 * 
 * @param logger Reference na Logger
 * @return 0 při úspěchu, 1 při chybě
 */
int demonstrateJuceIntegration(Logger& logger);

/**
 * @brief NOVÁ funkce pro demonstraci multi-instance memory savings
 * 
 * Vytvoří několik VoiceManager instancí a demonstruje paměťové úspory
 * dosažené sdílením EnvelopeStaticData. Srovnává původní vs. refaktorovanou
 * paměťovou spotřebu.
 * 
 * @param numInstances Počet VoiceManager instancí k vytvoření (default 16)
 * @param logger Reference na Logger
 * @return int 0 při úspěchu, počet chyb při problémech
 */
int demonstrateMultiInstanceMemorySavings(int numInstances = 16, Logger& logger);

/**
 * @brief NOVÁ utility funkce pro validaci EnvelopeStaticData stavu
 * 
 * Kontroluje, zda jsou statická envelope data správně inicializována
 * a poskytuje diagnostické informace o jejich stavu.
 * 
 * @param logger Reference na Logger pro výstup diagnostiky
 * @return bool true pokud jsou data validní a připravená k použití
 */
bool validateEnvelopeStaticDataState(Logger& logger);

/**
 * @brief NOVÁ benchmark funkce pro envelope performance
 * 
 * Srovnává performance původního vs. refaktorovaného envelope systému.
 * Měří rychlost přístupu k envelope datům, sample rate switching,
 * a overall processing performance.
 * 
 * @param logger Reference na Logger pro výsledky benchmarku
 * @return int 0 při úspěchu, 1 při chybě
 */
int benchmarkEnvelopePerformance(Logger& logger);

/**
 * @struct EnvelopeMemoryStats
 * @brief NOVÁ struktura pro sledování envelope memory usage
 */
struct EnvelopeMemoryStats {
    size_t totalStaticMemoryBytes;      // Celková velikost statických dat
    size_t perInstanceSavingsBytes;     // Úspora na jednu instanci
    int numActiveInstances;             // Počet aktivních VoiceManager instancí
    size_t totalMemorySavedBytes;       // Celková úspora paměti
    bool isOptimizationActive;          // Flag zda je optimalizace aktivní
};

/**
 * @brief NOVÁ funkce pro získání envelope memory statistik
 * 
 * @param logger Reference na Logger
 * @return EnvelopeMemoryStats Struktura s memory statistikami
 */
EnvelopeMemoryStats getEnvelopeMemoryStatistics(Logger& logger);

#ifdef ENABLE_TESTS

/**
 * @struct TestConfig
 * @brief Rozšířená konfigurace pro testovací framework s envelope podporou
 */
struct TestConfig {
    bool exportAudio = false;                    // Export audio výsledků
    int exportBlockSize = 512;                   // Block size pro export
    uint8_t defaultTestVelocity = 100;           // Default MIDI velocity
    std::vector<float> testMasterGains = {0.8f}; // Test master gain values
    std::string exportDir = "./exports/tests";   // Export directory
    bool verboseLogging = false;                 // Verbose test logging
    
    // NOVÉ envelope-specific konfigurace
    bool testEnvelopeMemoryOptimization = true;  // Test memory optimizations
    bool testSampleRateSwitching = true;         // Test sample rate changes
    std::vector<int> testSampleRates = {44100, 48000}; // Sample rates k testování
    int envelopeTestBlocks = 10;                 // Počet bloků pro envelope testy
    bool validateSharedEnvelopeData = true;      // Validace sdílených dat
};

/**
 * @struct TestResult
 * @brief Rozšířený výsledek testu s envelope metrics
 */
struct TestResult {
    bool passed = false;                         // Základní test result
    std::string errorMessage;                    // Chybová zpráva
    std::string details;                         // Detaily testu
    
    // NOVÉ envelope-specific metrics
    bool envelopeDataValid = false;              // Validita envelope dat
    size_t memoryFootprintBytes = 0;            // Paměťová stopa
    double averageProcessingTimeMs = 0.0;       // Průměrný processing čas
    int sampleRatesSwitched = 0;                // Počet sample rate změn
};

/**
 * @class BaseTest
 * @brief NOVÁ base class pro envelope-aware testy
 */
class BaseTest {
public:
    BaseTest(const std::string& name, Logger& logger, const TestConfig& config)
        : testName_(name), logger_(logger), config_(config) {}
    
    virtual ~BaseTest() = default;
    
    virtual TestResult run(class VoiceManager& voiceManager) = 0;
    
    const std::string& getName() const { return testName_; }

protected:
    std::string testName_;
    Logger& logger_;
    TestConfig config_;
    
    // Helper methods pro envelope testing
    bool validateEnvelopeData() const;
    TestResult createResult(bool passed, const std::string& message = "") const;
    void logTestProgress(const std::string& step) const;
};

/**
 * @class EnvelopeSpecificTest
 * @brief NOVÝ test class specificky pro envelope system
 */
class EnvelopeSpecificTest : public BaseTest {
public:
    EnvelopeSpecificTest(Logger& logger, const TestConfig& config);
    TestResult run(VoiceManager& voiceManager) override;

private:
    TestResult testStaticDataInitialization(VoiceManager& vm);
    TestResult testSampleRateSwitching(VoiceManager& vm);
    TestResult testMemoryFootprint(VoiceManager& vm);
    TestResult testConcurrentAccess(VoiceManager& vm);
};

/**
 * @class MemoryOptimizationTest
 * @brief NOVÝ test pro multi-instance memory optimization
 */
class MemoryOptimizationTest : public BaseTest {
public:
    MemoryOptimizationTest(Logger& logger, const TestConfig& config);
    TestResult run(VoiceManager& voiceManager) override;

private:
    TestResult simulateMultipleInstances();
    TestResult measureMemoryUsage();
    TestResult validateSharedData();
};

#endif // ENABLE_TESTS

#endif // SAMPLER_H