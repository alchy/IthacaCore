#ifndef SAMPLER_H
#define SAMPLER_H

#include <cstdint>
#include <string>
#include <vector>
#include <sndfile.h>

#include "core_logger.h"
#include "envelopes/envelope_static_data.h"

// Configuration constants
#define DEFAULT_SAMPLE_DIR_VARIANT R"(C:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)"
#define DEFAULT_SAMPLE_DIR R"(C:\Users\nemej992\AppData\Roaming\IthacaPlayer\instrument)"
#define DEFAULT_SAMPLE_RATE 44100
#define ALTERNATIVE_SAMPLE_RATE 48000
#define DEFAULT_JUCE_BLOCK_SIZE 512
#define MAX_JUCE_BLOCK_SIZE 2048

/**
 * @struct SampleInfo
 * @brief Metadata pro jeden audio sample soubor
 */
struct SampleInfo {
    char filename[256];              // Plná cesta k souboru
    uint8_t midi_note;              // MIDI nota (0-127)
    uint8_t midi_note_velocity;     // Velocity layer (0-7)
    int frequency;                  // Sample rate v Hz
    int sample_count;        // Počet frames (stereo pairs)
    double duration_seconds;        // Délka v sekundách
    int channels;                   // Počet kanálů (1 = mono, 2 = stereo)
    bool is_stereo;                // True pokud channels >= 2
    bool interleaved_format;        // True pro interleaved stereo (standard)
    bool needs_conversion;          // True pokud potřebuje konverzi do float
};

/**
 * @class SamplerIO
 * @brief Třída pro skenování a metadata management audio samples
 * LOCKED CLASS - neměnit implementaci!
 */
class SamplerIO {
public:
    SamplerIO();
    ~SamplerIO();

    void scanSampleDirectory(const std::string& directoryPath, Logger& logger);
    int findSampleInSampleList(uint8_t midi_note, uint8_t velocity, int sampleRate) const;
    const std::vector<SampleInfo>& getLoadedSampleList() const;

    // Gettery pro přístup k metadatům
    const char* getFilename(int index, Logger& logger) const;
    uint8_t getMidiNote(int index, Logger& logger) const;
    uint8_t getMidiNoteVelocity(int index, Logger& logger) const;
    int getFrequency(int index, Logger& logger) const;
    int getSampleCount(int index, Logger& logger) const;
    double getDurationInSeconds(int index, Logger& logger) const;
    int getChannelCount(int index, Logger& logger) const;
    bool getIsStereo(int index, Logger& logger) const;
    bool getIsInterleavedFormat(int index, Logger& logger) const;
    bool getNeedsConversion(int index, Logger& logger) const;

private:
    std::vector<SampleInfo> sampleList;
    bool detectInterleavedFormat(const char* filename, Logger& logger) const;
    bool detectFloatConversionNeed(const char* filename, Logger& logger) const;
};

// Forward declarations
class VoiceManager;

/**
 * @brief CORE: runSampler - hlavní produkční funkce
 * 
 * Inicializuje sampler systém s envelope static data.
 * Provede základní ověření funkčnosti.
 * 
 * @param logger Reference na Logger pro zaznamenání
 * @return 0 při úspěchu, 1 při chybě
 */
int runSampler(Logger& logger);

#endif // SAMPLER_H