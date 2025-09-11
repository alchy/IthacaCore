#include "envelope_test.h"
#include "../core_logger.h"
#include "../voice_manager.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <algorithm>

EnvelopeTest::EnvelopeTest(Logger& logger, const TestConfig& config)
    : TestBase("EnvelopeTest", logger, config) {}

bool EnvelopeTest::shouldExportAudio() const { return config().exportAudio; }

std::vector<std::string> EnvelopeTest::getExportFileNames() const {
    return { "envelope_attack.wav", "envelope_release.wav", "envelope_full_cycle.wav" };
}

TestResult EnvelopeTest::runTest(VoiceManager& voiceManager) {
    TestResult result;
    result.testName = getTestName();
    try {
        logProgress("Starting enhanced envelope test with gain monitoring");
        uint8_t testMidi = findValidTestMidiNote(voiceManager, 60);
        uint8_t testVelocity = config().defaultTestVelocity;
        bool testPassed = true;

        logger_.log("EnvelopeTest/runTest", "info", 
                    "Testing MIDI " + std::to_string(testMidi) + 
                    " with velocity " + std::to_string(testVelocity) + 
                    " (enhanced envelope monitoring)");

        Voice& voice = voiceManager.getVoice(testMidi);
        
        // Get debug info before starting
        voice.getGainDebugInfo(logger_);

        const int blockSize = config().exportBlockSize;
        const int attackBlocks = 10;  // Blocks for attack phase
        const int sustainBlocks = 5;  // Blocks for sustain phase  
        const int releaseBlocks = 10; // Blocks for release phase
        const int totalBlocks = attackBlocks + sustainBlocks + releaseBlocks;
        
        float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
        float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
        float* exportBuffer = shouldExportAudio() ? createDummyAudioBuffer(blockSize, 2) : nullptr;
        
        std::vector<float> envelopeGains;
        std::vector<float> peakLevels;
        std::vector<int> voiceStates;

        // Start note for attack phase
        voiceManager.setNoteState(testMidi, true, testVelocity);
        
        // Process all blocks and monitor envelope behavior
        for (int block = 0; block < totalBlocks; ++block) {
            memset(leftBuffer, 0, blockSize * sizeof(float));
            memset(rightBuffer, 0, blockSize * sizeof(float));
            
            bool hasAudio = voiceManager.processBlock(leftBuffer, rightBuffer, blockSize);
            AudioStats s = analyzeAudioBuffer(leftBuffer, blockSize, 1);
            
            // Collect envelope data
            float currentEnvelopeGain = voice.getCurrentEnvelopeGain();
            envelopeGains.push_back(currentEnvelopeGain);
            peakLevels.push_back(s.peakLevel);
            voiceStates.push_back(static_cast<int>(voice.getState()));

            if (hasAudio) {
                logger_.log("EnvelopeTest/runTest", "info", 
                            "Block " + std::to_string(block) + 
                            " - Peak: " + std::to_string(s.peakLevel) + 
                            ", RMS: " + std::to_string(s.rmsLevel) +
                            ", Envelope gain: " + std::to_string(currentEnvelopeGain) +
                            ", Velocity gain: " + std::to_string(voice.getVelocityGain()) +
                            ", Final gain: " + std::to_string(voice.getFinalGain()) +
                            ", State: " + std::to_string(static_cast<int>(voice.getState())));

                // Export different phases
                if (shouldExportAudio() && exportBuffer) {
                    for (int i = 0; i < blockSize; ++i) {
                        exportBuffer[i * 2] = leftBuffer[i];
                        exportBuffer[i * 2 + 1] = rightBuffer[i];
                    }
                    
                    std::string filename;
                    if (block < attackBlocks) {
                        filename = "envelope_attack.wav";
                    } else if (block < attackBlocks + sustainBlocks) {
                        filename = "envelope_sustain.wav"; 
                    } else {
                        filename = "envelope_release.wav";
                    }
                    
                    // Only export first block of each phase to avoid overwriting
                    if ((block == 0) || (block == attackBlocks) || (block == attackBlocks + sustainBlocks)) {
                        exportTestAudio(filename, exportBuffer, blockSize, 2, 44100);
                        logger_.log("EnvelopeTest/runTest", "info", "Exported: " + filename);
                    }
                }
            } else if (block < attackBlocks + sustainBlocks) {
                // Should have audio during attack and sustain phases
                testPassed = false;
                logger_.log("EnvelopeTest/runTest", "error", 
                            "No audio during active phase at block " + std::to_string(block));
            }

            // Transition to release phase
            if (block == attackBlocks + sustainBlocks) {
                voiceManager.setNoteState(testMidi, false, 0);
                logger_.log("EnvelopeTest/runTest", "info", 
                            "Note-off sent - monitoring release envelope");
            }
        }

        // Export full cycle
        if (shouldExportAudio() && exportBuffer && !envelopeGains.empty()) {
            // Create a synthetic full cycle for export
            voiceManager.setNoteState(testMidi, true, testVelocity);
            memset(leftBuffer, 0, blockSize * sizeof(float));
            memset(rightBuffer, 0, blockSize * sizeof(float));
            voiceManager.processBlock(leftBuffer, rightBuffer, blockSize);
            
            for (int i = 0; i < blockSize; ++i) {
                exportBuffer[i * 2] = leftBuffer[i];
                exportBuffer[i * 2 + 1] = rightBuffer[i];
            }
            exportTestAudio("envelope_full_cycle.wav", exportBuffer, blockSize, 2, 44100);
            logger_.log("EnvelopeTest/runTest", "info", "Exported: envelope_full_cycle.wav");
            
            voiceManager.setNoteState(testMidi, false, 0);
        }

        // Analyze envelope behavior
        bool attackPhaseOk = analyzeAttackPhase(envelopeGains, attackBlocks);
        bool sustainPhaseOk = analyzeSustainPhase(envelopeGains, attackBlocks, sustainBlocks);
        bool releasePhaseOk = analyzeReleasePhase(envelopeGains, attackBlocks + sustainBlocks, releaseBlocks);

        logTestResult("attack_phase", attackPhaseOk, 
                     attackPhaseOk ? "Attack envelope behavior correct" : "Attack envelope issues detected");
        logTestResult("sustain_phase", sustainPhaseOk, 
                     sustainPhaseOk ? "Sustain envelope behavior correct" : "Sustain envelope issues detected");
        logTestResult("release_phase", releasePhaseOk, 
                     releasePhaseOk ? "Release envelope behavior correct" : "Release envelope issues detected");

        destroyDummyAudioBuffer(leftBuffer);
        destroyDummyAudioBuffer(rightBuffer);
        if (exportBuffer) destroyDummyAudioBuffer(exportBuffer);

        testPassed = testPassed && attackPhaseOk && sustainPhaseOk && releasePhaseOk;
        result.passed = testPassed;
        result.details = "Envelope phases analyzed: " + std::to_string(envelopeGains.size()) + 
                        " samples across " + std::to_string(totalBlocks) + " blocks";
        
        if (!result.passed) {
            result.errorMessage = "Envelope behavior not as expected in one or more phases";
        }
        
        logTestResult("envelope_test", result.passed, result.details);
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.errorMessage = e.what();
        logTestResult("exception", false, result.errorMessage);
    }
    return result;
}

bool EnvelopeTest::analyzeAttackPhase(const std::vector<float>& envelopeGains, int attackBlocks) {
    if (envelopeGains.size() < static_cast<size_t>(attackBlocks) || attackBlocks <= 1) {
        return false;
    }
    
    // Attack phase should generally show increasing gain values
    int increasingCount = 0;
    for (int i = 1; i < attackBlocks && i < static_cast<int>(envelopeGains.size()); ++i) {
        if (envelopeGains[i] >= envelopeGains[i-1]) {
            increasingCount++;
        }
    }
    
    // Allow some tolerance - at least 70% of transitions should be increasing
    bool attackOk = (static_cast<float>(increasingCount) / static_cast<float>(attackBlocks - 1)) >= 0.7f;
    
    logger_.log("EnvelopeTest/analyzeAttackPhase", "info", 
               "Attack analysis: " + std::to_string(increasingCount) + "/" + 
               std::to_string(attackBlocks - 1) + " transitions increasing");
    
    return attackOk;
}

bool EnvelopeTest::analyzeSustainPhase(const std::vector<float>& envelopeGains, int attackBlocks, int sustainBlocks) {
    int sustainStart = attackBlocks;
    int sustainEnd = attackBlocks + sustainBlocks;
    
    if (envelopeGains.size() < static_cast<size_t>(sustainEnd) || sustainBlocks <= 1) {
        return false;
    }
    
    // Sustain phase should maintain relatively stable gain
    float sustainLevel = envelopeGains[sustainStart];
    float maxVariation = 0.0f;
    
    for (int i = sustainStart; i < sustainEnd && i < static_cast<int>(envelopeGains.size()); ++i) {
        float variation = std::abs(envelopeGains[i] - sustainLevel);
        maxVariation = std::max(maxVariation, variation);
    }
    
    // Sustain should be relatively stable (within 20% variation)
    bool sustainOk = (maxVariation <= 0.2f);
    
    logger_.log("EnvelopeTest/analyzeSustainPhase", "info", 
               "Sustain analysis: level=" + std::to_string(sustainLevel) + 
               ", max variation=" + std::to_string(maxVariation));
    
    return sustainOk;
}

bool EnvelopeTest::analyzeReleasePhase(const std::vector<float>& envelopeGains, int releaseStart, int releaseBlocks) {
    if (envelopeGains.size() < static_cast<size_t>(releaseStart + releaseBlocks) || releaseBlocks <= 1) {
        return false;
    }
    
    // Release phase should generally show decreasing gain values
    int decreasingCount = 0;
    for (int i = releaseStart + 1; i < releaseStart + releaseBlocks && i < static_cast<int>(envelopeGains.size()); ++i) {
        if (envelopeGains[i] <= envelopeGains[i-1]) {
            decreasingCount++;
        }
    }
    
    // Allow some tolerance - at least 70% of transitions should be decreasing
    bool releaseOk = (static_cast<float>(decreasingCount) / static_cast<float>(releaseBlocks - 1)) >= 0.7f;
    
    logger_.log("EnvelopeTest/analyzeReleasePhase", "info", 
               "Release analysis: " + std::to_string(decreasingCount) + "/" + 
               std::to_string(releaseBlocks - 1) + " transitions decreasing");
    
    return releaseOk;
}