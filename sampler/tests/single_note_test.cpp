#include "single_note_test.h"
#include "../core_logger.h"
#include "../voice_manager.h"
#include <vector>
#include <filesystem>

SingleNoteTest::SingleNoteTest(Logger& logger, const TestConfig& config)
    : TestBase("SingleNoteTest", logger, config) {}

bool SingleNoteTest::shouldExportAudio() const { return config().exportAudio; }

std::vector<std::string> SingleNoteTest::getExportFileNames() const {
    return { "single_note_test.wav" };
}

TestResult SingleNoteTest::runTest(VoiceManager& voiceManager) {
    TestResult result;
    result.testName = getTestName();
    try {
        logProgress("Starting single note test");
        voiceManager.resetAllVoices(logger_);
        uint8_t testMidi = findValidTestMidiNote(voiceManager, 60);
        uint8_t testVelocity = config().defaultTestVelocity;
        bool testPassed = true;

        voiceManager.setNoteState(testMidi, true, testVelocity);
        int activeCount = voiceManager.getActiveVoicesCount();
        if (activeCount != 1) {
            testPassed = false;
            logger_.log("SingleNoteTest/runTest", "error", 
                        "Expected 1 active voice, got " + std::to_string(activeCount));
        }

        const int blockSize = config().exportBlockSize;
        const int numBlocks = 5;
        float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
        float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
        float* exportBuffer = shouldExportAudio() ? createDummyAudioBuffer(blockSize, 2) : nullptr;

        for (int block = 0; block < numBlocks; ++block) {
            memset(leftBuffer, 0, blockSize * sizeof(float));
            memset(rightBuffer, 0, blockSize * sizeof(float));
            bool hasAudio = voiceManager.processBlock(leftBuffer, rightBuffer, blockSize);
            AudioStats s = analyzeAudioBuffer(leftBuffer, blockSize, 1);

            if (hasAudio) {
                logger_.log("SingleNoteTest/runTest", "info", 
                            "Block " + std::to_string(block) + 
                            " - Peak L: " + std::to_string(s.peakLevel));
                if (shouldExportAudio() && exportBuffer) {
                    for (int i = 0; i < blockSize; ++i) {
                        exportBuffer[i * 2] = leftBuffer[i];
                        exportBuffer[i * 2 + 1] = rightBuffer[i];
                    }
                    exportTestAudio("single_note_test.wav", exportBuffer, blockSize, 2, 44100);
                    logger_.log("SingleNoteTest/runTest", "info", "Exported: single_note_test.wav");
                }
            } else {
                logger_.log("SingleNoteTest/runTest", "info", 
                            "Block " + std::to_string(block) + " - Silent");
            }
        }

        destroyDummyAudioBuffer(leftBuffer);
        destroyDummyAudioBuffer(rightBuffer);
        if (exportBuffer) destroyDummyAudioBuffer(exportBuffer);

        voiceManager.setNoteState(testMidi, false, 0);

        result.passed = testPassed;
        result.details = "Single note test completed";
        logTestResult("single_note_test", result.passed, result.details);
    } catch (const std::exception& e) {
        result.passed = false;
        result.errorMessage = e.what();
        logTestResult("exception", false, result.errorMessage);
    }
    return result;
}