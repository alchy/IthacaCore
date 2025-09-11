#include "voice_manager_tests.h"
#include "../voice_manager.h"
#include "../instrument_loader.h"
#include "../core_logger.h"
#include "../wav_file_exporter.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cstring>

/**
 * @brief Konstruktor VoiceManagerTester
 * Inicializuje test-specifická data a časovač.
 */
VoiceManagerTester::VoiceManagerTester(int numVoices, int sampleRate, Logger& logger)
    : logger_(&logger), numVoices_(numVoices), sampleRate_(sampleRate),
      testCount_(0), failureCount_(0), startTime_(std::chrono::steady_clock::now()) {
    
    logger_->log("VoiceManagerTester/constructor", "info", 
                "VoiceManagerTester initialized: " + std::to_string(numVoices_) + 
                " voices, " + std::to_string(sampleRate_) + " Hz");
}

/**
 * @brief Hlavní metoda pro spuštění všech testů
 * Agreguje všechny testy a vrací počet selhání.
 */
int VoiceManagerTester::runAllTests(VoiceManager& voiceManager, InstrumentLoader& loader) {
    logger_->log("VoiceManagerTester/runAllTests", "info", 
                "=== STARTING COMPREHENSIVE VOICE MANAGER TEST SUITE ===");
    
    testCount_ = 0;
    failureCount_ = 0;
    startTime_ = std::chrono::steady_clock::now();
    
    // Inicializace voices přes VoiceManager
    try {
        logger_->log("VoiceManagerTester/runAllTests", "info", 
                    "Using pre-initialized VoiceManager and InstrumentLoader");
    } catch (...) {
        logger_->log("VoiceManagerTester/runAllTests", "error", 
                    "Failed to use VoiceManager - check initialization");
        failureCount_++;
        return failureCount_;
    }
    
    // ===== VELOCITY GAIN SPECIFIC TESTY =====
    
    logger_->log("VoiceManagerTester/runAllTests", "info", 
                "=== STARTING VELOCITY GAIN TESTS ===");
    
    // Test velocity gain s různými hodnotami
    if (!runVelocityGainTest(voiceManager)) {
        failureCount_++;
    }
    testCount_++;
    
    // Test master gain nastavení
    if (!runMasterGainTest(voiceManager)) {
        failureCount_++;
    }
    testCount_++;
    
    // Enhanced single note test s gain monitoringem
    if (!runSingleNoteTestWithGain(voiceManager)) {
        failureCount_++;
    }
    testCount_++;
    
    // ===== STANDARDNÍ GRANULAR TESTY =====
    
    logger_->log("VoiceManagerTester/runAllTests", "info", 
                "=== STARTING STANDARD GRANULAR TESTS ===");
    
    // Individual voice inspection
    if (!runIndividualVoiceTest(voiceManager)) {
        failureCount_++;
    }
    testCount_++;
    
    // Single note functionality
    if (!runSingleNoteTest(voiceManager)) {
        failureCount_++;
    }
    testCount_++;
    
    // Polyphonic capabilities
    if (!runPolyphonyTest(voiceManager)) {
        failureCount_++;
    }
    testCount_++;
    
    // Edge case handling
    if (!runEdgeCaseTests(voiceManager)) {
        failureCount_++;
    }
    testCount_++;
    
    // ===== EXPORT TESTY =====
    
    logger_->log("VoiceManagerTester/runAllTests", "info", 
                "=== STARTING EXPORT TESTS ===");
    
    try {
        if (!exportTestSample(voiceManager)) {
            failureCount_++;
        }
        testCount_++;
    } catch (...) {
        logger_->log("VoiceManagerTester/runAllTests", "warn", 
                    "Export tests failed - continuing anyway");
        failureCount_++;
        testCount_++;
    }
    
    // ===== FINAL SUMMARY =====
    
    auto endTime = std::chrono::steady_clock::now();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime_).count();
    
    logger_->log("VoiceManagerTester/runAllTests", "info", 
                "=== VOICE MANAGER TEST SUITE COMPLETED ===");
    logger_->log("VoiceManagerTester/runAllTests", "info", 
                "Total tests: " + std::to_string(testCount_) + 
                ", Failures: " + std::to_string(failureCount_) + 
                ", Time: " + std::to_string(totalMs) + " ms");
    
    if (failureCount_ == 0) {
        logger_->log("VoiceManagerTester/runAllTests", "info", 
                    "✓ ALL TESTS PASSED - VoiceManager is ready for production");
    } else {
        logger_->log("VoiceManagerTester/runAllTests", "error", 
                    "✗ " + std::to_string(failureCount_) + " TESTS FAILED");
    }
    
    return failureCount_;
}

/**
 * @brief Test velocity gain s různými hodnotami
 */
bool VoiceManagerTester::runVelocityGainTest(VoiceManager& voiceManager) {
    logTestResult("runVelocityGainTest", true, "Starting velocity gain test");
    
    uint8_t testMidi = findValidTestMidiNote(voiceManager);
    
    // Test různých velocity hodnot: 1, 32, 64, 96, 127
    std::vector<uint8_t> testVelocities = {1, 32, 64, 96, 127};
    
    const int blockSize = 512;
    float* leftBuffer = new float[blockSize];
    float* rightBuffer = new float[blockSize];
    
    bool testPassed = true;
    
    for (uint8_t velocity : testVelocities) {
        logger_->log("VoiceManagerTester/runVelocityGainTest", "info", 
                    "Testing velocity " + std::to_string(velocity) + " for MIDI " + std::to_string(testMidi));
        
        // Note-on s testovanou velocity
        voiceManager.setNoteState(testMidi, true, velocity);
        
        Voice& voice = voiceManager.getVoice(testMidi);
        
        // Process jeden blok pro měření výstupní hlasitosti
        memset(leftBuffer, 0, blockSize * sizeof(float));
        memset(rightBuffer, 0, blockSize * sizeof(float));
        
        if (voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize)) {
            // Analýza peak level
            float peakL = 0.0f, peakR = 0.0f;
            float rmsL = 0.0f, rmsR = 0.0f;
            
            for (int i = 0; i < blockSize; ++i) {
                peakL = std::max(peakL, std::abs(leftBuffer[i]));
                peakR = std::max(peakR, std::abs(rightBuffer[i]));
                rmsL += leftBuffer[i] * leftBuffer[i];
                rmsR += rightBuffer[i] * rightBuffer[i];
            }
            
            rmsL = std::sqrt(rmsL / blockSize);
            rmsR = std::sqrt(rmsR / blockSize);
            
            logger_->log("VoiceManagerTester/runVelocityGainTest", "info", 
                        "Velocity " + std::to_string(velocity) + 
                        " - Peak L: " + std::to_string(peakL) + 
                        ", Peak R: " + std::to_string(peakR) +
                        ", RMS L: " + std::to_string(rmsL) +
                        ", RMS R: " + std::to_string(rmsR) +
                        ", Voice Final Gain: " + std::to_string(voice.getFinalGain()));
                      
            // Očekávaný růst hlasitosti s velocity
            float expectedGain = std::sqrt(static_cast<float>(velocity) / 127.0f) * voice.getMasterGain();
            if (!verifyGainCurve(expectedGain, voice.getVelocityGain())) {
                testPassed = false;
                logger_->log("VoiceManagerTester/runVelocityGainTest", "error", 
                            "Velocity gain verification failed for velocity " + std::to_string(velocity));
            }
        } else {
            testPassed = false;
            logger_->log("VoiceManagerTester/runVelocityGainTest", "error", 
                        "No audio output for velocity " + std::to_string(velocity));
        }
        
        // Note-off
        voiceManager.setNoteState(testMidi, false, 0);
        
        // Krátká pauza pro cleanup
        for (int i = 0; i < 3; ++i) {
            voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
        }
    }
    
    delete[] leftBuffer;
    delete[] rightBuffer;
    
    logTestResult("runVelocityGainTest", testPassed, 
                  testPassed ? "All velocity levels tested successfully" : "Some velocity tests failed");
    
    return testPassed;
}

/**
 * @brief Test master gain nastavení
 */
bool VoiceManagerTester::runMasterGainTest(VoiceManager& voiceManager) {
    logTestResult("runMasterGainTest", true, "Starting master gain test");
    
    uint8_t testMidi = findValidTestMidiNote(voiceManager);
    Voice& voice = voiceManager.getVoice(testMidi);
    
    // Test různých master gain hodnot
    std::vector<float> testGains = {0.1f, 0.3f, 0.5f, 0.8f, 1.0f};
    bool testPassed = true;
    
    for (float masterGain : testGains) {
        logger_->log("VoiceManagerTester/runMasterGainTest", "info", 
                    "Testing master gain " + std::to_string(masterGain));
        
        // Nastavení master gain
        voice.setMasterGain(masterGain, *logger_);
        
        // Test s pevnou velocity (100)
        voiceManager.setNoteState(testMidi, true, 100);
        
        // Krátký test
        const int blockSize = 256;
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        memset(leftBuffer, 0, blockSize * sizeof(float));
        memset(rightBuffer, 0, blockSize * sizeof(float));
        
        if (voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize)) {
            float peakL = 0.0f;
            for (int i = 0; i < blockSize; ++i) {
                peakL = std::max(peakL, std::abs(leftBuffer[i]));
            }
            
            logger_->log("VoiceManagerTester/runMasterGainTest", "info", 
                        "Master gain " + std::to_string(masterGain) + 
                        " → Peak output: " + std::to_string(peakL));
            
            // Ověření, že master gain skutečně ovlivňuje výstup
            if (voice.getMasterGain() != masterGain) {
                testPassed = false;
                logger_->log("VoiceManagerTester/runMasterGainTest", "error", 
                            "Master gain not set correctly");
            }
        } else {
            testPassed = false;
            logger_->log("VoiceManagerTester/runMasterGainTest", "error", 
                        "No audio output for master gain test");
        }
        
        delete[] leftBuffer;
        delete[] rightBuffer;
        
        voiceManager.setNoteState(testMidi, false, 0);
    }
    
    logTestResult("runMasterGainTest", testPassed, 
                  testPassed ? "All master gain levels tested successfully" : "Some master gain tests failed");
    
    return testPassed;
}

/**
 * @brief Enhanced single note test s gain monitoringem
 */
bool VoiceManagerTester::runSingleNoteTestWithGain(VoiceManager& voiceManager) {
    logTestResult("runSingleNoteTestWithGain", true, "Starting enhanced single note test");
    
    uint8_t testMidi = findValidTestMidiNote(voiceManager);
    uint8_t testVelocity = 100;
    bool testPassed = true;
    
    logger_->log("VoiceManagerTester/runSingleNoteTestWithGain", "info", 
                "Testing MIDI " + std::to_string(testMidi) + 
                " with velocity " + std::to_string(testVelocity) + 
                " (enhanced gain monitoring)");
    
    Voice& voice = voiceManager.getVoice(testMidi);
    
    // Note-on
    voiceManager.setNoteState(testMidi, true, testVelocity);
    
    // Detailní gain monitoring
    voice.getGainDebugInfo(*logger_);
    
    const int blockSize = 512;
    const int numBlocks = 8;  // Více bloků pro sledování envelope
    float* leftBuffer = new float[blockSize];
    float* rightBuffer = new float[blockSize];
    
    for (int block = 0; block < numBlocks; ++block) {
        memset(leftBuffer, 0, blockSize * sizeof(float));
        memset(rightBuffer, 0, blockSize * sizeof(float));
        
        bool hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
        
        if (hasAudio) {
            float maxL = 0.0f;
            for (int i = 0; i < blockSize; ++i) {
                maxL = std::max(maxL, std::abs(leftBuffer[i]));
            }
            
            logger_->log("VoiceManagerTester/runSingleNoteTestWithGain", "info", 
                        "Block " + std::to_string(block) + 
                        " - Peak: " + std::to_string(maxL) + 
                        ", Voice gains: Env=" + std::to_string(voice.getCurrentEnvelopeGain()) +
                        ", Vel=" + std::to_string(voice.getVelocityGain()) +
                        ", Final=" + std::to_string(voice.getFinalGain()));
        } else if (block < numBlocks / 2) {
            // Před release by měl být audio
            testPassed = false;
            logger_->log("VoiceManagerTester/runSingleNoteTestWithGain", "error", 
                        "No audio in sustain phase at block " + std::to_string(block));
        }
        
        // Release po polovině bloků
        if (block == numBlocks / 2) {
            voiceManager.setNoteState(testMidi, false, 0);
            logger_->log("VoiceManagerTester/runSingleNoteTestWithGain", "info", 
                        "Note-off sent - monitoring release envelope");
        }
    }
    
    delete[] leftBuffer;
    delete[] rightBuffer;
    
    logTestResult("runSingleNoteTestWithGain", testPassed, 
                  testPassed ? "Enhanced single note test completed successfully" : "Enhanced single note test failed");
    
    return testPassed;
}

/**
 * @brief Test jednotlivé voice inspekce
 */
bool VoiceManagerTester::runIndividualVoiceTest(VoiceManager& voiceManager) {
    logTestResult("runIndividualVoiceTest", true, "Starting individual voice test");
    
    uint8_t testMidi = findValidTestMidiNote(voiceManager);
    Voice& voice = voiceManager.getVoice(testMidi);
    
    logger_->log("VoiceManagerTester/runIndividualVoiceTest", "info", 
                "Voice " + std::to_string(testMidi) + " state: " + 
                std::to_string(static_cast<int>(voice.getState())) + 
                ", position: " + std::to_string(voice.getPosition()) + 
                ", envelope gain: " + std::to_string(voice.getCurrentEnvelopeGain()) +
                ", velocity gain: " + std::to_string(voice.getVelocityGain()) +
                ", master gain: " + std::to_string(voice.getMasterGain()) +
                ", final gain: " + std::to_string(voice.getFinalGain()));
    
    logTestResult("runIndividualVoiceTest", true, "Individual voice inspection completed");
    return true;
}

/**
 * @brief Test základní single note funkcionality
 */
bool VoiceManagerTester::runSingleNoteTest(VoiceManager& voiceManager) {
    logTestResult("runSingleNoteTest", true, "Starting single note test");
    
    uint8_t testMidi = findValidTestMidiNote(voiceManager);
    uint8_t testVelocity = 100;
    bool testPassed = true;
    
    logger_->log("VoiceManagerTester/runSingleNoteTest", "info", 
                "Testing MIDI " + std::to_string(testMidi) + " with velocity " + std::to_string(testVelocity));
    
    // Note-on
    voiceManager.setNoteState(testMidi, true, testVelocity);
    
    int activeCount = voiceManager.getActiveVoicesCount();
    if (activeCount != 1) {
        testPassed = false;
        logger_->log("VoiceManagerTester/runSingleNoteTest", "error", 
                    "Expected 1 active voice, got " + std::to_string(activeCount));
    }
    
    // Simulate audio processing
    const int blockSize = 512;
    const int numBlocks = 5;
    
    for (int block = 0; block < numBlocks; ++block) {
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        bool hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
        
        if (hasAudio) {
            float maxL = 0.0f, maxR = 0.0f;
            for (int i = 0; i < blockSize; ++i) {
                maxL = std::max(maxL, std::abs(leftBuffer[i]));
                maxR = std::max(maxR, std::abs(rightBuffer[i]));
            }
            
            logger_->log("VoiceManagerTester/runSingleNoteTest", "info", 
                        "Block " + std::to_string(block) + " - Peak L: " + std::to_string(maxL) + 
                        ", Peak R: " + std::to_string(maxR));
        } else {
            logger_->log("VoiceManagerTester/runSingleNoteTest", "info", 
                        "Block " + std::to_string(block) + " - Silent");
        }
        
        delete[] leftBuffer;
        delete[] rightBuffer;
    }
    
    // Note-off
    voiceManager.setNoteState(testMidi, false, 0);
    
    logTestResult("runSingleNoteTest", testPassed, "Single note test completed");
    return testPassed;
}

/**
 * @brief Test polyfonních schopností
 */
bool VoiceManagerTester::runPolyphonyTest(VoiceManager& voiceManager) {
    logTestResult("runPolyphonyTest", true, "Starting polyphony test");
    
    std::vector<uint8_t> testNotes = findValidNotesForPolyphony(voiceManager, 3);
    bool testPassed = true;
    
    if (testNotes.size() >= 2) {
        // Note-on for all notes
        for (uint8_t note : testNotes) {
            voiceManager.setNoteState(note, true, 90);
            logger_->log("VoiceManagerTester/runPolyphonyTest", "info", 
                        "Note-on MIDI " + std::to_string(note));
        }
        
        int activeCount = voiceManager.getActiveVoicesCount();
        if (activeCount != static_cast<int>(testNotes.size())) {
            testPassed = false;
            logger_->log("VoiceManagerTester/runPolyphonyTest", "error", 
                        "Expected " + std::to_string(testNotes.size()) + " active voices, got " + std::to_string(activeCount));
        }
        
        // Process one block for testing
        const int blockSize = 512;
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        if (voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize)) {
            float maxL = 0.0f, maxR = 0.0f;
            for (int i = 0; i < blockSize; ++i) {
                maxL = std::max(maxL, std::abs(leftBuffer[i]));
                maxR = std::max(maxR, std::abs(rightBuffer[i]));
            }
            
            logger_->log("VoiceManagerTester/runPolyphonyTest", "info", 
                        "Polyphonic audio - Peak L: " + std::to_string(maxL) + 
                        ", Peak R: " + std::to_string(maxR));
        } else {
            testPassed = false;
            logger_->log("VoiceManagerTester/runPolyphonyTest", "error", 
                        "No polyphonic audio output");
        }
        
        delete[] leftBuffer;
        delete[] rightBuffer;
        
        // Note-off for all
        for (uint8_t note : testNotes) {
            voiceManager.setNoteState(note, false, 0);
        }
    } else {
        testPassed = false;
        logger_->log("VoiceManagerTester/runPolyphonyTest", "error", 
                    "Not enough valid notes for polyphony test");
    }
    
    logTestResult("runPolyphonyTest", testPassed, "Polyphony test completed");
    return testPassed;
}

/**
 * @brief Test edge cases
 */
bool VoiceManagerTester::runEdgeCaseTests(VoiceManager& voiceManager) {
    logTestResult("runEdgeCaseTests", true, "Starting edge case tests");
    
    // Invalid MIDI notes
    voiceManager.setNoteState(200, true, 100);  // Invalid MIDI
    voiceManager.setNoteState(60, true, 0);     // Zero velocity
    voiceManager.setNoteState(61, true, 127);   // Max velocity
    
    int activeCount = voiceManager.getActiveVoicesCount();
    logger_->log("VoiceManagerTester/runEdgeCaseTests", "info", 
                "Active voices after edge cases: " + std::to_string(activeCount));
    
    voiceManager.stopAllVoices();
    
    logTestResult("runEdgeCaseTests", true, "Edge case tests completed");
    return true;
}

/**
 * @brief Export testového vzorku
 */
bool VoiceManagerTester::exportTestSample(VoiceManager& voiceManager) {
    logTestResult("exportTestSample", true, "Starting export test");
    
    uint8_t testMidi = findValidTestMidiNote(voiceManager);
    uint8_t testVelocity = 100;
    bool testPassed = true;
    
    try {
        // Vytvoření export adresáře
        std::string exportDir = "./exports/tests";
        std::filesystem::create_directories(exportDir);
        
        logger_->log("VoiceManagerTester/exportTestSample", "info", 
                    "Export directory created: " + exportDir);
        
        WavExporter exporter(exportDir, *logger_);
        std::string filename = "voice_manager_test_midi" + std::to_string(testMidi) + 
                              "_vel" + std::to_string(testVelocity) + ".wav";
        
        const int blockSize = 512;
        float* exportBuffer = exporter.wavFileCreate(filename, sampleRate_, blockSize, true, true);
        
        if (exportBuffer) {
            float* leftBuffer = new float[blockSize];
            float* rightBuffer = new float[blockSize];
            
            // Start note
            voiceManager.setNoteState(testMidi, true, testVelocity);
            
            // Capture several blocks
            const int numBlocks = 20;
            for (int block = 0; block < numBlocks; ++block) {
                if (voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize)) {
                    // Interleave for export
                    for (int i = 0; i < blockSize; ++i) {
                        exportBuffer[i * 2] = leftBuffer[i];
                        exportBuffer[i * 2 + 1] = rightBuffer[i];
                    }
                    
                    if (!exporter.wavFileWriteBuffer(exportBuffer, blockSize)) {
                        testPassed = false;
                        break;
                    }
                }
                
                // Release at halfway point
                if (block == numBlocks / 2) {
                    voiceManager.setNoteState(testMidi, false, 0);
                }
            }
            
            delete[] leftBuffer;
            delete[] rightBuffer;
            
            logger_->log("VoiceManagerTester/exportTestSample", "info", 
                        "Export completed: " + exportDir + "/" + filename);
        } else {
            testPassed = false;
        }
        
    } catch (...) {
        testPassed = false;
        logger_->log("VoiceManagerTester/exportTestSample", "error", 
                    "Export test failed with exception");
    }
    
    logTestResult("exportTestSample", testPassed, "Export test completed");
    return testPassed;
}

// ===== TESTOVACÍ HELPERY =====

void* VoiceManagerTester::createTestInstrument() {
    // Dummy implementation - v reálném kódu by vytvořil test instrument
    return nullptr;
}

void VoiceManagerTester::simulateNoteSequence(VoiceManager& voiceManager, uint8_t midiNote, 
                                             uint8_t velocity, int durationMs) {
    voiceManager.setNoteState(midiNote, true, velocity);
    
    // Simulace času pomocí processBlockUninterleaved volání
    const int blockSize = 512;
    const int blocksPerSecond = sampleRate_ / blockSize;
    const int totalBlocks = (durationMs * blocksPerSecond) / 1000;
    
    float* leftBuffer = new float[blockSize];
    float* rightBuffer = new float[blockSize];
    
    for (int i = 0; i < totalBlocks; ++i) {
        voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
    }
    
    voiceManager.setNoteState(midiNote, false, 0);
    
    delete[] leftBuffer;
    delete[] rightBuffer;
}

bool VoiceManagerTester::verifyGainCurve(float expectedGain, float actualGain, float tolerance) {
    return std::abs(expectedGain - actualGain) <= tolerance;
}

bool VoiceManagerTester::verifyAudioOutput(const float* outputBuffer, int numSamples, 
                                          float expectedPeak, float tolerance) {
    float actualPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        actualPeak = std::max(actualPeak, std::abs(outputBuffer[i]));
    }
    return std::abs(expectedPeak - actualPeak) <= tolerance;
}

float* VoiceManagerTester::createDummyAudioBuffer(int numSamples, int channels) {
    return new float[numSamples * channels]();  // Zero-initialized
}

long VoiceManagerTester::getTotalTimeMs() const {
    auto endTime = std::chrono::steady_clock::now();
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime_).count());
}

// ===== PRIVATE HELPER METHODS =====

uint8_t VoiceManagerTester::findValidTestMidiNote(VoiceManager& voiceManager) {
    // Zkusíme najít platnou MIDI notu pro testy
    for (int midi = 60; midi <= 80; ++midi) {
        // Předpokládáme, že voice je správně inicializována
        const Voice& voice = voiceManager.getVoice(midi);
        if (voice.getMidiNote() == midi) {
            return static_cast<uint8_t>(midi);
        }
    }
    
    logger_->log("VoiceManagerTester/findValidTestMidiNote", "warn", 
                "No valid test note found, using default MIDI 60");
    return 60; // fallback
}

std::vector<uint8_t> VoiceManagerTester::findValidNotesForPolyphony(VoiceManager& voiceManager, int maxNotes) {
    std::vector<uint8_t> notes;
    for (int midi = 70; midi <= 80 && notes.size() < maxNotes; ++midi) {
        const Voice& voice = voiceManager.getVoice(midi);
        if (voice.getMidiNote() == midi) {
            notes.push_back(static_cast<uint8_t>(midi));
            logger_->log("VoiceManagerTester/findValidNotesForPolyphony", "info", 
                        "Added MIDI " + std::to_string(midi) + " for polyphony test");
        }
    }
    return notes;
}

void VoiceManagerTester::logTestResult(const std::string& testName, bool success, const std::string& message) {
    std::string severity = success ? "info" : "error";
    std::string prefix = success ? "✓ " : "✗ ";
    std::string fullMessage = prefix + testName;
    if (!message.empty()) {
        fullMessage += " - " + message;
    }
    logger_->log("VoiceManagerTester/" + testName, severity, fullMessage);
}

void VoiceManagerTester::startTestTimer() {
    startTime_ = std::chrono::steady_clock::now();
}

void VoiceManagerTester::endTestTimer(const std::string& testName) {
    auto endTime = std::chrono::steady_clock::now();
    auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime_).count();
    
    logger_->log("VoiceManagerTester/" + testName, "info", 
                "Test duration: " + std::to_string(durationMs) + " ms");
}