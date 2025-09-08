#ifndef SAMPLER_H
#define SAMPLER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <sndfile.h>

#include "core_logger.h"

// NOVÉ: Configuration constants pro VoiceManager
#define DEFAULT_SAMPLE_DIR R"(C:\Users\nemej992\AppData\Roaming\IthacaPlayer\instrument)"
#define ALTERNATIVE_SAMPLE_DIR R"(C:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)"
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

// Existing SampleInfo struct
struct SampleInfo {
    char filename[256];
    uint8_t midi_note;
    uint8_t midi_note_velocity;
    int frequency;
    sf_count_t sample_count;
    double duration_seconds;
    int channels;
    bool is_stereo;
    bool interleaved_format;
    bool needs_conversion;
};

// Existing SamplerIO class - nezměněná
class SamplerIO {
public:
    SamplerIO();
    ~SamplerIO();

    void scanSampleDirectory(const std::string& directoryPath, Logger& logger);
    int findSampleInSampleList(uint8_t midi_note, uint8_t velocity, int sampleRate) const;
    const std::vector<SampleInfo>& getLoadedSampleList() const;

    // Gettery - nezměněné
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
    bool detectInterleavedFormat(const char* filename, Logger& logger) const;
    bool detectFloatConversionNeed(const char* filename, Logger& logger) const;
};

// REFAKTOROVANÉ: runSampler jako thin wrapper
int runSampler(Logger& logger);

#endif // SAMPLER_H