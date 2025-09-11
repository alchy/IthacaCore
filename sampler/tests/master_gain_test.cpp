#include "master_gain_test.h"
#include "../core_logger.h"
#include "../voice_manager.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <filesystem>

MasterGainTest::MasterGainTest(Logger& logger, const TestConfig& config)
    : TestBase("MasterGainTest", logger, config) {}

bool MasterGainTest::shouldExportAudio() const { return config().exportAudio; }

std::vector<std::string> MasterGainTest::getExportFileNames() const {
    std::vector<std::string> out;
    for (float gain : config().testMasterGains) {
        out.push_back("master_gain_" + std::to_string(gain) + ".wav");
    }
    return out;
}

TestResult MasterGainTest::runTest(VoiceManager& voiceManager) {
    TestResult result;
    result.testName = getTestName();
    try {
        logProgress("Starting master gain test with real VoiceManager API");
        uint8_t testMidi = findValidTestMidiNote(voiceManager, 60);
        bool testPassed = true;

        const int blockSize = config().exportBlockSize;
        float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
        float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
        float* exportBuffer = shouldExportAudio() ? createDummyAudioBuffer(blockSize, 2) : nullptr;

        for (float masterGain : config().testMasterGains) {
            logger_.log("MasterGainTest/runTest", "info", 
                        "Testing master gain " + std::to_string(masterGain));

            Voice& voice = voiceManager.getVoice(testMidi);
            
            // Set master gain using the voice's API
            voice.setMasterGain(masterGain, logger_);
            
            // Test with fixed velocity (100)
            voiceManager.setNoteState(testMidi, true, 100);

            memset(leftBuffer, 0, blockSize * sizeof(float));
            memset(rightBuffer, 0, blockSize * sizeof(float));

            if (voiceManager.processBlock(leftBuffer, rightBuffer, blockSize)) {
                AudioStats s = analyzeAudioBuffer(leftBuffer, blockSize, 1);
                
                // Verify that master gain was set correctly
                float actualMasterGain = voice.getMasterGain();
                bool masterGainOk = std::abs(actualMasterGain - masterGain) <= 0.001f;

                logger_.log("MasterGainTest/runTest", "info", 
                            "Master gain " + std::to_string(masterGain) + 
                            " → Peak output: " + std::to_string(s.peakLevel) + 
                            ", RMS: " + std::to_string(s.rmsLevel) +
                            ", Actual master gain: " + std::to_string(actualMasterGain) +
                            ", Velocity gain: " + std::to_string(voice.getVelocityGain()) +
                            ", Final gain: " + std::to_string(voice.getFinalGain()));

                if (shouldExportAudio() && exportBuffer) {
                    for (int i = 0; i < blockSize; ++i) {
                        exportBuffer[i * 2] = leftBuffer[i];
                        exportBuffer[i * 2 + 1] = rightBuffer[i];
                    }
                    std::string filename = "master_gain_" + std::to_string(masterGain) + ".wav";
                    exportTestAudio(filename, exportBuffer, blockSize, 2, 44100);
                    logger_.log("MasterGainTest/runTest", "info", "Exported: " + filename);
                }

                if (!masterGainOk) {
                    testPassed = false;
                    logger_.log("MasterGainTest/runTest", "error", 
                                "Master gain not set correctly - expected: " + std::to_string(masterGain) +
                                ", actual: " + std::to_string(actualMasterGain));
                }
                
                // Store test data for verification
                MasterGainTestData data;
                data.masterGain = masterGain;
                data.measuredLevel = s.peakLevel;
                data.passed = masterGainOk;
                testResults_.push_back(data);
                
            } else {
                testPassed = false;
                logger_.log("MasterGainTest/runTest", "error", 
                            "No audio output for master gain test");
            }

            voiceManager.setNoteState(testMidi, false, 0);
            
            // Process a few blocks for cleanup
            for (int i = 0; i < 3; ++i) {
                voiceManager.processBlock(leftBuffer, rightBuffer, blockSize);
            }
        }

        // Test master gain and velocity interaction
        if (!testMasterGainVelocityInteraction(voiceManager, testMidi)) {
            testPassed = false;
        }
        
        // Verify linearity of master gain effect
        std::vector<float> gains, levels;
        for (const auto& data : testResults_) {
            if (data.passed) {
                gains.push_back(data.masterGain);
                levels.push_back(data.measuredLevel);
            }
        }
        
        if (!verifyMasterGainLinearity(gains, levels)) {
            testPassed = false;
            logger_.log("MasterGainTest/runTest", "warn", 
                       "Master gain linearity verification failed");
        }

        destroyDummyAudioBuffer(leftBuffer);
        destroyDummyAudioBuffer(rightBuffer);
        if (exportBuffer) destroyDummyAudioBuffer(exportBuffer);

        result.passed = testPassed;
        result.details = "Tested " + std::to_string(testResults_.size()) + " master gain levels with linearity verification";
        logTestResult("master_gain_test", result.passed, result.details);
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.errorMessage = e.what();
        logTestResult("exception", false, result.errorMessage);
    }
    return result;
}

bool MasterGainTest::testSingleMasterGain(VoiceManager& voiceManager, float masterGain, uint8_t testMidi) {
    // Implementation for single master gain test
    // This method can be used for more detailed testing if needed
    return true;
}

bool MasterGainTest::testMasterGainVelocityInteraction(VoiceManager& voiceManager, uint8_t testMidi) {
    logger_.log("MasterGainTest/testMasterGainVelocityInteraction", "info", 
               "Testing master gain and velocity interaction");
    
    Voice& voice = voiceManager.getVoice(testMidi);
    
    // Set a specific master gain
    float testMasterGain = 0.5f;
    voice.setMasterGain(testMasterGain, logger_);
    
    // Test with different velocities
    std::vector<uint8_t> velocities = {32, 64, 127};
    bool interactionOk = true;
    
    for (uint8_t velocity : velocities) {
        voiceManager.setNoteState(testMidi, true, velocity);
        
        float expectedVelocityGain = std::sqrt(static_cast<float>(velocity) / 127.0f);
        float expectedFinalGain = expectedVelocityGain * testMasterGain;
        float actualFinalGain = voice.getFinalGain();
        
        if (std::abs(expectedFinalGain - actualFinalGain) > 0.01f) {
            interactionOk = false;
            logger_.log("MasterGainTest/testMasterGainVelocityInteraction", "error", 
                       "Gain interaction failed for velocity " + std::to_string(velocity) +
                       " - expected: " + std::to_string(expectedFinalGain) +
                       ", actual: " + std::to_string(actualFinalGain));
        }
        
        voiceManager.setNoteState(testMidi, false, 0);
    }
    
    return interactionOk;
}

bool MasterGainTest::verifyMasterGainLinearity(const std::vector<float>& gains, const std::vector<float>& levels) {
    if (gains.size() < 2 || levels.size() < 2 || gains.size() != levels.size()) {
        return false;
    }
    
    // Simple linearity check - higher gain should generally produce higher levels
    // (This is a simplified check; real-world audio might have more complex relationships)
    bool linearityOk = true;
    
    for (size_t i = 1; i < gains.size(); ++i) {
        if (gains[i] > gains[i-1] && levels[i] < levels[i-1] * 0.9f) {
            linearityOk = false;
            logger_.log("MasterGainTest/verifyMasterGainLinearity", "warn", 
                       "Linearity issue detected between gain " + std::to_string(gains[i-1]) +
                       " and " + std::to_string(gains[i]));
        }
    }
    
    return linearityOk;
}