#include "velocity_gain_test.h"
#include "../core_logger.h"
#include "../voice_manager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

VelocityGainTest::VelocityGainTest(Logger& logger, const TestConfig& config)
    : TestBase("VelocityGainTest", logger, config) {}

bool VelocityGainTest::shouldExportAudio() const { return config().exportAudio; }

std::vector<std::string> VelocityGainTest::getExportFileNames() const {
    std::vector<std::string> out;
    for (uint8_t v : {1, 32, 64, 96, 127}) {
        out.push_back("velocity_gain_" + std::to_string(v) + ".wav");
    }
    return out;
}

TestResult VelocityGainTest::runTest(VoiceManager& voiceManager) {
    TestResult result;
    result.testName = getTestName();
    try {
        logProgress("Starting velocity gain test with real VoiceManager API");
        uint8_t testMidi = findValidTestMidiNote(voiceManager, 60);
        std::vector<uint8_t> testVelocities = {1, 32, 64, 96, 127};
        bool testPassed = true;

        const int blockSize = config().exportBlockSize;
        float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
        float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
        float* exportBuffer = shouldExportAudio() ? createDummyAudioBuffer(blockSize, 2) : nullptr;

        for (uint8_t velocity : testVelocities) {
            logger_.log("VelocityGainTest/runTest", "info", 
                        "Testing velocity " + std::to_string(velocity) + 
                        " for MIDI " + std::to_string(testMidi));

            // Use real VoiceManager API
            voiceManager.setNoteState(testMidi, true, velocity);
            Voice& voice = voiceManager.getVoice(testMidi);

            memset(leftBuffer, 0, blockSize * sizeof(float));
            memset(rightBuffer, 0, blockSize * sizeof(float));

            if (voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize)) {
                AudioStats s = analyzeAudioBuffer(leftBuffer, blockSize, 1);
                
                // Calculate expected gain using the same formula as Voice::updateVelocityGain
                float expectedVelocityGain = std::sqrt(static_cast<float>(velocity) / 127.0f);
                float actualVelocityGain = voice.getVelocityGain();
                bool gainOk = std::abs(expectedVelocityGain - actualVelocityGain) <= 0.01f;

                logger_.log("VelocityGainTest/runTest", "info", 
                            "Velocity " + std::to_string(velocity) + 
                            " - Peak L: " + std::to_string(s.peakLevel) + 
                            ", RMS L: " + std::to_string(s.rmsLevel) +
                            ", Expected velocity gain: " + std::to_string(expectedVelocityGain) +
                            ", Actual velocity gain: " + std::to_string(actualVelocityGain) +
                            ", Master gain: " + std::to_string(voice.getMasterGain()) +
                            ", Final gain: " + std::to_string(voice.getFinalGain()));

                if (shouldExportAudio() && exportBuffer) {
                    for (int i = 0; i < blockSize; ++i) {
                        exportBuffer[i * 2] = leftBuffer[i];
                        exportBuffer[i * 2 + 1] = rightBuffer[i];
                    }
                    std::string filename = "velocity_gain_" + std::to_string(velocity) + ".wav";
                    exportTestAudio(filename, exportBuffer, blockSize, 2, 44100);
                    logger_.log("VelocityGainTest/runTest", "info", "Exported: " + filename);
                }

                if (!gainOk) {
                    testPassed = false;
                    logger_.log("VelocityGainTest/runTest", "error", 
                                "Velocity gain verification failed for velocity " + std::to_string(velocity) +
                                " - expected: " + std::to_string(expectedVelocityGain) +
                                ", actual: " + std::to_string(actualVelocityGain));
                }
            } else {
                testPassed = false;
                logger_.log("VelocityGainTest/runTest", "error", 
                            "No audio output for velocity " + std::to_string(velocity));
            }

            voiceManager.setNoteState(testMidi, false, 0);
            
            // Process a few more blocks to allow release phase
            for (int i = 0; i < 3; ++i) {
                voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
            }
        }

        destroyDummyAudioBuffer(leftBuffer);
        destroyDummyAudioBuffer(rightBuffer);
        if (exportBuffer) destroyDummyAudioBuffer(exportBuffer);

        result.passed = testPassed;
        result.details = "Tested velocities: 1, 32, 64, 96, 127 with gain verification";
        logTestResult("velocity_gain_test", result.passed, result.details);
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.errorMessage = e.what();
        logTestResult("exception", false, result.errorMessage);
    }
    return result;
}