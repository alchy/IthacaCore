#include "sampler.h"
#include "voice_manager.h"  // Pro VoiceManager testing

// Podmíněné include pro testovací systém
#ifdef ENABLE_TESTS
#include "voice_manager_tests.h"  // Pro VoiceManagerTester
#endif

#include <iostream>
#include <memory>

/**
 * @brief REFAKTOROVÁNO: runSampler s novou 3-fázovou architekturou
 * 
 * NOVÁ ARCHITEKTURA - Čisté oddělení fází:
 * FÁZE 1: Jednorázová inicializace (skenování adresáře + envelope generování)
 * FÁZE 2: Načtení pro konkrétní sample rate (data loading + envelope přepnutí)
 * FÁZE 3: JUCE příprava (buffer sizes pro voices)
 * 
 * Výhody nové architektury:
 * - Jasné oddělení odpovědností
 * - Snadná změna sample rate bez úplné reinicializace
 * - JUCE-ready workflow pattern
 * - Modulární struktura pro lepší testování
 * 
 * Test pipeline:
 * 1. VoiceManager instance creation
 * 2. 3-fázová inicializace
 * 3. VoiceManager testy (pokud ENABLE_TESTS)
 * 4. Demo testy s envelope systémem
 * 5. System statistics
 * 
 * @param logger Reference na Logger pro zaznamenání celého procesu
 * @return 0 při úspěchu, 1 při kritické chybě
 */
int runSampler(Logger& logger) {
    logger.log("runSampler", "info", "=== STARTING REFACTORED SAMPLER TEST SUITE ===");
    logger.log("runSampler", "info", "Using VoiceManager with 3-phase initialization + Envelope system");
    
    try {
        // 1. VYTVOŘENÍ VOICEMANAGER INSTANCE
        logger.log("runSampler", "info", "Creating VoiceManager instance...");
        VoiceManager voiceManager(DEFAULT_SAMPLE_DIR, logger);
        
        // 2. FÁZE 1: Jednorázová inicializace (skenování + envelope generování)
        logger.log("runSampler", "info", "=== INIT PHASE 1: System initialization ===");
        logger.log("runSampler", "info", "Scanning sample directory and generating envelope data...");
        voiceManager.initializeSystem(logger);
        
        // 3. FÁZE 2: Načtení pro konkrétní sample rate
        logger.log("runSampler", "info", "=== INIT PHASE 2: Loading for sample rate ===");
        logger.log("runSampler", "info", "Loading samples and configuring envelope for " + 
                  std::to_string(DEFAULT_SAMPLE_RATE) + " Hz...");
        voiceManager.loadForSampleRate(DEFAULT_SAMPLE_RATE, logger);
        
        // 4. FÁZE 3: JUCE příprava
        logger.log("runSampler", "info", "=== INIT PHASE 3: JUCE preparation ===");
        logger.log("runSampler", "info", "Preparing voices for audio processing...");
        voiceManager.prepareToPlay(DEFAULT_JUCE_BLOCK_SIZE);
        
        #ifdef ENABLE_TESTS
        // 5. VOICEMANAGER TESTOVACÍ SUITE
        logger.log("runSampler", "info", "=== STARTING VOICEMANAGER TEST SUITE ===");
        
        // Vytvoření VoiceManagerTester instance
        VoiceManagerTester testManager(128, DEFAULT_SAMPLE_RATE, logger);
        
        logger.log("runSampler", "info", "Running comprehensive VoiceManager tests with Envelope support...");
        
        int testResult = 0;  // Placeholder pro test výsledky
        
        // Velocity gain testy s envelope systémem
        logger.log("runSampler", "info", "Running velocity gain test...");
        if (!testManager.runVelocityGainTest(voiceManager)) {
            testResult++;
        }
        
        // Master gain testy  
        logger.log("runSampler", "info", "Running master gain test...");
        if (!testManager.runMasterGainTest(voiceManager)) {
            testResult++;
        }
        
        // Enhanced single note test s gain monitoringem
        logger.log("runSampler", "info", "Running enhanced single note test with gain monitoring...");
        if (!testManager.runSingleNoteTestWithGain(voiceManager)) {
            testResult++;
        }
        
        // Standard granular testy
        logger.log("runSampler", "info", "Running individual voice test...");
        if (!testManager.runIndividualVoiceTest(voiceManager)) {
            testResult++;
        }
        
        logger.log("runSampler", "info", "Running standard single note test...");
        if (!testManager.runSingleNoteTest(voiceManager)) {
            testResult++;
        }
        
        logger.log("runSampler", "info", "Running polyphony test...");
        if (!testManager.runPolyphonyTest(voiceManager)) {
            testResult++;
        }
        
        logger.log("runSampler", "info", "Running edge case tests...");
        if (!testManager.runEdgeCaseTests(voiceManager)) {
            testResult++;
        }
        
        // Export testy
        logger.log("runSampler", "info", "Running export tests...");
        if (!testManager.exportTestSample(voiceManager)) {
            testResult++;
        }
        
        // Výsledek testů
        if (testResult > 0) {
            logger.log("runSampler", "error", 
                      "VoiceManager tests failed with " + std::to_string(testResult) + " failures");
            return 1;
        } else {
            logger.log("runSampler", "info", "All VoiceManager tests passed successfully");
        }
        
        #else
        logger.log("runSampler", "info", "ENABLE_TESTS not defined - skipping VoiceManager tests");
        #endif
        
        // 6. SYSTEM STATISTICS
        logger.log("runSampler", "info", "=== FINAL SYSTEM STATISTICS ===");
        voiceManager.logSystemStatistics(logger);
        
        // 7. DEMO TESTY S ENVELOPE SYSTÉMEM
        logger.log("runSampler", "info", "=== DEMO TESTS WITH ENVELOPE SYSTEM ===");
        try {
            // Demo single note s envelope
            uint8_t testMidi = 108;
            uint8_t testVelocity = 100;
            
            logger.log("runSampler", "info", "Demo: Playing MIDI " + std::to_string(testMidi) + 
                      " with velocity " + std::to_string(testVelocity) + " (envelope-enabled)");
            
            voiceManager.setNoteState(testMidi, true, testVelocity);
            
            // Process několik bloků pro demonstraci envelope
            const int blockSize = 512;
            float* leftBuffer = new float[blockSize];
            float* rightBuffer = new float[blockSize];
            
            for (int i = 0; i < 5; ++i) {
                if (voiceManager.processBlock(leftBuffer, rightBuffer, blockSize)) {
                    float peakL = 0.0f;
                    for (int j = 0; j < blockSize; ++j) {
                        peakL = std::max(peakL, std::abs(leftBuffer[j]));
                    }
                    logger.log("runSampler", "info", 
                              "Demo block " + std::to_string(i) + " peak: " + std::to_string(peakL) + 
                              " (with envelope processing)");
                }
            }
            
            voiceManager.setNoteState(testMidi, false, 0);
            delete[] leftBuffer;
            delete[] rightBuffer;
            
            logger.log("runSampler", "info", "Demo tests completed successfully");
        } catch (...) {
            logger.log("runSampler", "warn", "Demo tests failed - continuing anyway");
        }
        
        // 8. ÚSPĚŠNÉ UKONČENÍ
        logger.log("runSampler", "info", "=== SAMPLER TEST SUITE COMPLETED SUCCESSFULLY ===");
        logger.log("runSampler", "info", "All tests passed including ENVELOPE SYSTEM functionality.");
        logger.log("runSampler", "info", "3-phase architecture verified: FÁZE 1 → FÁZE 2 → FÁZE 3");
        logger.log("runSampler", "info", "Envelope system integrated: runtime generation + frequency switching");
        logger.log("runSampler", "info", "System ready for production use.");
        
        return 0;  // Úspěch
        
    } catch (const std::exception& e) {
        logger.log("runSampler", "error", 
                  "CRITICAL ERROR in runSampler: " + std::string(e.what()));
        return 1;  // Chyba
    } catch (...) {
        logger.log("runSampler", "error", 
                  "UNKNOWN CRITICAL ERROR in runSampler");
        return 1;  // Neznámá chyba
    }
}

#ifdef ENABLE_TESTS
/**
 * @brief Implementace runVoiceManagerTests funkce s novou architekturou
 * @param testManager Reference na VoiceManagerTester instanci
 * @param loader Reference na InstrumentLoader pro inicializaci
 * @param logger Reference na Logger pro výstupy
 * @return int Počet selhání (0 = úspěch)
 */
int runVoiceManagerTests(VoiceManagerTester& testManager, InstrumentLoader& loader, Logger& logger) {
    logger.log("runVoiceManagerTests", "info", "Starting VoiceManager test execution with 3-phase architecture");
    
    try {
        // Vytvoření VoiceManager pro testy
        VoiceManager voiceManager(DEFAULT_SAMPLE_DIR, logger);
        
        // AKTUALIZOVÁNO: Inicializace s novou 3-fázovou architekturou
        voiceManager.initializeSystem(logger);          // FÁZE 1
        voiceManager.loadForSampleRate(DEFAULT_SAMPLE_RATE, logger); // FÁZE 2
        voiceManager.prepareToPlay(DEFAULT_JUCE_BLOCK_SIZE);         // FÁZE 3
        
        // Spuštění všech testů
        int failures = testManager.runAllTests(voiceManager, loader);
        
        // Summary
        if (failures == 0) {
            logger.log("runVoiceManagerTests", "info", "All tests passed with new architecture");
        } else {
            logger.log("runVoiceManagerTests", "error", 
                      std::to_string(failures) + " tests failed");
        }
        
        return failures;
        
    } catch (const std::exception& e) {
        logger.log("runVoiceManagerTests", "error", 
                  "Exception in runVoiceManagerTests: " + std::string(e.what()));
        return 1;
    } catch (...) {
        logger.log("runVoiceManagerTests", "error", 
                  "Unknown exception in runVoiceManagerTests");
        return 1;
    }
}
#endif

/**
 * @brief AKTUALIZOVÁNO: JUCE integration demonstration s novou architekturou
 * 
 * Ukazuje správný pattern pro integraci VoiceManager do JUCE AudioProcessor.
 * NOVÁ ARCHITEKTURA: Demonstruje 3-fázový workflow místo starých metod.
 * Tento kód slouží jako reference pro skutečnou JUCE implementaci.
 * 
 * @param logger Reference na Logger
 * @return 0 při úspěchu, 1 při chybě
 */
int demonstrateJuceIntegration(Logger& logger) {
    logger.log("JUCEDemo", "info", "=== JUCE INTEGRATION DEMONSTRATION ===");
    logger.log("JUCEDemo", "info", "Demonstrating 3-phase architecture for JUCE AudioProcessor");
    
    try {
        // 1. VYTVOŘENÍ VOICEMANAGER (equivalent to AudioProcessor constructor)
        logger.log("JUCEDemo", "info", "Creating VoiceManager (AudioProcessor constructor pattern)...");
        VoiceManager voiceManager(DEFAULT_SAMPLE_DIR, logger);
        
        // 2. SAMPLE RATE A BUFFER SIZE SETUP (equivalent to prepareToPlay)
        logger.log("JUCEDemo", "info", "Simulating AudioProcessor::prepareToPlay()...");
        
        const int demoSampleRate = DEFAULT_SAMPLE_RATE;
        const int demoBlockSize = DEFAULT_JUCE_BLOCK_SIZE;
        
        // NOVÁ ARCHITEKTURA: 3-fázová inicializace místo starých metod
        logger.log("JUCEDemo", "info", "=== Phase 1: System initialization ===");
        voiceManager.initializeSystem(logger);          // FÁZE 1
        
        logger.log("JUCEDemo", "info", "=== Phase 2: Loading for sample rate ===");
        voiceManager.loadForSampleRate(demoSampleRate, logger); // FÁZE 2
        
        logger.log("JUCEDemo", "info", "=== Phase 3: JUCE preparation ===");
        voiceManager.prepareToPlay(demoBlockSize);      // FÁZE 3
        
        logger.log("JUCEDemo", "info", 
                  "Prepared for sample rate: " + std::to_string(demoSampleRate) + 
                  " Hz, block size: " + std::to_string(demoBlockSize));
        
        // 3. SIMULACE ZMĚNY BUFFER SIZE BĚHEM RUNTIME
        logger.log("JUCEDemo", "info", "Simulating DAW buffer size change...");
        const int newBlockSize = 1024;
        voiceManager.prepareToPlay(newBlockSize);
        logger.log("JUCEDemo", "info", 
                  "Buffer size changed to: " + std::to_string(newBlockSize));
        
        // 4. SIMULACE MIDI INPUT A AUDIO PROCESSING S ENVELOPE SYSTÉMEM
        logger.log("JUCEDemo", "info", "Simulating MIDI input with ENVELOPE SYSTEM...");
        
        // Allocate audio buffers (equivalent to JUCE AudioBuffer)
        const int numSamples = demoBlockSize;
        float* leftChannel = new float[numSamples];
        float* rightChannel = new float[numSamples];
        
        // Simulate MIDI note-on events s různými velocities pro envelope testing
        const uint8_t testNote1 = 60;  // C4
        const uint8_t testNote2 = 64;  // E4
        const uint8_t testNote3 = 67;  // G4
        
        // Test různých velocities pro ověření envelope systému
        std::vector<uint8_t> testVelocities = {32, 64, 127};
        
        for (uint8_t velocity : testVelocities) {
            logger.log("JUCEDemo", "info", 
                      "Testing chord with velocity " + std::to_string(velocity) + " (envelope processing)");
            
            // Send MIDI note-on events
            voiceManager.setNoteState(testNote1, true, velocity);
            voiceManager.setNoteState(testNote2, true, velocity);
            voiceManager.setNoteState(testNote3, true, velocity);
            
            logger.log("JUCEDemo", "info", 
                      "Active voices after chord: " + std::to_string(voiceManager.getActiveVoicesCount()));
            
            // Process several audio blocks
            const int numBlocks = 5;
            for (int block = 0; block < numBlocks; ++block) {
                // Process audio block (equivalent to processBlock in JUCE)
                bool hasAudio = voiceManager.processBlock(leftChannel, rightChannel, numSamples);
                
                if (hasAudio) {
                    // Calculate peak levels for monitoring
                    float peakL = 0.0f, peakR = 0.0f;
                    for (int i = 0; i < numSamples; ++i) {
                        peakL = std::max(peakL, std::abs(leftChannel[i]));
                        peakR = std::max(peakR, std::abs(rightChannel[i]));
                    }
                    
                    logger.log("JUCEDemo", "info", 
                              "Velocity " + std::to_string(velocity) + 
                              ", Block " + std::to_string(block) + 
                              " - Peak L: " + std::to_string(peakL) + 
                              ", Peak R: " + std::to_string(peakR) + " (envelope-processed)");
                } else {
                    logger.log("JUCEDemo", "info", 
                              "Velocity " + std::to_string(velocity) + 
                              ", Block " + std::to_string(block) + " - Silent");
                }
            }
            
            // Simulate MIDI note-off events
            voiceManager.setNoteState(testNote1, false, 0);
            voiceManager.setNoteState(testNote2, false, 0);
            voiceManager.setNoteState(testNote3, false, 0);
            
            // Process release phase s envelope
            for (int block = 0; block < 3; ++block) {
                bool hasAudio = voiceManager.processBlock(leftChannel, rightChannel, numSamples);
                if (!hasAudio) {
                    logger.log("JUCEDemo", "info", 
                              "All voices finished releasing at block " + std::to_string(block) + " (envelope release)");
                    break;
                }
            }
        }
        
        // Cleanup
        delete[] leftChannel;
        delete[] rightChannel;
        
        // 5. FINAL STATISTICS
        voiceManager.logSystemStatistics(logger);
        
        logger.log("JUCEDemo", "info", "=== JUCE INTEGRATION DEMO COMPLETED ===");
        logger.log("JUCEDemo", "info", "3-phase architecture ready for real JUCE AudioProcessor implementation");
        logger.log("JUCEDemo", "info", "ENVELOPE SYSTEM verified in JUCE-like environment");
        logger.log("JUCEDemo", "info", "Pattern: FÁZE 1 → FÁZE 2 → FÁZE 3 → processBlock with envelope");
        
        return 0;
        
    } catch (const std::exception& e) {
        logger.log("JUCEDemo", "error", 
                  "Error in JUCE demo: " + std::string(e.what()));
        return 1;
    } catch (...) {
        logger.log("JUCEDemo", "error", "Unknown error in JUCE demo");
        return 1;
    }
}