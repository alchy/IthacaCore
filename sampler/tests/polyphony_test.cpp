#include "polyphony_test.h"
#include "../core_logger.h"
#include "../voice_manager.h"
#include <vector>
#include <filesystem>
#include <algorithm>

PolyphonyTest::PolyphonyTest(Logger& logger, const TestConfig& config)
    : TestBase("PolyphonyTest", logger, config) {}

bool PolyphonyTest::shouldExportAudio() const { return config().exportAudio; }

std::vector<std::string> PolyphonyTest::getExportFileNames() const {
    return { "polyphony_chord.wav", "polyphony_progression.wav", "polyphony_stress_test.wav" };
}

TestResult PolyphonyTest::runTest(VoiceManager& voiceManager) {
    TestResult result;
    result.testName = getTestName();
    try {
        logProgress("Starting polyphony test with real VoiceManager API");
        
        // Reset all voices before testing
        voiceManager.resetAllVoices(logger_);
        
        bool testPassed = true;
        
        // Test 1: Basic chord (3 notes)
        if (!testBasicChord(voiceManager)) {
            testPassed = false;
        }
        
        // Test 2: Note progression
        if (!testNoteProgression(voiceManager)) {
            testPassed = false;
        }
        
        // Test 3: Stress test (many simultaneous notes)
        if (!testPolyphonyStress(voiceManager)) {
            testPassed = false;
        }
        
        // Test 4: Voice stealing/management
        if (!testVoiceManagement(voiceManager)) {
            testPassed = false;
        }

        result.passed = testPassed;
        result.details = "Polyphony test completed - basic chord, progression, stress test, and voice management";
        logTestResult("polyphony_test", result.passed, result.details);
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.errorMessage = e.what();
        logTestResult("exception", false, result.errorMessage);
    }
    return result;
}

bool PolyphonyTest::testBasicChord(VoiceManager& voiceManager) {
    logger_.log("PolyphonyTest/testBasicChord", "info", "Testing basic 3-note chord");
    
    std::vector<uint8_t> chordNotes = findValidNotesForPolyphony(voiceManager, 3, 60);
    if (chordNotes.size() < 3) {
        logger_.log("PolyphonyTest/testBasicChord", "error", 
                   "Not enough valid notes for chord test, found: " + std::to_string(chordNotes.size()));
        return false;
    }
    
    uint8_t testVelocity = config().defaultTestVelocity;
    bool chordTestPassed = true;
    
    // Start all chord notes
    for (uint8_t note : chordNotes) {
        voiceManager.setNoteState(note, true, testVelocity);
    }
    
    // Check active voice count
    int activeCount = voiceManager.getActiveVoicesCount();
    if (activeCount != static_cast<int>(chordNotes.size())) {
        chordTestPassed = false;
        logger_.log("PolyphonyTest/testBasicChord", "error", 
                   "Expected " + std::to_string(chordNotes.size()) + " active voices, got " + std::to_string(activeCount));
    }
    
    // Process audio and verify polyphonic output
    const int blockSize = config().exportBlockSize;
    float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
    float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
    float* exportBuffer = shouldExportAudio() ? createDummyAudioBuffer(blockSize, 2) : nullptr;
    
    memset(leftBuffer, 0, blockSize * sizeof(float));
    memset(rightBuffer, 0, blockSize * sizeof(float));
    
    if (voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize)) {
        AudioStats s = analyzeAudioBuffer(leftBuffer, blockSize, 1);
        
        logger_.log("PolyphonyTest/testBasicChord", "info", 
                   "Chord audio - Peak L: " + std::to_string(s.peakLevel) + 
                   ", Peak R: " + std::to_string(s.peakLevel) + 
                   ", Active voices: " + std::to_string(activeCount));
        
        if (shouldExportAudio() && exportBuffer) {
            for (int i = 0; i < blockSize; ++i) {
                exportBuffer[i * 2] = leftBuffer[i];
                exportBuffer[i * 2 + 1] = rightBuffer[i];
            }
            exportTestAudio("polyphony_chord.wav", exportBuffer, blockSize, 2, 44100);
            logger_.log("PolyphonyTest/testBasicChord", "info", "Exported: polyphony_chord.wav");
        }
        
        // Verify we have meaningful audio output
        if (s.peakLevel < 0.001f) {
            chordTestPassed = false;
            logger_.log("PolyphonyTest/testBasicChord", "error", 
                       "Polyphonic audio output too low: " + std::to_string(s.peakLevel));
        }
    } else {
        chordTestPassed = false;
        logger_.log("PolyphonyTest/testBasicChord", "error", "No polyphonic audio output");
    }
    
    // Stop all notes
    for (uint8_t note : chordNotes) {
        voiceManager.setNoteState(note, false, 0);
    }
    
    // Cleanup
    destroyDummyAudioBuffer(leftBuffer);
    destroyDummyAudioBuffer(rightBuffer);
    if (exportBuffer) destroyDummyAudioBuffer(exportBuffer);
    
    return chordTestPassed;
}

bool PolyphonyTest::testNoteProgression(VoiceManager& voiceManager) {
    logger_.log("PolyphonyTest/testNoteProgression", "info", "Testing note progression with overlapping voices");
    
    std::vector<uint8_t> progressionNotes = findValidNotesForPolyphony(voiceManager, 4, 64);
    if (progressionNotes.size() < 4) {
        logger_.log("PolyphonyTest/testNoteProgression", "error", 
                   "Not enough valid notes for progression test, found: " + std::to_string(progressionNotes.size()));
        return false;
    }
    
    bool progressionTestPassed = true;
    uint8_t testVelocity = config().defaultTestVelocity;
    
    const int blockSize = config().exportBlockSize;
    float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
    float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
    float* exportBuffer = shouldExportAudio() ? createDummyAudioBuffer(blockSize, 2) : nullptr;
    
    // Start notes progressively with overlap
    for (size_t i = 0; i < progressionNotes.size(); ++i) {
        uint8_t note = progressionNotes[i];
        voiceManager.setNoteState(note, true, testVelocity);
        
        // Process a block after each note addition
        memset(leftBuffer, 0, blockSize * sizeof(float));
        memset(rightBuffer, 0, blockSize * sizeof(float));
        
        if (voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize)) {
            AudioStats s = analyzeAudioBuffer(leftBuffer, blockSize, 1);
            int currentActive = voiceManager.getActiveVoicesCount();
            
            logger_.log("PolyphonyTest/testNoteProgression", "info", 
                       "Step " + std::to_string(i + 1) + " - Note " + std::to_string(note) + 
                       " added, Active voices: " + std::to_string(currentActive) + 
                       ", Peak: " + std::to_string(s.peakLevel));
            
            // Export progression on final step
            if (shouldExportAudio() && exportBuffer && i == progressionNotes.size() - 1) {
                for (int j = 0; j < blockSize; ++j) {
                    exportBuffer[j * 2] = leftBuffer[j];
                    exportBuffer[j * 2 + 1] = rightBuffer[j];
                }
                exportTestAudio("polyphony_progression.wav", exportBuffer, blockSize, 2, 44100);
                logger_.log("PolyphonyTest/testNoteProgression", "info", "Exported: polyphony_progression.wav");
            }
            
            // Verify expected number of active voices
            if (currentActive != static_cast<int>(i + 1)) {
                progressionTestPassed = false;
                logger_.log("PolyphonyTest/testNoteProgression", "error", 
                           "Expected " + std::to_string(i + 1) + " active voices, got " + std::to_string(currentActive));
            }
        }
        
        // Stop first note when we have 3 notes playing (test overlap)
        if (i == 2) {
            voiceManager.setNoteState(progressionNotes[0], false, 0);
            logger_.log("PolyphonyTest/testNoteProgression", "info", 
                       "Stopped first note to test overlap handling");
        }
    }
    
    // Stop remaining notes
    for (uint8_t note : progressionNotes) {
        voiceManager.setNoteState(note, false, 0);
    }
    
    // Cleanup
    destroyDummyAudioBuffer(leftBuffer);
    destroyDummyAudioBuffer(rightBuffer);
    if (exportBuffer) destroyDummyAudioBuffer(exportBuffer);
    
    return progressionTestPassed;
}

bool PolyphonyTest::testPolyphonyStress(VoiceManager& voiceManager) {
    logger_.log("PolyphonyTest/testPolyphonyStress", "info", "Testing polyphony stress with many simultaneous notes");
    
    // Try to activate many voices simultaneously
    std::vector<uint8_t> stressNotes = findValidNotesForPolyphony(voiceManager, 16, 48);
    if (stressNotes.size() < 8) {
        logger_.log("PolyphonyTest/testPolyphonyStress", "warn", 
                   "Limited stress test - only " + std::to_string(stressNotes.size()) + " valid notes available");
    }
    
    bool stressTestPassed = true;
    uint8_t testVelocity = config().defaultTestVelocity;
    
    // Start all available notes
    for (uint8_t note : stressNotes) {
        voiceManager.setNoteState(note, true, testVelocity);
    }
    
    int maxActiveCount = voiceManager.getActiveVoicesCount();
    logger_.log("PolyphonyTest/testPolyphonyStress", "info", 
               "Stress test - activated " + std::to_string(maxActiveCount) + " simultaneous voices");
    
    const int blockSize = config().exportBlockSize;
    float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
    float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
    float* exportBuffer = shouldExportAudio() ? createDummyAudioBuffer(blockSize, 2) : nullptr;
    
    // Process multiple blocks to test stability
    float maxPeakObserved = 0.0f;
    for (int block = 0; block < 5; ++block) {
        memset(leftBuffer, 0, blockSize * sizeof(float));
        memset(rightBuffer, 0, blockSize * sizeof(float));
        
        if (voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize)) {
            AudioStats s = analyzeAudioBuffer(leftBuffer, blockSize, 1);
            maxPeakObserved = std::max(maxPeakObserved, s.peakLevel);
            
            logger_.log("PolyphonyTest/testPolyphonyStress", "info", 
                       "Stress block " + std::to_string(block) + 
                       " - Peak: " + std::to_string(s.peakLevel) + 
                       ", Active: " + std::to_string(voiceManager.getActiveVoicesCount()));
            
            // Export first block for analysis
            if (shouldExportAudio() && exportBuffer && block == 0) {
                for (int i = 0; i < blockSize; ++i) {
                    exportBuffer[i * 2] = leftBuffer[i];
                    exportBuffer[i * 2 + 1] = rightBuffer[i];
                }
                exportTestAudio("polyphony_stress_test.wav", exportBuffer, blockSize, 2, 44100);
                logger_.log("PolyphonyTest/testPolyphonyStress", "info", "Exported: polyphony_stress_test.wav");
            }
        } else {
            logger_.log("PolyphonyTest/testPolyphonyStress", "warn", 
                       "No audio output during stress test block " + std::to_string(block));
        }
    }
    
    // Verify audio output under stress
    if (maxPeakObserved < 0.001f) {
        stressTestPassed = false;
        logger_.log("PolyphonyTest/testPolyphonyStress", "error", 
                   "Audio output too low during stress test: " + std::to_string(maxPeakObserved));
    }
    
    // Stop all notes
    for (uint8_t note : stressNotes) {
        voiceManager.setNoteState(note, false, 0);
    }
    
    // Verify cleanup
    int finalActiveCount = voiceManager.getActiveVoicesCount();
    if (finalActiveCount > 0) {
        logger_.log("PolyphonyTest/testPolyphonyStress", "warn", 
                   "Some voices still active after stress test cleanup: " + std::to_string(finalActiveCount));
    }
    
    // Cleanup
    destroyDummyAudioBuffer(leftBuffer);
    destroyDummyAudioBuffer(rightBuffer);
    if (exportBuffer) destroyDummyAudioBuffer(exportBuffer);
    
    return stressTestPassed;
}

bool PolyphonyTest::testVoiceManagement(VoiceManager& voiceManager) {
    logger_.log("PolyphonyTest/testVoiceManagement", "info", "Testing voice management and statistics");
    
    bool managementTestPassed = true;
    
    // Test voice statistics methods
    int maxVoices = voiceManager.getMaxVoices();
    if (maxVoices != 128) {
        logger_.log("PolyphonyTest/testVoiceManagement", "warn", 
                   "Expected 128 max voices, got " + std::to_string(maxVoices));
    }
    
    // Test voice state tracking
    std::vector<uint8_t> testNotes = findValidNotesForPolyphony(voiceManager, 5, 70);
    
    // Start some notes and check statistics
    for (size_t i = 0; i < std::min(size_t(3), testNotes.size()); ++i) {
        voiceManager.setNoteState(testNotes[i], true, config().defaultTestVelocity);
    }
    
    int activeCount = voiceManager.getActiveVoicesCount();
    int sustainingCount = voiceManager.getSustainingVoicesCount();
    int releasingCount = voiceManager.getReleasingVoicesCount();
    
    logger_.log("PolyphonyTest/testVoiceManagement", "info", 
               "Voice statistics - Active: " + std::to_string(activeCount) + 
               ", Sustaining: " + std::to_string(sustainingCount) + 
               ", Releasing: " + std::to_string(releasingCount));
    
    // Verify statistics make sense
    if (activeCount < sustainingCount + releasingCount) {
        managementTestPassed = false;
        logger_.log("PolyphonyTest/testVoiceManagement", "error", 
                   "Voice statistics inconsistent");
    }
    
    // Test stopAllVoices functionality
    voiceManager.stopAllVoices();
    
    // Process a few blocks to allow release phase
    const int blockSize = config().exportBlockSize;
    float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
    float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
    
    for (int i = 0; i < 5; ++i) {
        voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
    }
    
    // Check final statistics
    int finalActive = voiceManager.getActiveVoicesCount();
    logger_.log("PolyphonyTest/testVoiceManagement", "info", 
               "After stopAllVoices() - Active voices: " + std::to_string(finalActive));
    
    destroyDummyAudioBuffer(leftBuffer);
    destroyDummyAudioBuffer(rightBuffer);
    
    return managementTestPassed;
}