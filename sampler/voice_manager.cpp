#include "voice_manager.h"
#include "sampler.h"            // Pro stack allocated SamplerIO
#include "instrument_loader.h"  // Pro stack allocated InstrumentLoader
#include "envelopes/envelope.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

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
 * @brief Nastavení sample rate - deleguje na FÁZI 2
 */
void VoiceManager::changeSampleRate(int newSampleRate, Logger& logger) {
    logger.log("VoiceManager/changeSampleRate", "info", 
              "Requested sample rate change to " + std::to_string(newSampleRate) + " Hz");
    
    if (currentSampleRate_ == newSampleRate) {
        logger.log("VoiceManager/changeSampleRate", "info", 
                  "Sample rate unchanged: " + std::to_string(newSampleRate) + " Hz");
        return;
    }
    
    // Stop všechny aktivní voices před změnou
    stopAllVoices();
    
    // Delegace na FÁZI 2 - načtení pro nový sample rate
    loadForSampleRate(newSampleRate, logger);
    
    logger.log("VoiceManager/changeSampleRate", "info", 
              "Sample rate successfully changed to " + std::to_string(newSampleRate) + " Hz");
}


/**
 * @brief FÁZE 3: JUCE příprava - buffer sizes pro všechny voices
 */
void VoiceManager::prepareToPlay(int maxBlockSize) noexcept {
    // Prepare all 128 voices for new buffer size
    for (int i = 0; i < 128; ++i) {
        voices_[i].prepareToPlay(maxBlockSize);
    }
    
    // Žádné logování - metoda je noexcept a RT-safe
    // Logger se předává jen do non-RT metod jako parametr
}

/**
 * @brief INIT FÁZE 1: Jednorázová inicializace - skenování adresáře + generování envelope dat
 */
void VoiceManager::initializeSystem(Logger& logger) {

    logger.log("VoiceManager/initializeSystem", "info", 
              "INIT PHASE 1: System initialization ===");
    logger.log("VoiceManager/initializeSystem", "info", 
              "Scanning directory: " + sampleDir_);
    
    try {
        // Krok 1: Skenování adresáře (najde všechny soubory pro všechny frekvence)
        samplerIO_.scanSampleDirectory(sampleDir_, logger);
        
        // Krok 2: Generování envelope dat (pro všechny frekvence)
        logger.log("VoiceManager/initializeSystem", "info", 
                  "Initializing envelope system...");
        envelope_.initialize(logger);
        
        logger.log("VoiceManager/initializeSystem", "info", 
                  "=== INIT PHASE 1: Completed successfully ===");
        
    } catch (...) {
        logger.log("VoiceManager/initializeSystem", "error", 
                  "INIT PHASE 1: System initialization failed. Terminating.");
        std::exit(1);
    }
}

/**
 * @brief FÁZE 2: Načtení dat + přepnutí envelope pro konkrétní sample rate
 */
void VoiceManager::loadForSampleRate(int sampleRate, Logger& logger) {
    logger.log("VoiceManager/loadForSampleRate", "info", 
              "=== INIT PHASE 2: Loading for sample rate " + std::to_string(sampleRate) + " Hz ===");
    
    // Validace sample rate
    if (sampleRate != 44100 && sampleRate != 48000) {
        logger.log("VoiceManager/loadForSampleRate", "error", 
                  "Invalid sample rate " + std::to_string(sampleRate) + 
                  " Hz - only 44100 Hz and 48000 Hz are supported. Terminating.");
        std::exit(1);
    }
    
    try {
        // Krok 1: Nastavení nového sample rate
        currentSampleRate_ = sampleRate;
        
        // Krok 2: Přepnutí envelope na novou frekvenci
        envelope_.setEnvelopeFrequency(sampleRate, logger);
        
        // Krok 3: Načtení samples pro danou frekvenci
        instrumentLoader_.loadInstrumentData(samplerIO_, sampleRate, logger);
        
        // Krok 4: Inicializace všech voices s novými daty
        initializeVoicesWithInstruments(logger);
        
        // Krok 5: Validace integrity
        instrumentLoader_.validateStereoConsistency(logger);
        
        systemInitialized_ = true;
        
        logger.log("VoiceManager/loadForSampleRate", "info", 
                  "=== INIT PHASE 2: Completed successfully ===");
        
    } catch (...) {
        logger.log("VoiceManager/loadForSampleRate", "error", 
                  "INIT PHASE 2: Loading failed. Terminating.");
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
    for (int i = 0; i < numSamples; ++i) {                          // vynyluje cely audio buffer podle nastavene velikosti
        outputBuffer[i].left = 0.0f;                                // nuluje levy kanal
        outputBuffer[i].right = 0.0f;                               // nuluje pravy kanal
    }
    
    if (activeVoices_.empty()) {                                    // nejsou aktivni voicy
        return false;                                               // nic nezpracovavame a vracime se
    }
    
    bool anyActive = false;                                         // vynulujeme anyActive - pokud nektery voice bude aktivni, prepismev loop cyklu anyActive na true 
    
    for (Voice* voice : activeVoices_) {                            // projdi vsechny  voices
        if (voice && voice->isActive()) {                           // voice je aktivni, pozadame instanci voice o blok audio dat
            if (voice->processBlock(outputBuffer, numSamples)) {    // voice vratil data (stale hraje):
                anyActive = true;                                   //                    prepiseme anyActive na true
            } else {                                                // voice nevratil data:
                voicesToRemove_.push_back(voice);                   //                    pridame jej na seznam hlasu k odebrani
            }
        } else {
            voicesToRemove_.push_back(voice);                       // pridama jej na seznam hlasu k odebrani
        }
    }
    
    if (!voicesToRemove_.empty()) {                                 // pokud seznamhlasu k odebrani neni prazdny
        cleanupInactiveVoices();                                    // volame vycisteni neaktivnich hlasu
    }
    
    return anyActive;                                               // vratime stav - existuji nebo neexistuji aktivni hlasy
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
              "Initializing all 128 voices with loaded instruments and envelope system...");
    
    for (int i = 0; i < 128; ++i) {
        uint8_t midiNote = static_cast<uint8_t>(i);
        const Instrument& inst = instrumentLoader_.getInstrumentNote(midiNote);
        Voice& voice = voices_[i];
        
        // AKTUALIZOVÁNO: Initialize voice s odpovídajícím instrumentem A envelope
        voice.initialize(inst, currentSampleRate_, envelope_, logger);
        
        // Prepare voice pro current buffer size
        voice.prepareToPlay(512);
    }
    
    logger.log("VoiceManager/initializeVoicesWithInstruments", "info", 
              "All voices initialized with instruments and envelope system successfully");
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