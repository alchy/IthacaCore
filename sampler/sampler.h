#ifndef SAMPLER_H
#define SAMPLER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <sndfile.h>

#include "core_logger.h"

// Configuration constants pro VoiceManager
//#define DEFAULT_SAMPLE_DIR R"(C:\Users\nemej992\AppData\Roaming\IthacaPlayer\instrument)"
#define DEFAULT_SAMPLE_DIR R"(C:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)"
// #define ALTERNATIVE_SAMPLE_DIR R"(C:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)"

#define DEFAULT_SAMPLE_RATE 44100
#define ALTERNATIVE_SAMPLE_RATE 48000

// Test configuration constants
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

/**
 * @struct SampleInfo
 * @brief Metadata pro jeden audio sample soubor
 * Obsahuje všechny potřebné informace pro načtení a zpracování samples
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

/**
 * @brief REFAKTOROVANÝ: runSampler jako thin wrapper pro VoiceManager testing
 * 
 * Nahrazuje původní monolitickou runSampler funkci.
 * Nyní vytvoří VoiceManager a spustí kompletní test pipeline:
 * 1. Sample rate setup
 * 2. System initialization 
 * 3. Instrument loading
 * 4. Validation
 * 5. Granular testing
 * 6. Statistics
 * 
 * @param logger Reference na Logger pro zaznamenávání
 * @return 0 při úspěchu, 1 při chybě
 */
int runSampler(Logger& logger);

/**
 * @brief NOVÉ: JUCE integration helper pro AudioProcessor
 * 
 * Ukázkový pattern pro integraci VoiceManager do JUCE AudioProcessor.
 * Ukazuje správné volání prepareToPlay() a processBlock() metod.
 * 
 * @param logger Reference na Logger
 * @return 0 při úspěchu, 1 při chybě
 */
int demonstrateJuceIntegration(Logger& logger);

#endif // SAMPLER_H