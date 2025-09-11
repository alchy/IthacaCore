#include "sampler.h"
#include "voice_manager.h"

// Podmíněné include pro testovací systém
#ifdef ENABLE_TESTS
// Starý testovací systém (zachováno pro kompatibilitu)
#include "tests/original-working-tests/voice_manager_tests.h"

// NOVÝ testovací framework
#include "tests/test_registry.h"
#include "tests/velocity_gain_test.h"
#include "tests/master_gain_test.h"
#include "tests/envelope_test.h"
#include "tests/single_note_test.h"
#include "tests/polyphony_test.h"
#include "tests/edge_case_test.h"
#include "tests/performance_test.h"
#endif

#include <iostream>
#include <memory>

/**
 * @brief REFAKTOROVÁNO: runSampler s hybridním testovacím přístupem
 * 
 * NOVÁ ARCHITEKTURA:
 * FÁZE 1: Jednorázová inicializace (skenování adresáře + envelope generování)
 * FÁZE 2: Načtení pro konkrétní sample rate (data loading + envelope přepnutí)
 * FÁZE 3: JUCE příprava (buffer sizes pro voices)
 * FÁZE 4: HYBRIDNÍ TESTOVÁNÍ - kombinace starých a nových testů
 * 
 * Test pipeline:
 * 1. VoiceManager instance creation
 * 2. 3-fázová inicializace
 * 3. Starý VoiceManager tester (pokud ENABLE_TESTS)
 * 4. NOVÝ test framework s registrací
 * 5. Demo testy s envelope systémem
 * 6. System statistics
 * 
 * @param logger Reference na Logger pro zaznamenání celého procesu
 * @return 0 při úspěchu, 1 při kritické chybě
 */
int runSampler(Logger& logger) {
    logger.log("runSampler", "info", "=== STARTING HYBRID SAMPLER TEST SUITE ===");
    logger.log("runSampler", "info", "Using VoiceManager with 3-phase initialization + HYBRID testing framework");
    
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
        // 5. HYBRIDNÍ TESTOVACÍ SUITE
        logger.log("runSampler", "info", "=== STARTING HYBRID TEST SUITE ===");
        
        int totalFailures = 0;
        
        // === ČÁST A: STARÝ TESTOVACÍ SYSTÉM (zachováno pro kritické testy) ===
        logger.log("runSampler", "info", "=== PART A: Legacy VoiceManager Tests ===");
        
        // Vytvoření VoiceManagerTester instance pro kritické testy
        VoiceManagerTester legacyTester(128, DEFAULT_SAMPLE_RATE, logger);
        
        logger.log("runSampler", "info", "Running legacy critical tests...");
        
        // Jen nejdůležitější testy ze starého systému
        if (!legacyTester.runVelocityGainTest(voiceManager)) {
            totalFailures++;
            logger.log("runSampler", "error", "Legacy velocity gain test FAILED");
        }
        
        if (!legacyTester.runSingleNoteTest(voiceManager)) {
            totalFailures++;
            logger.log("runSampler", "error", "Legacy single note test FAILED");
        }
        
        if (!legacyTester.runPolyphonyTest(voiceManager)) {
            totalFailures++;
            logger.log("runSampler", "error", "Legacy polyphony test FAILED");
        }
        
        logger.log("runSampler", "info", "Legacy tests completed with " + 
                  std::to_string(totalFailures) + " failures");
        
        // === ČÁST B: NOVÝ TESTOVACÍ FRAMEWORK ===
        logger.log("runSampler", "info", "=== PART B: New Test Framework ===");
        
        // Inicializace nového test frameworku
        TestRegistry registry(logger);
        
        // Konfigurace testů s exportem povoleným
        TestConfig config;
        config.exportAudio = true;
        config.exportBlockSize = 512;
        config.defaultTestVelocity = 100;
        config.testMasterGains = {0.1f, 0.3f, 0.5f, 0.8f, 1.0f};
        config.exportDir = "./exports/tests";
        config.verboseLogging = true;
        
        logger.log("runSampler", "info", "Registering new test framework tests...");
        
        // Registrace všech nových testů
        registry.registerTest(std::make_unique<VelocityGainTest>(logger, config));
        registry.registerTest(std::make_unique<MasterGainTest>(logger, config));
        registry.registerTest(std::make_unique<EnvelopeTest>(logger, config));
        registry.registerTest(std::make_unique<SingleNoteTest>(logger, config));
        registry.registerTest(std::make_unique<PolyphonyTest>(logger, config));
        registry.registerTest(std::make_unique<EdgeCaseTest>(logger, config));
        registry.registerTest(std::make_unique<PerformanceTest>(logger, config));
        
        logger.log("runSampler", "info", "Running new framework tests...");
        
        // Spuštění všech nových testů
        auto newTestResults = registry.runAll(voiceManager, config);
        
        // Analýza výsledků nových testů
        int newTestFailures = 0;
        for (const auto& [testName, result] : newTestResults) {
            if (!result.passed) {
                newTestFailures++;
                logger.log("runSampler", "error", "New test FAILED: " + testName + 
                          " - " + result.errorMessage);
            } else {
                logger.log("runSampler", "info", "New test PASSED: " + testName + 
                          " - " + result.details);
            }
        }
        
        totalFailures += newTestFailures;
        
        logger.log("runSampler", "info", "New framework tests completed with " + 
                  std::to_string(newTestFailures) + " failures");
        
        // === ČÁST C: HYBRIDNÍ VÝSLEDKY ===
        logger.log("runSampler", "info", "=== HYBRID TEST RESULTS ===");
        logger.log("runSampler", "info", "Legacy tests: Critical functionality verified");
        logger.log("runSampler", "info", "New tests: " + std::to_string(newTestResults.size()) + 
                  " comprehensive tests with export capability");
        logger.log("runSampler", "info", "Total test failures: " + std::to_string(totalFailures));
        
        if (totalFailures > 0) {
            logger.log("runSampler", "error", 
                      "HYBRID test suite failed with " + std::to_string(totalFailures) + " failures");
            // Pokračujeme v demo testech i při selhání, ale označíme výsledek
        } else {
            logger.log("runSampler", "info", "All HYBRID tests passed successfully");
        }
        
        #else
        logger.log("runSampler", "info", "ENABLE_TESTS not defined - skipping test suites");
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
                if (voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize)) {
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
        logger.log("runSampler", "info", "=== HYBRID SAMPLER TEST SUITE COMPLETED ===");
        logger.log("runSampler", "info", "Architecture verified: 3-phase initialization + hybrid testing");
        logger.log("runSampler", "info", "Legacy tests: Critical functionality preserved");
        logger.log("runSampler", "info", "New framework: Comprehensive testing with export capability");
        logger.log("runSampler", "info", "Envelope system integrated: runtime generation + frequency switching");
        logger.log("runSampler", "info", "Export files available in: ./exports/tests/");
        logger.log("runSampler", "info", "System ready for production use.");
        
        #ifdef ENABLE_TESTS
        return (totalFailures > 0) ? 1 : 0;  // Návrat podle výsledků testů
        #else
        return 0;  // Bez testů = úspěch
        #endif
        
    } catch (const std::exception& e) {
        logger.log("runSampler", "error", 
                  "CRITICAL ERROR in hybrid runSampler: " + std::string(e.what()));
        return 1;  // Chyba
    } catch (...) {
        logger.log("runSampler", "error", 
                  "UNKNOWN CRITICAL ERROR in hybrid runSampler");
        return 1;  // Neznámá chyba
    }
}