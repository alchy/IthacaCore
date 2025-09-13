#ifndef TEST_BASE_H
#define TEST_BASE_H

#include "test_common.h"  // NOVÝ INCLUDE pro sdílené struktury
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

// Forward declarations
class Logger;
class VoiceManager;
class Voice;

/**
 * @class TestBase
 * @brief Základní třída pro všechny testy s reálnou Logger a VoiceManager integrací
 */
class TestBase {
public:
    TestBase(const std::string& name, Logger& logger, const TestConfig& config = TestConfig{});
    virtual ~TestBase();

    virtual TestResult runTest(VoiceManager& voiceManager) = 0;
    const std::string& getTestName() const;

    virtual bool shouldExportAudio() const;
    virtual std::vector<std::string> getExportFileNames() const;

protected:
    void logProgress(const std::string& message);
    void logTestResult(const std::string& step, bool passed, const std::string& details = "");

    static AudioStats analyzeAudioBuffer(const float* buffer, int blockSize, int channels = 1);
    static float* createDummyAudioBuffer(int blockSize, int channels = 1);
    static void destroyDummyAudioBuffer(float* buffer);

    /**
     * @brief Vylepšený WAV export s automatickým vytvořením adresářů
     */
    bool exportTestAudio(const std::string& filename, const float* buffer, int frames, int channels = 2, int sampleRate = 44100);

    /**
     * @brief Inteligentní vyhledání validní MIDI noty pro testy
     */
    uint8_t findValidTestMidiNote(VoiceManager& vm, uint8_t fallback = 60);
    
    /**
     * @brief Vyhledání více not pro polyfonnĺ testy
     */
    std::vector<uint8_t> findValidNotesForPolyphony(VoiceManager& vm, size_t count, uint8_t start = 48);

    const TestConfig& config() const { return config_; }
    Logger& logger_;

private:
    std::string testName_;
    TestConfig config_;
};

#endif // TEST_BASE_H