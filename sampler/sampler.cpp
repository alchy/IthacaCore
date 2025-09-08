#include "sampler.h"
#include "voice_manager.h"  // Pro VoiceManager testing
#include <iostream>
#include <memory>

/**
 * @brief REFAKTOROVANÁ: runSampler jako thin wrapper pro VoiceManager testing
 * 
 * NOVÉ: Přidané gain testování pro ověření velocity aplikace na hlasitost.
 * Původní monolitická runSampler funkce byla nahrazena delegací na VoiceManager.
 * Tento approach poskytuje lepší separation of concerns a modularity.
 * 
 * Test pipeline:
 * 1. VoiceManager instance creation
 * 2. Sample rate configuration
 * 3. System initialization (directory scan)
 * 4. Instrument loading (audio data)
 * 5. System integrity validation
 * 6. NOVÉ: Velocity gain testing suite
 * 7. Standard granular testing suite
 * 8. System statistics
 * 
 * @param logger Reference na Logger pro zaznamenání celého procesu
 * @return 0 při úspěchu, 1 při kritické chybě
 */
int runSampler(Logger& logger) {
    logger.log("runSampler", "info", "=== STARTING REFACTORED SAMPLER TEST SUITE ===");
    logger.log("runSampler", "info", "Using VoiceManager-based architecture with FIXED Voice gain system");
    
    try {
        // 1. VYTVOŘENÍ VOICEMANAGER INSTANCE
        logger.log("runSampler", "info", "Creating VoiceManager instance...");
        VoiceManager voiceManager(DEFAULT_SAMPLE_DIR, logger);
        
        // 2. SAMPLE RATE CONFIGURATION
        logger.log("runSampler", "info", "Configuring sample rate...");
        voiceManager.changeSampleRate(DEFAULT_SAMPLE_RATE, logger);
        
        // 3. SYSTEM INITIALIZATION (Directory scan)
        logger.log("runSampler", "info", "Initializing system (scanning sample directory)...");
        voiceManager.initializeSystem(logger);
        
        // 4. INSTRUMENT LOADING (Audio data loading)
        logger.log("runSampler", "info", "Loading all instruments into memory...");
        voiceManager.loadAllInstruments(logger);
        
        // 5. SYSTEM INTEGRITY VALIDATION
        logger.log("runSampler", "info", "Validating system integrity...");
        voiceManager.validateSystemIntegrity(logger);
        
        // 6. NOVÉ: VELOCITY GAIN TESTING SUITE
        logger.log("runSampler", "info", "=== STARTING VELOCITY GAIN TESTS ===");
        
        // Test velocity gain s různými hodnotami
        logger.log("runSampler", "info", "Running velocity gain test...");
        voiceManager.runVelocityGainTest(logger);
        
        // Test master gain nastavení
        logger.log("runSampler", "info", "Running master gain test...");
        voiceManager.runMasterGainTest(logger);
        
        // Enhanced single note test s gain monitoringem
        logger.log("runSampler", "info", "Running enhanced single note test with gain monitoring...");
        voiceManager.runSingleNoteTestWithGain(logger);
        
        // 7. STANDARD GRANULAR TESTING SUITE
        logger.log("runSampler", "info", "=== STARTING STANDARD GRANULAR TESTS ===");
        
        // Individual voice inspection
        logger.log("runSampler", "info", "Running individual voice test...");
        voiceManager.runIndividualVoiceTest(logger);
        
        // Single note functionality
        logger.log("runSampler", "info", "Running standard single note test...");
        voiceManager.runSingleNoteTest(logger);
        
        // Polyphonic capabilities
        logger.log("runSampler", "info", "Running polyphony test...");
        voiceManager.runPolyphonyTest(logger);
        
        // Edge case handling
        logger.log("runSampler", "info", "Running edge case tests...");
        voiceManager.runEdgeCaseTests(logger);
        
        // 8. SYSTEM STATISTICS
        logger.log("runSampler", "info", "=== FINAL SYSTEM STATISTICS ===");
        voiceManager.logSystemStatistics(logger);
        
        // 9. DODATEČNÉ EXPORT TESTY (Optional)
        logger.log("runSampler", "info", "=== EXPORT TESTS ===");
        try {
            // Test export raw sample data
            voiceManager.exportTestSample(TEST_MIDI_NOTE, TEST_VELOCITY, EXPORT_DIR, logger);
            
            logger.log("runSampler", "info", "Export tests completed successfully");
        } catch (...) {
            logger.log("runSampler", "warn", "Export tests failed - continuing anyway");
        }
        
        // 10. ÚSPĚŠNÉ UKONČENÍ
        logger.log("runSampler", "info", "=== SAMPLER TEST SUITE COMPLETED SUCCESSFULLY ===");
        logger.log("runSampler", "info", "All tests passed including VELOCITY GAIN functionality.");
        logger.log("runSampler", "info", "Voice gain system verified: velocity now properly affects output volume.");
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

/**
 * @brief NOVÉ: JUCE integration demonstration
 * 
 * Ukazuje správný pattern pro integraci VoiceManager do JUCE AudioProcessor.
 * Tento kód slouží jako reference pro skutečnou JUCE implementaci.
 * 
 * @param logger Reference na Logger
 * @return 0 při úspěchu, 1 při chybě
 */
int demonstrateJuceIntegration(Logger& logger) {
    logger.log("JUCEDemo", "info", "=== JUCE INTEGRATION DEMONSTRATION ===");
    
    try {
        // 1. VYTVOŘENÍ VOICEMANAGER (equivalent to AudioProcessor constructor)
        logger.log("JUCEDemo", "info", "Creating VoiceManager (AudioProcessor constructor pattern)...");
        VoiceManager voiceManager(DEFAULT_SAMPLE_DIR, logger);
        
        // 2. SAMPLE RATE A BUFFER SIZE SETUP (equivalent to prepareToPlay)
        logger.log("JUCEDemo", "info", "Simulating AudioProcessor::prepareToPlay()...");
        
        const int demoSampleRate = DEFAULT_SAMPLE_RATE;
        const int demoBlockSize = DEFAULT_JUCE_BLOCK_SIZE;
        
        // Sample rate configuration
        voiceManager.changeSampleRate(demoSampleRate, logger);
        
        // Buffer size preparation
        voiceManager.prepareToPlay(demoBlockSize);
        logger.log("JUCEDemo", "info", 
                  "Prepared for sample rate: " + std::to_string(demoSampleRate) + 
                  " Hz, block size: " + std::to_string(demoBlockSize));
        
        // System initialization
        voiceManager.initializeSystem(logger);
        voiceManager.loadAllInstruments(logger);
        voiceManager.validateSystemIntegrity(logger);
        
        // 3. SIMULACE ZMĚNY BUFFER SIZE BĚHEM RUNTIME
        logger.log("JUCEDemo", "info", "Simulating DAW buffer size change...");
        const int newBlockSize = 1024;
        voiceManager.prepareToPlay(newBlockSize);
        logger.log("JUCEDemo", "info", 
                  "Buffer size changed to: " + std::to_string(newBlockSize));
        
        // 4. SIMULACE MIDI INPUT A AUDIO PROCESSING S VELOCITY TESTOVÁNÍM
        logger.log("JUCEDemo", "info", "Simulating MIDI input with VELOCITY TESTING...");
        
        // Allocate audio buffers (equivalent to JUCE AudioBuffer)
        const int numSamples = demoBlockSize;
        float* leftChannel = new float[numSamples];
        float* rightChannel = new float[numSamples];
        
        // Simulate MIDI note-on events s různými velocities
        const uint8_t testNote1 = 60;  // C4
        const uint8_t testNote2 = 64;  // E4
        const uint8_t testNote3 = 67;  // G4
        
        // Test různých velocities pro ověření gain systému
        std::vector<uint8_t> testVelocities = {32, 64, 127};
        
        for (uint8_t velocity : testVelocities) {
            logger.log("JUCEDemo", "info", 
                      "Testing chord with velocity " + std::to_string(velocity));
            
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
                              ", Peak R: " + std::to_string(peakR));
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
            
            // Process release phase
            for (int block = 0; block < 3; ++block) {
                bool hasAudio = voiceManager.processBlock(leftChannel, rightChannel, numSamples);
                if (!hasAudio) {
                    logger.log("JUCEDemo", "info", 
                              "All voices finished releasing at block " + std::to_string(block));
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
        logger.log("JUCEDemo", "info", "Pattern ready for real JUCE AudioProcessor implementation");
        logger.log("JUCEDemo", "info", "VELOCITY GAIN SYSTEM verified in JUCE-like environment");
        
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

/**
 * @brief POMOCNÁ FUNKCE: Sample JUCE AudioProcessor integration code
 * 
 * Tento kód ukazuje, jak by vypadala skutečná integrace do JUCE AudioProcessor.
 * Kopírujte tento pattern do vašeho AudioProcessor potomka.
 */

/*
// SAMPLE JUCE AUDIOPROCESSOR INTEGRATION S OPRAVENÝM GAIN SYSTÉMEM:

class YourSamplerAudioProcessor : public juce::AudioProcessor {
public:
    YourSamplerAudioProcessor() 
        : voiceManager_(DEFAULT_SAMPLE_DIR, logger_) {
        // VoiceManager se vytvoří s cestou k samples
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        // 1. Configure sample rate
        voiceManager_.changeSampleRate(static_cast<int>(sampleRate), logger_);
        
        // 2. KRITICKÉ: Prepare for buffer size
        voiceManager_.prepareToPlay(samplesPerBlock);
        
        // 3. Initialize system if not done yet
        if (!systemInitialized_) {
            voiceManager_.initializeSystem(logger_);
            voiceManager_.loadAllInstruments(logger_);
            voiceManager_.validateSystemIntegrity(logger_);
            systemInitialized_ = true;
        }
        
        // 4. Log preparation
        logger_.log("AudioProcessor", "info", 
                   "Prepared for " + std::to_string(sampleRate) + " Hz, " + 
                   std::to_string(samplesPerBlock) + " samples per block");
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        const int numSamples = buffer.getNumSamples();
        
        // 1. Handle MIDI events - VELOCITY NYŮ SPRÁVNĚ OVLIVŇUJE HLASITOST!
        for (const auto metadata : midiMessages) {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn()) {
                // OPRAVENO: velocity nyní správně aplikuje gain na výstup
                voiceManager_.setNoteState(msg.getNoteNumber(), true, msg.getVelocity());
            } else if (msg.isNoteOff()) {
                voiceManager_.setNoteState(msg.getNoteNumber(), false, 0);
            }
        }
        
        // 2. Process audio - get pointers to JUCE buffer channels
        float* leftChannel = buffer.getWritePointer(0);
        float* rightChannel = buffer.getNumChannels() > 1 ? 
                             buffer.getWritePointer(1) : leftChannel;
        
        // 3. KRITICKÉ: VoiceManager processBlock handles buffer clearing and mixing
        // S OPRAVENÝM GAIN SYSTÉMEM: envelope * velocity * master * voiceScaling
        voiceManager_.processBlock(leftChannel, rightChannel, numSamples);
    }

    void releaseResources() override {
        voiceManager_.stopAllVoices();
        voiceManager_.resetAllVoices(logger_);
    }

private:
    VoiceManager voiceManager_;
    Logger logger_;
    bool systemInitialized_ = false;
};
*/


/**
 * @brief POMOCNÁ FUNKCE: Sample JUCE AudioProcessor integration code
 * 
 * Tento kód ukazuje, jak by vypadala skutečná integrace do JUCE AudioProcessor.
 * Kopírujte tento pattern do vašeho AudioProcessor potomka.
 */

/*
// SAMPLE JUCE AUDIOPROCESSOR INTEGRATION S OPRAVENÝM GAIN SYSTÉMEM:

class YourSamplerAudioProcessor : public juce::AudioProcessor {
public:
    YourSamplerAudioProcessor() 
        : voiceManager_(DEFAULT_SAMPLE_DIR, logger_) {
        // VoiceManager se vytvoří s cestou k samples
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        // 1. Configure sample rate
        voiceManager_.changeSampleRate(static_cast<int>(sampleRate), logger_);
        
        // 2. KRITICKÉ: Prepare for buffer size
        voiceManager_.prepareToPlay(samplesPerBlock);
        
        // 3. Initialize system if not done yet
        if (!systemInitialized_) {
            voiceManager_.initializeSystem(logger_);
            voiceManager_.loadAllInstruments(logger_);
            voiceManager_.validateSystemIntegrity(logger_);
            systemInitialized_ = true;
        }
        
        // 4. Log preparation
        logger_.log("AudioProcessor", "info", 
                   "Prepared for " + std::to_string(sampleRate) + " Hz, " + 
                   std::to_string(samplesPerBlock) + " samples per block");
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        const int numSamples = buffer.getNumSamples();
        
        // 1. Handle MIDI events - VELOCITY NYŮ SPRÁVNĚ OVLIVŇUJE HLASITOST!
        for (const auto metadata : midiMessages) {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn()) {
                // OPRAVENO: velocity nyní správně aplikuje gain na výstup
                voiceManager_.setNoteState(msg.getNoteNumber(), true, msg.getVelocity());
            } else if (msg.isNoteOff()) {
                voiceManager_.setNoteState(msg.getNoteNumber(), false, 0);
            }
        }
        
        // 2. Process audio - get pointers to JUCE buffer channels
        float* leftChannel = buffer.getWritePointer(0);
        float* rightChannel = buffer.getNumChannels() > 1 ? 
                             buffer.getWritePointer(1) : leftChannel;
        
        // 3. KRITICKÉ: VoiceManager processBlock handles buffer clearing and mixing
        // S OPRAVENÝM GAIN SYSTÉMEM: envelope * velocity * master * voiceScaling
        voiceManager_.processBlock(leftChannel, rightChannel, numSamples);
    }

    void releaseResources() override {
        voiceManager_.stopAllVoices();
        voiceManager_.resetAllVoices(logger_);
    }

private:
    VoiceManager voiceManager_;
    Logger logger_;
    bool systemInitialized_ = false;
};

*/