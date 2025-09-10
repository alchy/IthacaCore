#ifndef TEST_BASE_H
#define TEST_BASE_H

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

/**
 * @file test_base.h
 * @brief Základní třída a pomocné typy pro testy (TestBase, TestResult, TestConfig).
 */

class Logger;      // forward declaration (project provides real implementation)
class VoiceManager; // forward declaration (project provides real implementation)

struct TestConfig {
    std::string exportDir = "./exports";
    int exportBlockSize = 512;
    bool exportAudio = false;
    bool verboseLogging = false;
    int defaultTestVelocity = 100;
    std::vector<float> testMasterGains = {0.0f, 0.5f, 1.0f};
    TestConfig() = default;
};

struct TestResult {
    std::string testName;
    bool passed = false;
    std::string errorMessage;
    std::string details;
};

struct AudioStats {
    float peakLevel = 0.0f;
    float rmsLevel = 0.0f;
};

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

    // Writes simple 16-bit WAV PCM file to disk. Returns true on success.
    bool exportTestAudio(const std::string& filename, const float* buffer, int frames, int channels = 2, int sampleRate = 44100);

    uint8_t findValidTestMidiNote(VoiceManager& vm, uint8_t fallback = 60);
    std::vector<uint8_t> findValidNotesForPolyphony(VoiceManager& vm, size_t count, uint8_t start = 48);

    const TestConfig& config() const { return config_; }
    Logger& logger_;

private:
    std::string testName_;
    TestConfig config_;
};

#endif // TEST_BASE_H
