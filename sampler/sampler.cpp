#include "sampler.h"
#include "voice_manager.h"  // NOVÉ: Pro VoiceManager facade

// Základní includes
#include <string>
#include <exception>
#include <stdexcept>
#include <iostream>
#include <cstdlib>

/**
 * @brief REFAKTOROVANÉ: Thin wrapper pro VoiceManager facade
 * @param logger Reference na Logger pro logování.
 * @return 0 při úspěchu.
 * 
 * ZMĚNY:
 * - Veškerá business logika přesunuta do VoiceManager
 * - sampler.cpp slouží pouze jako demonstration/testing wrapper
 * - Konfigurace přes #define constants ze sampler.h
 */
int runSampler(Logger& logger) {
    logger.log("runSampler", "info", "Starting IthacaCore sampler with VoiceManager facade");

    try {
        // NOVÉ: Inicializace VoiceManager BEZ automatického sample rate
        logger.log("runSampler", "info", "Creating VoiceManager with sampleDir: " + std::string(DEFAULT_SAMPLE_DIR));
        VoiceManager voiceManager(DEFAULT_SAMPLE_DIR, logger);

        // NOVÉ: Veškerá business logika delegována na VoiceManager facade
        logger.log("runSampler", "info", "=== VoiceManager Initialization Pipeline ===");
        
        // Sample rate configuration (EXPLICITNÍ nastavení)
        voiceManager.changeSampleRate(DEFAULT_SAMPLE_RATE, logger);
        
        // System initialization phase
        voiceManager.initializeSystem(logger);       // Nahradí SamplerIO::scanSampleDirectory
        voiceManager.loadAllInstruments(logger);     // Nahradí InstrumentLoader::loadInstrumentData
        voiceManager.validateSystemIntegrity(logger); // Kompletní system validation

        // Testing/demonstration phase
        logger.log("runSampler", "info", "=== VoiceManager Testing Pipeline ===");
        
        voiceManager.runSingleNoteTest(logger);      // Single voice test
        voiceManager.runPolyphonyTest(logger);       // Multi-voice test  
        voiceManager.runEdgeCaseTests(logger);       // Edge cases and error handling
        voiceManager.runIndividualVoiceTest(logger); // Voice state monitoring

        // Export and diagnostics phase
        logger.log("runSampler", "info", "=== VoiceManager Export & Diagnostics ===");
        
        voiceManager.exportTestSample(TEST_MIDI_NOTE, TEST_VELOCITY, EXPORT_DIR, logger);
        voiceManager.logSystemStatistics(logger);

        // Optional: Dynamic sample rate demonstration
        logger.log("runSampler", "info", "=== Dynamic Sample Rate Change Demo ===");
        voiceManager.changeSampleRate(ALTERNATIVE_SAMPLE_RATE, logger);
        voiceManager.logSystemStatistics(logger);

        // Reset to original sample rate
        voiceManager.changeSampleRate(DEFAULT_SAMPLE_RATE, logger);

        logger.log("runSampler", "info", "Sampler workflow completed successfully via VoiceManager facade");
        return 0;

    } catch (const std::exception& e) {
        logger.log("runSampler", "error", "Exception in VoiceManager workflow: " + std::string(e.what()));
        return 1;
    } catch (...) {
        logger.log("runSampler", "error", "Unknown exception in VoiceManager workflow");
        return 1;
    }
}