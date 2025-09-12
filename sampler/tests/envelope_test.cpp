#include "test_common.h"
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
    return { "envelope_attack.wav", "envelope_sustain.wav", "envelope_release.wav", "envelope_full_cycle.wav" };  // Nové: Přidán sustain soubor
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
        
        // Nastavení délek fází v sekundách (doporučené hodnoty)
        const double attackDurationSec = 4.0;  // Doba attack fáze v sekundách
        const double sustainDurationSec = 4.0; // Doba sustain fáze v sekundách
        const double releaseDurationSec = 4.0; // Doba release fáze v sekundách
        const int blockSize = config().exportBlockSize;
        const int sampleRate = voiceManager.getCurrentSampleRate();  // Získej aktuální sample rate
        
        // Výpočet počtu bloků pro každou fázi
        const int attackBlocks = static_cast<int>(std::ceil(attackDurationSec * sampleRate / blockSize));
        const int sustainBlocks = static_cast<int>(std::ceil(sustainDurationSec * sampleRate / blockSize));
        const int releaseBlocks = static_cast<int>(std::ceil(releaseDurationSec * sampleRate / blockSize));
        const int totalBlocks = attackBlocks + sustainBlocks + releaseBlocks;
        
        // Logování vypočtených délek pro kontrolu
        logger_.log("EnvelopeTest/runTest", "info",
                    "Calculated phases: Attack " + std::to_string(attackBlocks) + " blocks (~" + std::to_string(attackDurationSec) + "s), "
                    "Sustain " + std::to_string(sustainBlocks) + " blocks (~" + std::to_string(sustainDurationSec) + "s), "
                    "Release " + std::to_string(releaseBlocks) + " blocks (~" + std::to_string(releaseDurationSec) + "s)");
        
        float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
        float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
        
        // Kumulativní vektory pro export (stereo interleaved)
        std::vector<float> attackExportBuffer;
        std::vector<float> sustainExportBuffer;
        std::vector<float> releaseExportBuffer;
        std::vector<float> fullCycleExportBuffer;
        
        if (shouldExportAudio()) {
            attackExportBuffer.reserve(attackBlocks * blockSize * 2);
            sustainExportBuffer.reserve(sustainBlocks * blockSize * 2);
            releaseExportBuffer.reserve(releaseBlocks * blockSize * 2);
            fullCycleExportBuffer.reserve(totalBlocks * blockSize * 2);
        }
        
        std::vector<float> envelopeGains;
        std::vector<float> peakLevels;
        std::vector<int> voiceStates;
        
        // Start note for attack phase
        voiceManager.setNoteState(testMidi, true, testVelocity);
        
        // Process all blocks and monitor envelope behavior
        for (int block = 0; block < totalBlocks; ++block) {
            memset(leftBuffer, 0, blockSize * sizeof(float));
            memset(rightBuffer, 0, blockSize * sizeof(float));
            
            bool hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
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
                            ", State: " + std::to_string(static_cast<int>(voice.getState())));
            } else if (block < attackBlocks + sustainBlocks) {
                testPassed = false;
                logger_.log("EnvelopeTest/runTest", "error",
                            "No audio during active phase at block " + std::to_string(block));
            }
            
            // Kumulativní přidání bloku do export vektorů
            if (shouldExportAudio()) {
                for (int i = 0; i < blockSize; ++i) {
                    if (block < attackBlocks) {
                        attackExportBuffer.push_back(leftBuffer[i]);
                        attackExportBuffer.push_back(rightBuffer[i]);
                    } else if (block < attackBlocks + sustainBlocks) {
                        sustainExportBuffer.push_back(leftBuffer[i]);
                        sustainExportBuffer.push_back(rightBuffer[i]);
                    } else {
                        releaseExportBuffer.push_back(leftBuffer[i]);
                        releaseExportBuffer.push_back(rightBuffer[i]);
                    }
                    fullCycleExportBuffer.push_back(leftBuffer[i]);
                    fullCycleExportBuffer.push_back(rightBuffer[i]);
                }
            }
            
            // Transition to release phase na konci sustain
            if (block == attackBlocks + sustainBlocks - 1) {
                voiceManager.setNoteState(testMidi, false, 0);
                logger_.log("EnvelopeTest/runTest", "info",
                            "Note-off sent - monitoring release envelope");
            }
        }
        
        // Export kumulativních vektorů na konci testu
        if (shouldExportAudio()) {
            exportTestAudio("envelope_attack.wav", attackExportBuffer.data(), static_cast<int>(attackExportBuffer.size() / 2), 2, sampleRate);
            exportTestAudio("envelope_sustain.wav", sustainExportBuffer.data(), static_cast<int>(sustainExportBuffer.size() / 2), 2, sampleRate);
            exportTestAudio("envelope_release.wav", releaseExportBuffer.data(), static_cast<int>(releaseExportBuffer.size() / 2), 2, sampleRate);
            exportTestAudio("envelope_full_cycle.wav", fullCycleExportBuffer.data(), static_cast<int>(fullCycleExportBuffer.size() / 2), 2, sampleRate);
            logger_.log("EnvelopeTest/runTest", "info", "Exported all phase audio files with full duration");
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