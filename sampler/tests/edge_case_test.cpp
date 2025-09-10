#include "edge_case_test.h"
#include "../core_logger.h"
#include "../voice_manager.h"
#include <string>
#include <filesystem>

EdgeCaseTest::EdgeCaseTest(Logger& logger, const TestConfig& config)
    : TestBase("EdgeCaseTest", logger, config) {}

bool EdgeCaseTest::shouldExportAudio() const { return config().exportAudio; }

std::vector<std::string> EdgeCaseTest::getExportFileNames() const {
    return { "edge_case_invalid_midi.wav", "edge_case_zero_velocity.wav", "edge_case_max_velocity.wav" };
}

TestResult EdgeCaseTest::runTest(VoiceManager& voiceManager) {
    TestResult result;
    result.testName = getTestName();
    try {
        logProgress("Starting edge case tests");
        voiceManager.resetAllVoices(logger_);

        std::vector<std::pair<uint8_t, uint8_t>> testCases = {
            {200, config().defaultTestVelocity}, // Invalid MIDI
            {60, 0},                           // Zero velocity
            {61, 127}                          // Max velocity
        };
        bool testPassed = true;

        const int blockSize = config().exportBlockSize;
        float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
        float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
        float* exportBuffer = shouldExportAudio() ? createDummyAudioBuffer(blockSize, 2) : nullptr;

        for (size_t i = 0; i < testCases.size(); ++i) {
            auto [midi, velocity] = testCases[i];
            std::string testName = getExportFileNames()[i];
            logger_.log("EdgeCaseTest/runTest", "info", 
                        "Testing MIDI " + std::to_string(midi) + " with velocity " + std::to_string(velocity));

            voiceManager.setNoteState(midi, true, velocity);
            memset(leftBuffer, 0, blockSize * sizeof(float));
            memset(rightBuffer, 0, blockSize * sizeof(float));

            bool hasAudio = voiceManager.processBlock(leftBuffer, rightBuffer, blockSize);
            if (hasAudio && shouldExportAudio()) {
                for (int j = 0; j < blockSize; ++j) {
                    exportBuffer[j * 2] = leftBuffer[j];
                    exportBuffer[j * 2 + 1] = rightBuffer[j];
                }
                exportTestAudio(testName, exportBuffer, blockSize, 2, 44100);
                logger_.log("EdgeCaseTest/runTest", "info", "Exported: " + testName);
            }

            voiceManager.setNoteState(midi, false, 0);
        }

        int activeCount = voiceManager.getActiveVoicesCount();
        logTestResult("edge_cases", true, "Active voices after edge cases: " + std::to_string(activeCount));

        voiceManager.stopAllVoices();

        destroyDummyAudioBuffer(leftBuffer);
        destroyDummyAudioBuffer(rightBuffer);
        if (exportBuffer) destroyDummyAudioBuffer(exportBuffer);

        result.passed = testPassed;
        result.details = "Edge case tests completed";
        logTestResult("edge_cases", true, result.details);
    } catch (const std::exception& e) {
        result.passed = false;
        result.errorMessage = e.what();
        logTestResult("exception", false, result.errorMessage);
    }
    return result;
}