#include "voice_manager.h"
#include "sampler.h"            // Pro stack allocated SamplerIO
#include "instrument_loader.h"  // Pro stack allocated InstrumentLoader
#include "envelopes/envelope.h"
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
      envelope_(),               // Stack allocated Envelope - prazdny stav
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

        //  Update envelope sample rate pokud je inicializováno
        if (systemInitialized_) {
            envelope_.setEnvelopeFrequency(newSampleRate, logger);
            logger.log("VoiceManager/changeSampleRate", "info", 
                      "Envelope frequency updated to " + std::to_string(newSampleRate) + " Hz");
        }
        
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

        // Inicializace envelope systému
        logger.log("VoiceManager/initializeSystem", "info", 
                  "Initializing envelope system...");
        envelope_.initialize(logger);
        envelope_.setEnvelopeFrequency(currentSampleRate_, logger);
        
        // Inicializace hotova
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

// ===== CORE AUDIO API IMPLEMENTATION =====

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

/**
 * @brief NON-RT SAFE: Safe logging wrapper
 */
void VoiceManager::logSafe(const std::string& component, const std::string& severity, 
                          const std::string& message, Logger& logger) const {
    if (!rtMode_.load()) {
        logger.log(component, severity, message);
    }
}