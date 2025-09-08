#include "voice_manager.h"
#include "sampler.h"            // Pro stack allocated SamplerIO
#include "instrument_loader.h"  // Pro stack allocated InstrumentLoader
#include "wav_file_exporter.h"  // Pro exportTestSample
#include <algorithm>
#include <cmath>

/**
 * @brief Constructor: ROZŠÍŘENÝ s stack allocated komponenty, BEZ automatické inicializace sample rate
 * @param sampleDir Cesta k sample adresáři
 * @param logger Reference na Logger
 * 
 * DŮLEŽITÉ: Constructor pouze vytvoří objekty s neinicializovaným sample rate!
 * Musí se explicitně volat changeSampleRate() + initializeSystem() + loadAllInstruments()
 */
VoiceManager::VoiceManager(const std::string& sampleDir, Logger& logger)
    : samplerIO_(),              // Stack allocated SamplerIO - prázdný stav
      instrumentLoader_(),       // Stack allocated InstrumentLoader - prázdný stav
      currentSampleRate_(0),     // NEINICIALIZOVÁNO - bude nastaveno changeSampleRate()
      sampleDir_(sampleDir),
      systemInitialized_(false), // System není připraven
      voices_(),
      activeVoices_(),
      voicesToRemove_(),
      activeVoicesCount_(0),
      rtMode_(false) {
    
    if (sampleDir_.empty()) {
        logSafe("VoiceManager/constructor", "error", 
               "Invalid sampleDir - cannot be empty. Terminating.", logger);
        std::exit(1);
    }
    
    // Původní inicializace voices (zachovat!)
    voices_.resize(128);
    for (int i = 0; i < 128; ++i) {
        voices_[i] = Voice(static_cast<uint8_t>(i));
    }
    
    activeVoices_.reserve(128);
    voicesToRemove_.reserve(128);
    
    logSafe("VoiceManager/constructor", "info", 
           "VoiceManager created with sampleDir '" + sampleDir_ + 
           "'. Ready for changeSampleRate() and initialization pipeline.", logger);
}

/**
 * @brief Nastavení sample rate a trigger reinicializace
 * @param newSampleRate Nový sample rate (44100 nebo 48000)
 * @param logger Reference na Logger
 */
void VoiceManager::changeSampleRate(int newSampleRate, Logger& logger) {
    // Validace sample rate
    if (newSampleRate != 44100 && newSampleRate != 48000) {
        logger.log("VoiceManager/changeSampleRate", "error", 
                  "Invalid sample rate " + std::to_string(newSampleRate) + 
                  " Hz - only 44100 Hz and 48000 Hz are supported. Terminating.");
        std::exit(1);
    }
    
    // DEBUG: Detailní info o aktuálním stavu
    logger.log("VoiceManager/changeSampleRate", "debug", 
              "DEBUG: Requested sample rate: " + std::to_string(newSampleRate) + " Hz");
    logger.log("VoiceManager/changeSampleRate", "debug", 
              "DEBUG: Current sample rate (VoiceManager): " + std::to_string(currentSampleRate_) + " Hz");
    logger.log("VoiceManager/changeSampleRate", "debug", 
              "DEBUG: Current sample rate (InstrumentLoader): " + std::to_string(instrumentLoader_.getActualSampleRate()) + " Hz");
    
    if (needsReinitialization(newSampleRate)) {
        logger.log("VoiceManager/changeSampleRate", "info", 
                  "Sample rate change detected: " + std::to_string(currentSampleRate_) + 
                  " Hz -> " + std::to_string(newSampleRate) + " Hz. Reinitializing...");
        
        // Stop all active voices před reinicializací
        stopAllVoices();
        
        // Update target sample rate
        currentSampleRate_ = newSampleRate;
        
        // Reset system initialization flag
        systemInitialized_ = false;
        
        // KRITICKÉ: Pokud už byl system inicializován, reinicializuj komplet
        if (instrumentLoader_.getActualSampleRate() != 0) {
            try {
                // Full reinitialization pipeline
                initializeSystem(logger);      // Re-scan s novým sample rate
                loadAllInstruments(logger);    // Reload s novým sample rate
                validateSystemIntegrity(logger);
                
                logger.log("VoiceManager/changeSampleRate", "info", 
                          "Sample rate change completed successfully");
            } catch (...) {
                logger.log("VoiceManager/changeSampleRate", "error", 
                          "Sample rate change failed during reinitialization. Terminating.");
                std::exit(1);
            }
        } else {
            logger.log("VoiceManager/changeSampleRate", "info", 
                      "Sample rate set to " + std::to_string(newSampleRate) + 
                      " Hz. System ready for initialization.");
        }
    } else {
        logger.log("VoiceManager/changeSampleRate", "info", 
                  "Sample rate unchanged: " + std::to_string(newSampleRate) + 
                  " Hz (no reinitialization needed)");
    }
}

/**
 * @brief NOVÉ: Prepare all voices for new buffer size (JUCE integration)
 * @param maxBlockSize Maximum block size from DAW
 */
void VoiceManager::prepareToPlay(int maxBlockSize) noexcept {
    // Prepare all 128 voices for new buffer size
    for (int i = 0; i < 128; ++i) {
        voices_[i].prepareToPlay(maxBlockSize);
    }
    
    // Note: Logging není RT-safe, ale tato metoda se volá během setup, ne RT processing
    // V production kódu by zde byl conditional logging based on rtMode_
}

/**
 * @brief Inicializace systému - skenování sample adresáře
 * @param logger Reference na Logger
 */
void VoiceManager::initializeSystem(Logger& logger) {
    if (currentSampleRate_ <= 0) {
        logger.log("VoiceManager/initializeSystem", "error", 
                  "Sample rate not set. Call changeSampleRate() first. Terminating.");
        std::exit(1);
    }
    
    logger.log("VoiceManager/initializeSystem", "info", 
              "Starting system initialization with sampleDir: " + sampleDir_);
    
    try {
        // Delegace na SamplerIO - skenování adresáře
        samplerIO_.scanSampleDirectory(sampleDir_, logger);
        
        logger.log("VoiceManager/initializeSystem", "info", 
                  "System initialization completed successfully");
        
    } catch (...) {
        logger.log("VoiceManager/initializeSystem", "error", 
                  "System initialization failed. Terminating.");
        std::exit(1);
    }
}

/**
 * @brief Načtení všech instrumentů do paměti
 * @param logger Reference na Logger
 */
void VoiceManager::loadAllInstruments(Logger& logger) {
    if (currentSampleRate_ <= 0) {
        logger.log("VoiceManager/loadAllInstruments", "error", 
                  "Sample rate not set. Call changeSampleRate() first. Terminating.");
        std::exit(1);
    }
    
    logger.log("VoiceManager/loadAllInstruments", "info", 
              "Starting instrument loading with targetSampleRate: " + 
              std::to_string(currentSampleRate_) + " Hz");
    
    try {
        // Delegace na InstrumentLoader - načtení všech dat
        instrumentLoader_.loadInstrumentData(samplerIO_, currentSampleRate_, logger);
        
        // Inicializace všech voices s načtenými instrumenty
        initializeVoicesWithInstruments(logger);
        
        systemInitialized_ = true;
        
        logger.log("VoiceManager/loadAllInstruments", "info", 
                  "All instruments loaded and voices initialized successfully");
        
    } catch (...) {
        logger.log("VoiceManager/loadAllInstruments", "error", 
                  "Instrument loading failed. Terminating.");
        std::exit(1);
    }
}

/**
 * @brief System validation
 */
void VoiceManager::validateSystemIntegrity(Logger& logger) {
    if (!systemInitialized_) {
        logSafe("VoiceManager/validateSystemIntegrity", "error", 
                "System not initialized. Call loadAllInstruments() first. Terminating.", logger);
        std::exit(1);
    }
    
    logSafe("VoiceManager/validateSystemIntegrity", "info", 
            "Starting system integrity validation...", logger);
    
    try {
        instrumentLoader_.validateStereoConsistency(logger);
        
        int validVoices = 0;
        for (int i = 0; i < 128; ++i) {
            const Voice& voice = voices_[i];
            if (voice.getMidiNote() == i) {
                validVoices++;
            } else {
                logSafe("VoiceManager/validateSystemIntegrity", "error", 
                        "Voice MIDI note mismatch: voice index " + std::to_string(i) + 
                        " has midiNote " + std::to_string(voice.getMidiNote()) + ". Terminating.", logger);
                std::exit(1);
            }
        }
        
        logSafe("VoiceManager/validateSystemIntegrity", "info", 
                "System integrity validation completed successfully. " + 
                std::to_string(validVoices) + " voices validated.", logger);
    } catch (...) {
        logSafe("VoiceManager/validateSystemIntegrity", "error", 
                "System integrity validation failed. Terminating.", logger);
        std::exit(1);
    }
}

/**
 * @brief Statistiky celého systému
 * @param logger Reference na Logger
 */
void VoiceManager::logSystemStatistics(Logger& logger) {
    if (!systemInitialized_) {
        logger.log("VoiceManager/logSystemStatistics", "warn", 
                  "System not initialized - limited statistics available");
        return;
    }
    
    // InstrumentLoader statistiky
    const int totalSamples = instrumentLoader_.getTotalLoadedSamples();
    const int monoSamples = instrumentLoader_.getMonoSamplesCount();
    const int stereoSamples = instrumentLoader_.getStereoSamplesCount();
    const int actualSampleRate = instrumentLoader_.getActualSampleRate();
    
    // VoiceManager statistiky
    const int activeCount = getActiveVoicesCount();
    const int sustaining = getSustainingVoicesCount();
    const int releasing = getReleasingVoicesCount();
    
    logger.log("VoiceManager/statistics", "info", 
              "=== SYSTEM STATISTICS ===");
    logger.log("VoiceManager/statistics", "info", 
              "Sample Rate: " + std::to_string(actualSampleRate) + " Hz");
    logger.log("VoiceManager/statistics", "info", 
              "Total loaded samples: " + std::to_string(totalSamples));
    logger.log("VoiceManager/statistics", "info", 
              "Originally mono: " + std::to_string(monoSamples) + 
              ", Originally stereo: " + std::to_string(stereoSamples));
    logger.log("VoiceManager/statistics", "info", 
              "Voice Activity - Active: " + std::to_string(activeCount) + 
              ", Sustaining: " + std::to_string(sustaining) + 
              ", Releasing: " + std::to_string(releasing) + 
              ", Max: 128");
    logger.log("VoiceManager/statistics", "info", 
              "========================");
}

// ===== NOVÉ TESTING METHODS - GAIN SPECIFIC =====

/**
 * @brief NOVÁ metoda: Test velocity gain s různými hodnotami
 * Testuje aplikaci MIDI velocity na hlasitost s detailním logováním.
 */
void VoiceManager::runVelocityGainTest(Logger& logger) {
    if (!systemInitialized_) {
        logger.log("VoiceManager/runVelocityGainTest", "error", 
                  "System not initialized. Call loadAllInstruments() first.");
        return;
    }
    
    logger.log("VoiceManager/runVelocityGainTest", "info", 
              "=== STARTING VELOCITY GAIN TEST ===");
    
    uint8_t testMidi = findValidTestMidiNote(logger);
    
    // Test různých velocity hodnot: 1, 32, 64, 96, 127
    std::vector<uint8_t> testVelocities = {1, 32, 64, 96, 127};
    
    const int blockSize = 512;
    float* leftBuffer = new float[blockSize];
    float* rightBuffer = new float[blockSize];
    
    for (uint8_t velocity : testVelocities) {
        logger.log("VoiceManager/runVelocityGainTest", "info", 
                  "Testing velocity " + std::to_string(velocity) + " for MIDI " + std::to_string(testMidi));
        
        // Note-on s testovanou velocity
        setNoteState(testMidi, true, velocity);
        
        Voice& voice = getVoice(testMidi);
        
        // Debug gain informace
        voice.getGainDebugInfo(logger);
        
        // Process jeden blok pro měření výstupní hlasitosti
        memset(leftBuffer, 0, blockSize * sizeof(float));
        memset(rightBuffer, 0, blockSize * sizeof(float));
        
        if (processBlock(leftBuffer, rightBuffer, blockSize)) {
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
            
            logger.log("VoiceManager/runVelocityGainTest", "info", 
                      "Velocity " + std::to_string(velocity) + 
                      " - Peak L: " + std::to_string(peakL) + 
                      ", Peak R: " + std::to_string(peakR) +
                      ", RMS L: " + std::to_string(rmsL) +
                      ", RMS R: " + std::to_string(rmsR) +
                      ", Voice Final Gain: " + std::to_string(voice.getFinalGain()));
                      
            // Očekávaný růst hlasitosti s velocity
            float expectedGain = std::sqrt(static_cast<float>(velocity) / 127.0f) * voice.getMasterGain();
            logger.log("VoiceManager/runVelocityGainTest", "info", 
                      "Expected gain: " + std::to_string(expectedGain) + 
                      ", Actual velocity gain: " + std::to_string(voice.getVelocityGain()));
        }
        
        // Note-off
        setNoteState(testMidi, false, 0);
        
        // Krátká pauza pro cleanup
        for (int i = 0; i < 3; ++i) {
            processBlock(leftBuffer, rightBuffer, blockSize);
        }
    }
    
    delete[] leftBuffer;
    delete[] rightBuffer;
    
    logger.log("VoiceManager/runVelocityGainTest", "info", 
              "=== VELOCITY GAIN TEST COMPLETED ===");
}

/**
 * @brief NOVÁ metoda: Test master gain nastavení
 */
void VoiceManager::runMasterGainTest(Logger& logger) {
    if (!systemInitialized_) {
        logger.log("VoiceManager/runMasterGainTest", "error", 
                  "System not initialized. Call loadAllInstruments() first.");
        return;
    }
    
    logger.log("VoiceManager/runMasterGainTest", "info", 
              "=== STARTING MASTER GAIN TEST ===");
    
    uint8_t testMidi = findValidTestMidiNote(logger);
    Voice& voice = getVoice(testMidi);
    
    // Test různých master gain hodnot
    std::vector<float> testGains = {0.1f, 0.3f, 0.5f, 0.8f, 1.0f};
    
    for (float masterGain : testGains) {
        logger.log("VoiceManager/runMasterGainTest", "info", 
                  "Testing master gain " + std::to_string(masterGain));
        
        // Nastavení master gain
        voice.setMasterGain(masterGain, logger);
        
        // Test s pevnou velocity (100)
        setNoteState(testMidi, true, 100);
        
        // Debug informace
        voice.getGainDebugInfo(logger);
        
        // Krátký test
        const int blockSize = 256;
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        memset(leftBuffer, 0, blockSize * sizeof(float));
        memset(rightBuffer, 0, blockSize * sizeof(float));
        
        if (processBlock(leftBuffer, rightBuffer, blockSize)) {
            float peakL = 0.0f;
            for (int i = 0; i < blockSize; ++i) {
                peakL = std::max(peakL, std::abs(leftBuffer[i]));
            }
            
            logger.log("VoiceManager/runMasterGainTest", "info", 
                      "Master gain " + std::to_string(masterGain) + 
                      " → Peak output: " + std::to_string(peakL));
        }
        
        delete[] leftBuffer;
        delete[] rightBuffer;
        
        setNoteState(testMidi, false, 0);
    }
    
    logger.log("VoiceManager/runMasterGainTest", "info", 
              "=== MASTER GAIN TEST COMPLETED ===");
}

/**
 * @brief ROZŠÍŘENÝ runSingleNoteTest s gain analýzou
 */
void VoiceManager::runSingleNoteTestWithGain(Logger& logger) {
    if (!systemInitialized_) {
        logger.log("VoiceManager/runSingleNoteTestWithGain", "error", 
                  "System not initialized. Call loadAllInstruments() first.");
        return;
    }
    
    logger.log("VoiceManager/runSingleNoteTestWithGain", "info", 
              "=== STARTING ENHANCED SINGLE NOTE TEST ===");
    
    uint8_t testMidi = findValidTestMidiNote(logger);
    uint8_t testVelocity = 100;
    
    logger.log("VoiceManager/runSingleNoteTestWithGain", "info", 
              "Testing MIDI " + std::to_string(testMidi) + 
              " with velocity " + std::to_string(testVelocity) + 
              " (enhanced gain monitoring)");
    
    Voice& voice = getVoice(testMidi);
    
    // Note-on
    setNoteState(testMidi, true, testVelocity);
    
    // Detailní gain monitoring
    logger.log("VoiceManager/runSingleNoteTestWithGain", "info", 
              "After note-on: " + voice.getGainDebugInfo(logger));
    
    const int blockSize = 512;
    const int numBlocks = 8;  // Více bloků pro sledování envelope
    float* leftBuffer = new float[blockSize];
    float* rightBuffer = new float[blockSize];
    
    for (int block = 0; block < numBlocks; ++block) {
        memset(leftBuffer, 0, blockSize * sizeof(float));
        memset(rightBuffer, 0, blockSize * sizeof(float));
        
        bool hasAudio = processBlock(leftBuffer, rightBuffer, blockSize);
        
        if (hasAudio) {
            float maxL = 0.0f;
            for (int i = 0; i < blockSize; ++i) {
                maxL = std::max(maxL, std::abs(leftBuffer[i]));
            }
            
            logger.log("VoiceManager/runSingleNoteTestWithGain", "info", 
                      "Block " + std::to_string(block) + 
                      " - Peak: " + std::to_string(maxL) + 
                      ", Voice gains: Env=" + std::to_string(voice.getCurrentEnvelopeGain()) +
                      ", Vel=" + std::to_string(voice.getVelocityGain()) +
                      ", Final=" + std::to_string(voice.getFinalGain()));
        }
        
        // Release po polovině bloků
        if (block == numBlocks / 2) {
            setNoteState(testMidi, false, 0);
            logger.log("VoiceManager/runSingleNoteTestWithGain", "info", 
                      "Note-off sent - monitoring release envelope");
        }
    }
    
    delete[] leftBuffer;
    delete[] rightBuffer;
    
    logger.log("VoiceManager/runSingleNoteTestWithGain", "info", 
              "=== ENHANCED SINGLE NOTE TEST COMPLETED ===");
}

// ===== PŮVODNÍ TESTING METHODS - adaptované pro novou Voice třídu =====

/**
 * @brief Single note test s live export možností
 */
void VoiceManager::runSingleNoteTest(Logger& logger) {
    if (!systemInitialized_) {
        logger.log("VoiceManager/runSingleNoteTest", "error", 
                  "System not initialized. Call loadAllInstruments() first.");
        return;
    }
    
    logger.log("VoiceManager/runSingleNoteTest", "info", "Starting single note test...");
    
    uint8_t testMidi = findValidTestMidiNote(logger);
    uint8_t testVelocity = 100;
    
    logger.log("VoiceManager/runSingleNoteTest", "info", 
              "Testing MIDI " + std::to_string(testMidi) + " with velocity " + std::to_string(testVelocity));
    
    // Note-on
    setNoteState(testMidi, true, testVelocity);
    logger.log("VoiceManager/runSingleNoteTest", "info", 
              "Note-on sent. Active voices: " + std::to_string(getActiveVoicesCount()));
    
    // Simulate audio processing (shorter for testing)
    const int blockSize = 512;
    const int numBlocks = 5;
    
    for (int block = 0; block < numBlocks; ++block) {
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        bool hasAudio = processBlock(leftBuffer, rightBuffer, blockSize);
        
        if (hasAudio) {
            float maxL = 0.0f, maxR = 0.0f;
            for (int i = 0; i < blockSize; ++i) {
                maxL = std::max(maxL, std::abs(leftBuffer[i]));
                maxR = std::max(maxR, std::abs(rightBuffer[i]));
            }
            
            logger.log("VoiceManager/runSingleNoteTest", "info", 
                      "Block " + std::to_string(block) + " - Peak L: " + std::to_string(maxL) + 
                      ", Peak R: " + std::to_string(maxR));
        } else {
            logger.log("VoiceManager/runSingleNoteTest", "info", 
                      "Block " + std::to_string(block) + " - Silent");
        }
        
        delete[] leftBuffer;
        delete[] rightBuffer;
    }
    
    // Note-off
    setNoteState(testMidi, false, 0);
    logger.log("VoiceManager/runSingleNoteTest", "info", "Single note test completed");
    
    // Live export
    logger.log("VoiceManager/runSingleNoteTest", "info", 
              "Running live export for single note test...");
    exportSingleNotePlayback(testMidi, testVelocity, "./exports", logger);
}

/**
 * @brief Polyphony test s live export možností
 */
void VoiceManager::runPolyphonyTest(Logger& logger) {
    if (!systemInitialized_) {
        logger.log("VoiceManager/runPolyphonyTest", "error", 
                  "System not initialized. Call loadAllInstruments() first.");
        return;
    }
    
    logger.log("VoiceManager/runPolyphonyTest", "info", "Starting polyphony test...");
    
    std::vector<uint8_t> testNotes = findValidNotesForPolyphony(logger, 2);
    
    if (testNotes.size() >= 2) {
        // Note-on for all notes
        for (uint8_t note : testNotes) {
            setNoteState(note, true, 90);
            logger.log("VoiceManager/runPolyphonyTest", "info", 
                      "Note-on MIDI " + std::to_string(note));
        }
        
        logger.log("VoiceManager/runPolyphonyTest", "info", 
                  "Active voices after chord: " + std::to_string(getActiveVoicesCount()));
        
        // Process one block for testing
        const int blockSize = 512;
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        if (processBlock(leftBuffer, rightBuffer, blockSize)) {
            float maxL = 0.0f, maxR = 0.0f;
            for (int i = 0; i < blockSize; ++i) {
                maxL = std::max(maxL, std::abs(leftBuffer[i]));
                maxR = std::max(maxR, std::abs(rightBuffer[i]));
            }
            
            logger.log("VoiceManager/runPolyphonyTest", "info", 
                      "Polyphonic audio - Peak L: " + std::to_string(maxL) + 
                      ", Peak R: " + std::to_string(maxR));
        }
        
        delete[] leftBuffer;
        delete[] rightBuffer;
        
        // Note-off for all
        for (uint8_t note : testNotes) {
            setNoteState(note, false, 0);
        }
        
        // Live export polyphony
        logger.log("VoiceManager/runPolyphonyTest", "info", 
                  "Running live export for polyphony test...");
        exportPolyphonyPlayback(testNotes, "./exports", logger);
    } else {
        logger.log("VoiceManager/runPolyphonyTest", "warn", 
                  "Not enough valid notes for polyphony test");
    }
    
    logger.log("VoiceManager/runPolyphonyTest", "info", "Polyphony test completed");
}

/**
 * @brief Edge case tests
 */
void VoiceManager::runEdgeCaseTests(Logger& logger) {
    logger.log("VoiceManager/runEdgeCaseTests", "info", "Starting edge case tests...");
    
    // Invalid MIDI notes
    setNoteState(200, true, 100);  // Invalid MIDI
    setNoteState(60, true, 0);     // Zero velocity
    setNoteState(61, true, 127);   // Max velocity
    
    logger.log("VoiceManager/runEdgeCaseTests", "info", 
              "Active voices after edge cases: " + std::to_string(getActiveVoicesCount()));
    
    stopAllVoices();
    logger.log("VoiceManager/runEdgeCaseTests", "info", "Edge case tests completed");
}

/**
 * @brief Individual voice test
 */
void VoiceManager::runIndividualVoiceTest(Logger& logger) {
    if (!systemInitialized_) {
        logger.log("VoiceManager/runIndividualVoiceTest", "error", 
                  "System not initialized. Call loadAllInstruments() first.");
        return;
    }
    
    logger.log("VoiceManager/runIndividualVoiceTest", "info", "Starting individual voice test...");
    
    uint8_t testMidi = findValidTestMidiNote(logger);
    Voice& voice = getVoice(testMidi);
    
    logger.log("VoiceManager/runIndividualVoiceTest", "info", 
              "Voice " + std::to_string(testMidi) + " state: " + 
              std::to_string(static_cast<int>(voice.getState())) + 
              ", position: " + std::to_string(voice.getPosition()) + 
              ", envelope gain: " + std::to_string(voice.getCurrentEnvelopeGain()) +
              ", velocity gain: " + std::to_string(voice.getVelocityGain()) +
              ", master gain: " + std::to_string(voice.getMasterGain()) +
              ", final gain: " + std::to_string(voice.getFinalGain()));
    
    logger.log("VoiceManager/runIndividualVoiceTest", "info", "Individual voice test completed");
}

/**
 * @brief Export test sample (původní raw sample data)
 */
void VoiceManager::exportTestSample(uint8_t midi, uint8_t vel, const std::string& exportDir, Logger& logger) {
    if (!systemInitialized_) {
        logger.log("VoiceManager/exportTestSample", "error", 
                  "System not initialized. Call loadAllInstruments() first.");
        return;
    }
    
    logger.log("VoiceManager/exportTestSample", "info", 
              "Starting export test for MIDI " + std::to_string(midi) + 
              " velocity " + std::to_string(vel));
    
    try {
        const Instrument& instrument = instrumentLoader_.getInstrumentNote(midi);
        if (instrument.velocityExists[vel]) {
            WavExporter exporter(exportDir, logger);
            std::string filename = "test_export_midi" + std::to_string(midi) + "_vel" + std::to_string(vel) + ".wav";
            
            float* exportBuffer = exporter.wavFileCreate(filename, currentSampleRate_, 512, true, true);
            if (exportBuffer) {
                sf_count_t totalFrames = instrument.get_frame_count(vel);
                sf_count_t framesPerBuffer = 512;
                sf_count_t remainingFrames = totalFrames;
                float* sourceData = instrument.get_sample_begin_pointer(vel);
                
                while (remainingFrames > 0) {
                    sf_count_t thisBufferFrames = std::min(framesPerBuffer, remainingFrames);
                    size_t offset = (totalFrames - remainingFrames) * 2;
                    
                    for (sf_count_t i = 0; i < thisBufferFrames * 2; ++i) {
                        exportBuffer[i] = sourceData[offset + i];
                    }
                    
                    if (!exporter.wavFileWriteBuffer(exportBuffer, static_cast<int>(thisBufferFrames))) {
                        logger.log("VoiceManager/exportTestSample", "error", "Export write failed");
                        return;
                    }
                    
                    remainingFrames -= thisBufferFrames;
                }
                
                logger.log("VoiceManager/exportTestSample", "info", 
                          "Export completed: " + filename);
            }
        } else {
            logger.log("VoiceManager/exportTestSample", "warn", 
                      "No sample found for MIDI " + std::to_string(midi) + " velocity " + std::to_string(vel));
        }
    } catch (...) {
        logger.log("VoiceManager/exportTestSample", "error", "Export failed with exception");
    }
}

/**
 * @brief Export live single note playback
 */
void VoiceManager::exportSingleNotePlayback(uint8_t midi, uint8_t velocity, 
                                           const std::string& exportDir, Logger& logger) {
    if (!systemInitialized_) {
        logger.log("VoiceManager/exportSingleNotePlayback", "error", 
                  "System not initialized. Call loadAllInstruments() first.");
        return;
    }
    
    logger.log("VoiceManager/exportSingleNotePlayback", "info", 
              "Starting live export for MIDI " + std::to_string(midi) + 
              " velocity " + std::to_string(velocity));
    
    try {
        WavExporter exporter(exportDir, logger);
        std::string filename = "live_single_midi" + std::to_string(midi) + 
                              "_vel" + std::to_string(velocity) + ".wav";
        
        const int blockSize = 512;
        float* exportBuffer = exporter.wavFileCreate(filename, currentSampleRate_, blockSize, true, true);
        
        if (!exportBuffer) {
            logger.log("VoiceManager/exportSingleNotePlayback", "error", "Failed to create export buffer");
            return;
        }
        
        // Temporary buffers for audio processing
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        // Start note
        setNoteState(midi, true, velocity);
        logger.log("VoiceManager/exportSingleNotePlayback", "info", "Note-on sent, capturing audio...");
        
        // Capture sustain phase (approximately 2 seconds)
        const int sustainBlocks = (2 * currentSampleRate_) / blockSize;
        for (int block = 0; block < sustainBlocks; ++block) {
            if (processBlock(leftBuffer, rightBuffer, blockSize)) {
                // Interleave for export
                for (int i = 0; i < blockSize; ++i) {
                    exportBuffer[i * 2] = leftBuffer[i];
                    exportBuffer[i * 2 + 1] = rightBuffer[i];
                }
                
                if (!exporter.wavFileWriteBuffer(exportBuffer, blockSize)) {
                    logger.log("VoiceManager/exportSingleNotePlayback", "error", "Export write failed");
                    break;
                }
            } else {
                // Silent block - still export zeros
                memset(exportBuffer, 0, blockSize * 2 * sizeof(float));
                exporter.wavFileWriteBuffer(exportBuffer, blockSize);
            }
        }
        
        // Release note
        setNoteState(midi, false, 0);
        logger.log("VoiceManager/exportSingleNotePlayback", "info", "Note-off sent, capturing release...");
        
        // Capture release phase (approximately 1 second)
        const int releaseBlocks = currentSampleRate_ / blockSize;
        for (int block = 0; block < releaseBlocks; ++block) {
            if (processBlock(leftBuffer, rightBuffer, blockSize)) {
                for (int i = 0; i < blockSize; ++i) {
                    exportBuffer[i * 2] = leftBuffer[i];
                    exportBuffer[i * 2 + 1] = rightBuffer[i];
                }
                
                if (!exporter.wavFileWriteBuffer(exportBuffer, blockSize)) {
                    break;
                }
            } else {
                // Voice finished
                break;
            }
        }
        
        delete[] leftBuffer;
        delete[] rightBuffer;
        
        logger.log("VoiceManager/exportSingleNotePlayback", "info", 
                  "Live single note export completed: " + filename);
        
    } catch (...) {
        logger.log("VoiceManager/exportSingleNotePlayback", "error", "Export failed with exception");
    }
}

/**
 * @brief Export live polyphony playback
 */
void VoiceManager::exportPolyphonyPlayback(const std::vector<uint8_t>& midiNotes, 
                                          const std::string& exportDir, Logger& logger) {
    if (!systemInitialized_) {
        logger.log("VoiceManager/exportPolyphonyPlayback", "error", 
                  "System not initialized. Call loadAllInstruments() first.");
        return;
    }
    
    if (midiNotes.empty()) {
        logger.log("VoiceManager/exportPolyphonyPlayback", "warn", "No MIDI notes provided");
        return;
    }
    
    logger.log("VoiceManager/exportPolyphonyPlayback", "info", 
              "Starting live polyphony export for " + std::to_string(midiNotes.size()) + " notes");
    
    try {
        WavExporter exporter(exportDir, logger);
        std::string filename = "live_polyphony_" + std::to_string(midiNotes.size()) + "notes.wav";
        
        const int blockSize = 512;
        float* exportBuffer = exporter.wavFileCreate(filename, currentSampleRate_, blockSize, true, true);
        
        if (!exportBuffer) {
            logger.log("VoiceManager/exportPolyphonyPlayback", "error", "Failed to create export buffer");
            return;
        }
        
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        // Start chord (all notes at once)
        for (uint8_t midi : midiNotes) {
            setNoteState(midi, true, 100);
            logger.log("VoiceManager/exportPolyphonyPlayback", "info", 
                      "Note-on MIDI " + std::to_string(midi));
        }
        
        logger.log("VoiceManager/exportPolyphonyPlayback", "info", 
                  "Chord started, active voices: " + std::to_string(getActiveVoicesCount()));
        
        // Capture chord sustain (approximately 3 seconds)
        const int sustainBlocks = (3 * currentSampleRate_) / blockSize;
        for (int block = 0; block < sustainBlocks; ++block) {
            if (processBlock(leftBuffer, rightBuffer, blockSize)) {
                // Interleave for export
                for (int i = 0; i < blockSize; ++i) {
                    exportBuffer[i * 2] = leftBuffer[i];
                    exportBuffer[i * 2 + 1] = rightBuffer[i];
                }
                
                if (!exporter.wavFileWriteBuffer(exportBuffer, blockSize)) {
                    logger.log("VoiceManager/exportPolyphonyPlayback", "error", "Export write failed");
                    break;
                }
            } else {
                memset(exportBuffer, 0, blockSize * 2 * sizeof(float));
                exporter.wavFileWriteBuffer(exportBuffer, blockSize);
            }
        }
        
        // Release chord (all notes off)
        for (uint8_t midi : midiNotes) {
            setNoteState(midi, false, 0);
        }
        
        logger.log("VoiceManager/exportPolyphonyPlayback", "info", "Chord released, capturing tail...");
        
        // Capture release phase (approximately 2 seconds)
        const int releaseBlocks = (2 * currentSampleRate_) / blockSize;
        for (int block = 0; block < releaseBlocks; ++block) {
            if (processBlock(leftBuffer, rightBuffer, blockSize)) {
                for (int i = 0; i < blockSize; ++i) {
                    exportBuffer[i * 2] = leftBuffer[i];
                    exportBuffer[i * 2 + 1] = rightBuffer[i];
                }
                
                if (!exporter.wavFileWriteBuffer(exportBuffer, blockSize)) {
                    break;
                }
            } else {
                // All voices finished
                break;
            }
        }
        
        delete[] leftBuffer;
        delete[] rightBuffer;
        
        logger.log("VoiceManager/exportPolyphonyPlayback", "info", 
                  "Live polyphony export completed: " + filename);
        
    } catch (...) {
        logger.log("VoiceManager/exportPolyphonyPlayback", "error", "Export failed with exception");
    }
}

// ===== PRIVATE HELPER METHODS =====

/**
 * @brief Inicializace všech voices s načtenými instrumenty
 * @param logger Reference na Logger
 */
void VoiceManager::initializeVoicesWithInstruments(Logger& logger) {
    logger.log("VoiceManager/initializeVoicesWithInstruments", "info", 
              "Initializing all 128 voices with loaded instruments...");
    
    for (int i = 0; i < 128; ++i) {
        uint8_t midiNote = static_cast<uint8_t>(i);
        const Instrument& inst = instrumentLoader_.getInstrumentNote(midiNote);
        Voice& voice = voices_[i];
        
        // Initialize voice s odpovídajícím instrumentem
        voice.initialize(inst, currentSampleRate_, logger);
        
        // NOVÉ: Prepare voice pro current buffer size (reasonable default)
        voice.prepareToPlay(512); // Will be updated by explicit prepareToPlay() call
    }
    
    logger.log("VoiceManager/initializeVoicesWithInstruments", "info", 
              "All voices initialized with instruments successfully");
}

/**
 * @brief Helper: Check if reinitialization needed
 */
bool VoiceManager::needsReinitialization(int targetSampleRate) const noexcept {
    // Debug logging pro diagnostiku
    bool currentMismatch = (currentSampleRate_ != targetSampleRate);
    bool loaderMismatch = (instrumentLoader_.getActualSampleRate() != targetSampleRate);
    
    // Pro debugging - toto není RT-safe, ale pro diagnostiku OK
    if (currentMismatch || loaderMismatch) {
        // Pokud je potřeba reinicializace, debug info se vypíše v changeSampleRate()
        return true;
    }
    
    return false;
}

/**
 * @brief Helper: Reinitialize if needed
 */
void VoiceManager::reinitializeIfNeeded(int targetSampleRate, Logger& logger) {
    if (needsReinitialization(targetSampleRate)) {
        changeSampleRate(targetSampleRate, logger);
    }
}

/**
 * @brief Helper: Find valid test MIDI note
 */
uint8_t VoiceManager::findValidTestMidiNote(Logger& logger) const {
    for (int midi = 60; midi <= 80; ++midi) {
        if (systemInitialized_) {
            const Instrument& inst = instrumentLoader_.getInstrumentNote(midi);
            for (int vel = 0; vel < 8; ++vel) {
                if (inst.velocityExists[vel]) {
                    logger.log("VoiceManager/findValidTestMidiNote", "info", 
                              "Found valid test note: MIDI " + std::to_string(midi));
                    return static_cast<uint8_t>(midi);
                }
            }
        }
    }
    logger.log("VoiceManager/findValidTestMidiNote", "warn", 
              "No valid test note found, using default MIDI 60");
    return 60; // fallback
}

/**
 * @brief Helper: Find valid notes for polyphony
 */
std::vector<uint8_t> VoiceManager::findValidNotesForPolyphony(Logger& logger, int maxNotes) const {
    std::vector<uint8_t> notes;
    for (int midi = 70; midi <= 80 && notes.size() < maxNotes; ++midi) {
        if (systemInitialized_) {
            const Instrument& inst = instrumentLoader_.getInstrumentNote(midi);
            for (int vel = 0; vel < 8; ++vel) {
                if (inst.velocityExists[vel]) {
                    notes.push_back(static_cast<uint8_t>(midi));
                    logger.log("VoiceManager/findValidNotesForPolyphony", "info", 
                              "Added MIDI " + std::to_string(midi) + " for polyphony test");
                    break;
                }
            }
        }
    }
    return notes;
}

/**
 * @brief NON-RT SAFE: Safe logging wrapper
 */
void VoiceManager::logSafe(const std::string& component, const std::string& severity, 
                          const std::string& message, Logger& logger) const {
    if (!rtMode_.load()) {
        logger.log(component, severity, message);
    }
}

// ===== PŮVODNÍ METHODS IMPLEMENTATION - UPRAVENÉ PRO NOVOU VOICE TŘÍDU =====

void VoiceManager::setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity) noexcept {
    if (!isValidMidiNote(midiNote)) {
        return;
    }
    
    Voice& voice = voices_[midiNote];
    
    if (isOn) {
        if (!voice.isActive()) {
            addActiveVoice(&voice);
        }
        voice.setNoteState(true, velocity);
    } else {
        voice.setNoteState(false, velocity);
    }
}

/**
 * @brief KRITICKÁ ÚPRAVA: processBlock s předáváním active voice count pro dynamic scaling
 */
bool VoiceManager::processBlock(float* outputLeft, float* outputRight, int numSamples) noexcept {
    if (!outputLeft || !outputRight || numSamples <= 0) {
        return false;
    }
    
    // KRITICKÉ: Clear output buffers first - refaktorovaná Voice používá += operace
    memset(outputLeft, 0, numSamples * sizeof(float));
    memset(outputRight, 0, numSamples * sizeof(float));
    
    if (activeVoices_.empty()) {
        return false;
    }
    
    bool anyActive = false;
    
    // NOVÉ: Get active voice count for dynamic scaling
    const int activeCount = static_cast<int>(activeVoices_.size());
    
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            // KLÍČOVÁ ZMĚNA: Pass activeCount for dynamic gain scaling
            if (voice->processBlock(outputLeft, outputRight, numSamples, activeCount)) {
                anyActive = true;
            } else {
                voicesToRemove_.push_back(voice);
            }
        } else {
            voicesToRemove_.push_back(voice);
        }
    }
    
    if (!voicesToRemove_.empty()) {
        cleanupInactiveVoices();
    }
    
    return anyActive;
}

/**
 * @brief UPRAVENÉ: AudioData processBlock variant
 */
bool VoiceManager::processBlock(AudioData* outputBuffer, int numSamples) noexcept {
    if (!outputBuffer || numSamples <= 0) {
        return false;
    }
    
    // KRITICKÉ: Clear output buffer first
    for (int i = 0; i < numSamples; ++i) {
        outputBuffer[i].left = 0.0f;
        outputBuffer[i].right = 0.0f;
    }
    
    if (activeVoices_.empty()) {
        return false;
    }
    
    bool anyActive = false;
    
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            if (voice->processBlock(outputBuffer, numSamples)) {
                anyActive = true;
            } else {
                voicesToRemove_.push_back(voice);
            }
        } else {
            voicesToRemove_.push_back(voice);
        }
    }
    
    if (!voicesToRemove_.empty()) {
        cleanupInactiveVoices();
    }
    
    return anyActive;
}

void VoiceManager::stopAllVoices() noexcept {
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            voice->setNoteState(false, 0);
        }
    }
}

void VoiceManager::resetAllVoices(Logger& logger) {
    for (int i = 0; i < 128; ++i) {
        voices_[i].cleanup(logger);
    }
    
    activeVoices_.clear();
    voicesToRemove_.clear();
    activeVoicesCount_.store(0);
    
    logSafe("VoiceManager/resetAllVoices", "info", 
           "Reset all 128 voices to idle state", logger);
}

int VoiceManager::getSustainingVoicesCount() const noexcept {
    int count = 0;
    for (const Voice* voice : activeVoices_) {
        if (voice && voice->getState() == VoiceState::Sustaining) {
            ++count;
        }
    }
    return count;
}

int VoiceManager::getReleasingVoicesCount() const noexcept {
    int count = 0;
    for (const Voice* voice : activeVoices_) {
        if (voice && voice->getState() == VoiceState::Releasing) {
            ++count;
        }
    }
    return count;
}

Voice& VoiceManager::getVoice(uint8_t midiNote) noexcept {
    #ifdef DEBUG
    if (!isValidMidiNote(midiNote)) {
        return voices_[0];
    }
    #endif
    
    return voices_[midiNote];
}

const Voice& VoiceManager::getVoice(uint8_t midiNote) const noexcept {
    #ifdef DEBUG
    if (!isValidMidiNote(midiNote)) {
        return voices_[0];
    }
    #endif
    
    return voices_[midiNote];
}

void VoiceManager::setRealTimeMode(bool enabled) noexcept {
    rtMode_.store(enabled);
    Voice::setRealTimeMode(enabled);
}

void VoiceManager::addActiveVoice(Voice* voice) noexcept {
    if (!voice) return;
    
    auto it = std::find(activeVoices_.begin(), activeVoices_.end(), voice);
    if (it == activeVoices_.end()) {
        activeVoices_.push_back(voice);
        activeVoicesCount_.fetch_add(1);
    }
}

void VoiceManager::removeActiveVoice(Voice* voice) noexcept {
    if (!voice) return;
    
    auto it = std::find(activeVoices_.begin(), activeVoices_.end(), voice);
    if (it != activeVoices_.end()) {
        activeVoices_.erase(it);
        activeVoicesCount_.fetch_sub(1);
    }
}

void VoiceManager::cleanupInactiveVoices() noexcept {
    for (Voice* voice : voicesToRemove_) {
        removeActiveVoice(voice);
    }
    
    voicesToRemove_.clear();
}