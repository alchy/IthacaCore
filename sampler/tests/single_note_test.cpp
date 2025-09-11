#include "single_note_test.h"
#include "../core_logger.h"
#include "../voice_manager.h"
#include <vector>
#include <filesystem>

SingleNoteTest::SingleNoteTest(Logger& logger, const TestConfig& config)
    : TestBase("SingleNoteTest", logger, config) {}

bool SingleNoteTest::shouldExportAudio() const { return config().exportAudio; }

std::vector<std::string> SingleNoteTest::getExportFileNames() const {
    return { "single_note_test.wav", "single_note_complete_cycle.wav" };
}

TestResult SingleNoteTest::runTest(VoiceManager& voiceManager) {
    TestResult result;
    result.testName = getTestName();
    try {
        logProgress("Starting single note test with real VoiceManager API");
        
        // Reset all voices before testing
        voiceManager.resetAllVoices(logger_);
        
        uint8_t testMidi = findValidTestMidiNote(voiceManager, 60);
        uint8_t testVelocity = config().defaultTestVelocity;
        bool testPassed = true;

        logger_.log("SingleNoteTest/runTest", "info", 
                    "Testing MIDI " + std::to_string(testMidi) + 
                    " with velocity " + std::to_string(testVelocity));

        // Test initial state
        int initialActiveCount = voiceManager.getActiveVoicesCount();
        if (initialActiveCount != 0) {
            logger_.log("SingleNoteTest/runTest", "warn", 
                       "Expected 0 initial active voices, got " + std::to_string(initialActiveCount));
        }

        // Start the note
        voiceManager.setNoteState(testMidi, true, testVelocity);
        
        // Check that voice became active
        int activeCountAfterNoteOn = voiceManager.getActiveVoicesCount();
        if (activeCountAfterNoteOn != 1) {
            testPassed = false;
            logger_.log("SingleNoteTest/runTest", "error", 
                        "Expected 1 active voice after note-on, got " + std::to_string(activeCountAfterNoteOn));
        }

        // Get reference to the voice for monitoring
        Voice& voice = voiceManager.getVoice(testMidi);
        
        // Verify voice state
        if (!voice.isActive()) {
            testPassed = false;
            logger_.log("SingleNoteTest/runTest", "error", 
                       "Voice should be active after note-on");
        }

        const int blockSize = config().exportBlockSize;
        const int numBlocks = 8;  // Enough blocks to test sustain phase
        float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
        float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
        float* exportBuffer = shouldExportAudio() ? createDummyAudioBuffer(blockSize, 2) : nullptr;

        bool hasAudioOutput = false;
        float maxPeakObserved = 0.0f;
        
        // Process blocks during sustain phase
        for (int block = 0; block < numBlocks; ++block) {
            memset(leftBuffer, 0, blockSize * sizeof(float));
            memset(rightBuffer, 0, blockSize * sizeof(float));
            
            bool hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
            AudioStats s = analyzeAudioBuffer(leftBuffer, blockSize, 1);
            
            if (hasAudio && s.peakLevel > 0.0001f) {
                hasAudioOutput = true;
                maxPeakObserved = std::max(maxPeakObserved, s.peakLevel);
                
                logger_.log("SingleNoteTest/runTest", "info", 
                            "Block " + std::to_string(block) + 
                            " - Peak L: " + std::to_string(s.peakLevel) + 
                            ", RMS L: " + std::to_string(s.rmsLevel) +
                            ", Voice state: " + std::to_string(static_cast<int>(voice.getState())) +
                            ", Envelope gain: " + std::to_string(voice.getCurrentEnvelopeGain()) +
                            ", Final gain: " + std::to_string(voice.getFinalGain()));

                // Export first block with audio for analysis
                if (shouldExportAudio() && exportBuffer && block == 0) {
                    for (int i = 0; i < blockSize; ++i) {
                        exportBuffer[i * 2] = leftBuffer[i];
                        exportBuffer[i * 2 + 1] = rightBuffer[i];
                    }
                    exportTestAudio("single_note_test.wav", exportBuffer, blockSize, 2, 44100);
                    logger_.log("SingleNoteTest/runTest", "info", "Exported: single_note_test.wav");
                }
            } else {
                logger_.log("SingleNoteTest/runTest", "info", 
                            "Block " + std::to_string(block) + " - Silent or very low level");
            }
        }

        // Test note-off and release phase
        logger_.log("SingleNoteTest/runTest", "info", "Sending note-off, monitoring release phase");
        voiceManager.setNoteState(testMidi, false, 0);
        
        // Process a few blocks during release
        const int releaseBlocks = 5;
        bool releasePhaseOk = true;
        
        for (int block = 0; block < releaseBlocks; ++block) {
            memset(leftBuffer, 0, blockSize * sizeof(float));
            memset(rightBuffer, 0, blockSize * sizeof(float));
            
            bool hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
            AudioStats s = analyzeAudioBuffer(leftBuffer, blockSize, 1);
            
            logger_.log("SingleNoteTest/runTest", "info", 
                       "Release block " + std::to_string(block) + 
                       " - Peak: " + std::to_string(s.peakLevel) + 
                       ", Voice active: " + (voice.isActive() ? "true" : "false") +
                       ", Voice state: " + std::to_string(static_cast<int>(voice.getState())));
            
            // Export complete cycle on last release block
            if (shouldExportAudio() && exportBuffer && block == releaseBlocks - 1) {
                // Create a complete cycle demonstration
                voiceManager.setNoteState(testMidi, true, testVelocity);
                memset(leftBuffer, 0, blockSize * sizeof(float));
                memset(rightBuffer, 0, blockSize * sizeof(float));
                voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
                
                for (int i = 0; i < blockSize; ++i) {
                    exportBuffer[i * 2] = leftBuffer[i];
                    exportBuffer[i * 2 + 1] = rightBuffer[i];
                }
                exportTestAudio("single_note_complete_cycle.wav", exportBuffer, blockSize, 2, 44100);
                logger_.log("SingleNoteTest/runTest", "info", "Exported: single_note_complete_cycle.wav");
                
                voiceManager.setNoteState(testMidi, false, 0);
            }
        }

        // Final validation
        if (!hasAudioOutput) {
            testPassed = false;
            logger_.log("SingleNoteTest/runTest", "error", 
                       "No audio output detected during single note test");
        }
        
        if (maxPeakObserved < 0.001f) {
            testPassed = false;
            logger_.log("SingleNoteTest/runTest", "error", 
                       "Audio output level too low: " + std::to_string(maxPeakObserved));
        }
        
        // Test velocity response
        if (!testVelocityResponse(voiceManager, testMidi)) {
            testPassed = false;
        }
        
        // Test state transitions
        if (!testVoiceStateTransitions(voiceManager, testMidi)) {
            testPassed = false;
        }

        // Cleanup
        destroyDummyAudioBuffer(leftBuffer);
        destroyDummyAudioBuffer(rightBuffer);
        if (exportBuffer) destroyDummyAudioBuffer(exportBuffer);

        result.passed = testPassed;
        result.details = "Single note test completed - max peak: " + std::to_string(maxPeakObserved) +
                        ", audio output: " + (hasAudioOutput ? "yes" : "no");
        logTestResult("single_note_test", result.passed, result.details);
        
    } catch (const std::exception& e) {
        result.passed = false;
        result.errorMessage = e.what();
        logTestResult("exception", false, result.errorMessage);
    }
    return result;
}

bool SingleNoteTest::testVelocityResponse(VoiceManager& voiceManager, uint8_t testMidi) {
    logger_.log("SingleNoteTest/testVelocityResponse", "info", "Testing velocity response");
    
    std::vector<uint8_t> testVelocities = {32, 64, 127};
    std::vector<float> peakLevels;
    
    const int blockSize = config().exportBlockSize;
    float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
    float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
    
    bool velocityResponseOk = true;
    
    for (uint8_t velocity : testVelocities) {
        voiceManager.setNoteState(testMidi, true, velocity);
        
        memset(leftBuffer, 0, blockSize * sizeof(float));
        memset(rightBuffer, 0, blockSize * sizeof(float));
        
        if (voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize)) {
            AudioStats s = analyzeAudioBuffer(leftBuffer, blockSize, 1);
            peakLevels.push_back(s.peakLevel);
            
            logger_.log("SingleNoteTest/testVelocityResponse", "info", 
                       "Velocity " + std::to_string(velocity) + " → Peak: " + std::to_string(s.peakLevel));
        } else {
            peakLevels.push_back(0.0f);
            velocityResponseOk = false;
            logger_.log("SingleNoteTest/testVelocityResponse", "error", 
                       "No audio for velocity " + std::to_string(velocity));
        }
        
        voiceManager.setNoteState(testMidi, false, 0);
        
        // Process a few blocks for cleanup
        for (int i = 0; i < 2; ++i) {
            voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
        }
    }
    
    // Check that higher velocities generally produce higher levels
    if (peakLevels.size() >= 2) {
        for (size_t i = 1; i < peakLevels.size(); ++i) {
            if (peakLevels[i] < peakLevels[i-1] * 0.8f) {
                // Allow some tolerance but expect general increase
                logger_.log("SingleNoteTest/testVelocityResponse", "warn", 
                           "Velocity response may be non-monotonic");
            }
        }
    }
    
    destroyDummyAudioBuffer(leftBuffer);
    destroyDummyAudioBuffer(rightBuffer);
    
    return velocityResponseOk;
}

bool SingleNoteTest::testVoiceStateTransitions(VoiceManager& voiceManager, uint8_t testMidi) {
    logger_.log("SingleNoteTest/testVoiceStateTransitions", "info", "Testing voice state transitions");
    
    Voice& voice = voiceManager.getVoice(testMidi);
    bool stateTransitionsOk = true;
    
    // Initial state should be Idle
    if (voice.getState() != VoiceState::Idle) {
        stateTransitionsOk = false;
        logger_.log("SingleNoteTest/testVoiceStateTransitions", "error", 
                   "Initial state should be Idle, got: " + std::to_string(static_cast<int>(voice.getState())));
    }
    
    // Start note - should transition to Attacking or Sustaining
    voiceManager.setNoteState(testMidi, true, 100);
    VoiceState stateAfterNoteOn = voice.getState();
    if (stateAfterNoteOn == VoiceState::Idle) {
        stateTransitionsOk = false;
        logger_.log("SingleNoteTest/testVoiceStateTransitions", "error", 
                   "State should not be Idle after note-on");
    }
    
    logger_.log("SingleNoteTest/testVoiceStateTransitions", "info", 
               "State after note-on: " + std::to_string(static_cast<int>(stateAfterNoteOn)));
    
    // Stop note - should transition to Releasing
    voiceManager.setNoteState(testMidi, false, 0);
    VoiceState stateAfterNoteOff = voice.getState();
    
    logger_.log("SingleNoteTest/testVoiceStateTransitions", "info", 
               "State after note-off: " + std::to_string(static_cast<int>(stateAfterNoteOff)));
    
    // Process some blocks to allow state changes
    const int blockSize = config().exportBlockSize;
    float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
    float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
    
    for (int i = 0; i < 5; ++i) {
        voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
        VoiceState currentState = voice.getState();
        logger_.log("SingleNoteTest/testVoiceStateTransitions", "info", 
                   "State after processing block " + std::to_string(i) + ": " + 
                   std::to_string(static_cast<int>(currentState)));
    }
    
    destroyDummyAudioBuffer(leftBuffer);
    destroyDummyAudioBuffer(rightBuffer);
    
    return stateTransitionsOk;
}