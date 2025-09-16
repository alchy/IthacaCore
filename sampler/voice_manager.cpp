#include "voice_manager.h"
#include "sampler.h"
#include "instrument_loader.h"
#include "envelopes/envelope_static_data.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

/**
 * @brief Konstruktor - automaticky používá sdílená statická envelope data
 */
VoiceManager::VoiceManager(const std::string& sampleDir, Logger& logger)
    : samplerIO_(),
      instrumentLoader_(),
      envelope_(),                // Per-instance envelope state (jen MIDI indexy a sustain level)
      currentSampleRate_(0),
      sampleDir_(sampleDir),
      systemInitialized_(false),
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
    
    // KLÍČOVÁ VALIDACE: Kontrola inicializace statických dat
    if (!EnvelopeStaticData::isInitialized()) {
        logSafe("VoiceManager/constructor", "error", 
               "EnvelopeStaticData not initialized. Call EnvelopeStaticData::initialize() first. Terminating.", logger);
        std::exit(1);
    }
    
    // Původní inicializace voices
    voices_.resize(128);
    for (int i = 0; i < 128; ++i) {
        voices_[i] = Voice(static_cast<uint8_t>(i));
    }
    
    activeVoices_.reserve(128);
    voicesToRemove_.reserve(128);

    // DŮLEŽITÉ: Setup error callback pro statická envelope data (RT error handling)
    EnvelopeStaticData::setErrorCallback([&logger](const std::string& component, 
                                                   const std::string& severity, 
                                                   const std::string& message) {
        logger.log(component, severity, message);
    });
    
    logSafe("VoiceManager/constructor", "info", 
           "VoiceManager created with sampleDir '" + sampleDir_ + 
           "' using shared envelope data. Ready for initialization pipeline.", logger);
}

void VoiceManager::changeSampleRate(int newSampleRate, Logger& logger) {
    logger.log("VoiceManager/changeSampleRate", "info", 
              "Requested sample rate change to " + std::to_string(newSampleRate) + " Hz");
    
    if (currentSampleRate_ == newSampleRate) {
        logger.log("VoiceManager/changeSampleRate", "info", 
                  "Sample rate unchanged: " + std::to_string(newSampleRate) + " Hz");
        return;
    }
    
    stopAllVoices();
    loadForSampleRate(newSampleRate, logger);
    
    logger.log("VoiceManager/changeSampleRate", "info", 
              "Sample rate successfully changed to " + std::to_string(newSampleRate) + " Hz");
}

void VoiceManager::prepareToPlay(int maxBlockSize) noexcept {
    for (int i = 0; i < 128; ++i) {
        voices_[i].prepareToPlay(maxBlockSize);
    }
}

void VoiceManager::initializeSystem(Logger& logger) {
    logger.log("VoiceManager/initializeSystem", "info", 
              "INIT PHASE 1: System initialization ===");
    logger.log("VoiceManager/initializeSystem", "info", 
              "Scanning directory: " + sampleDir_);
    
    try {
        // Pouze skenování adresáře - envelope data už jsou globálně inicializována!
        samplerIO_.scanSampleDirectory(sampleDir_, logger);
        
        logger.log("VoiceManager/initializeSystem", "info", 
                  "=== INIT PHASE 1: Completed successfully (envelope data shared) ===");
        
    } catch (...) {
        logger.log("VoiceManager/initializeSystem", "error", 
                  "INIT PHASE 1: System initialization failed. Terminating.");
        std::exit(1);
    }
}

void VoiceManager::loadForSampleRate(int sampleRate, Logger& logger) {
    logger.log("VoiceManager/loadForSampleRate", "info", 
              "=== INIT PHASE 2: Loading for sample rate " + std::to_string(sampleRate) + " Hz ===");
    
    if (sampleRate != 44100 && sampleRate != 48000) {
        logger.log("VoiceManager/loadForSampleRate", "error", 
                  "Invalid sample rate " + std::to_string(sampleRate) + 
                  " Hz - only 44100 Hz and 48000 Hz are supported. Terminating.");
        std::exit(1);
    }
    
    try {
        // KLÍČOVÉ: Pouze nastavení sample rate - statická envelope data už obsahují oba!
        currentSampleRate_ = sampleRate;
        
        // Načtení samples pro danou frekvenci  
        instrumentLoader_.loadInstrumentData(samplerIO_, sampleRate, logger);
        
        // Inicializace všech voices - předáme jim per-instance envelope_ wrapper
        initializeVoicesWithInstruments(logger);
        
        instrumentLoader_.validateStereoConsistency(logger);
        systemInitialized_ = true;
        
        logger.log("VoiceManager/loadForSampleRate", "info", 
                  "=== INIT PHASE 2: Completed successfully (using shared envelope data) ===");
        
    } catch (...) {
        logger.log("VoiceManager/loadForSampleRate", "error", 
                  "INIT PHASE 2: Loading failed. Terminating.");
        std::exit(1);
    }
}

void VoiceManager::logSystemStatistics(Logger& logger) {
    if (!systemInitialized_) {
        logger.log("VoiceManager/logSystemStatistics", "warn", 
                  "System not initialized - limited statistics available");
        return;
    }
    
    const int totalSamples = instrumentLoader_.getTotalLoadedSamples();
    const int monoSamples = instrumentLoader_.getMonoSamplesCount();
    const int stereoSamples = instrumentLoader_.getStereoSamplesCount();
    const int actualSampleRate = instrumentLoader_.getActualSampleRate();
    
    const int activeCount = getActiveVoicesCount();
    const int sustaining = getSustainingVoicesCount();
    const int releasing = getReleasingVoicesCount();
    
    logger.log("VoiceManager/statistics", "info", "=== SYSTEM STATISTICS ===");
    logger.log("VoiceManager/statistics", "info", "Sample Rate: " + std::to_string(actualSampleRate) + " Hz");
    logger.log("VoiceManager/statistics", "info", "Total loaded samples: " + std::to_string(totalSamples));
    logger.log("VoiceManager/statistics", "info", "Originally mono: " + std::to_string(monoSamples) + 
              ", Originally stereo: " + std::to_string(stereoSamples));
    logger.log("VoiceManager/statistics", "info", "Voice Activity - Active: " + std::to_string(activeCount) + 
              ", Sustaining: " + std::to_string(sustaining) + ", Releasing: " + std::to_string(releasing));
    logger.log("VoiceManager/statistics", "info", "========================");
}

// ===== CORE AUDIO API =====

void VoiceManager::setNoteStateMIDI(uint8_t midiNote, bool isOn, uint8_t velocity) noexcept {
    if (!isValidMidiNote(midiNote)) return;
    
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

void VoiceManager::setNoteStateMIDI(uint8_t midiNote, bool isOn) noexcept {
    if (!isValidMidiNote(midiNote)) return;
    
    Voice& voice = voices_[midiNote];
    
    if (isOn) {
        if (!voice.isActive()) {
            addActiveVoice(&voice);
        }
        voice.setNoteState(true);
    } else {
        voice.setNoteState(false);
    }
}

bool VoiceManager::processBlockUninterleaved(float* outputLeft, float* outputRight, int samplesPerBlock) noexcept {
    if (!outputLeft || !outputRight || samplesPerBlock <= 0) return false;
    
    std::fill(outputLeft, outputLeft + samplesPerBlock, 0.0f);
    std::fill(outputRight, outputRight + samplesPerBlock, 0.0f);
    
    if (activeVoices_.empty()) return false;
    
    bool anyActive = false;
    
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            if (voice->processBlock(outputLeft, outputRight, samplesPerBlock)) {
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

bool VoiceManager::processBlockInterleaved(AudioData* outputBuffer, int samplesPerBlock) noexcept {
    if (!outputBuffer || samplesPerBlock <= 0) return false;
    
    for (int i = 0; i < samplesPerBlock; ++i) {
        outputBuffer[i].left = 0.0f;
        outputBuffer[i].right = 0.0f;
    }
    
    if (activeVoices_.empty()) return false;
    
    static thread_local std::vector<float> tempLeft;
    static thread_local std::vector<float> tempRight;
    
    if (tempLeft.size() < static_cast<size_t>(samplesPerBlock)) {
        tempLeft.resize(samplesPerBlock);
        tempRight.resize(samplesPerBlock);
    }
    
    bool anyActive = false;
    
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            std::fill(tempLeft.begin(), tempLeft.begin() + samplesPerBlock, 0.0f);
            std::fill(tempRight.begin(), tempRight.begin() + samplesPerBlock, 0.0f);
            
            if (voice->processBlock(tempLeft.data(), tempRight.data(), samplesPerBlock)) {
                anyActive = true;
                
                for (int i = 0; i < samplesPerBlock; ++i) {
                    outputBuffer[i].left += tempLeft[i];
                    outputBuffer[i].right += tempRight[i];
                }
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
    
    logSafe("VoiceManager/resetAllVoices", "info", "Reset all 128 voices to idle state", logger);
}

int VoiceManager::getSustainingVoicesCount() const noexcept {
    int count = 0;
    for (const Voice* voice : activeVoices_) {
        if (voice && voice->getState() == VoiceState::Sustaining) ++count;
    }
    return count;
}

int VoiceManager::getReleasingVoicesCount() const noexcept {
    int count = 0;
    for (const Voice* voice : activeVoices_) {
        if (voice && voice->getState() == VoiceState::Releasing) ++count;
    }
    return count;
}

Voice& VoiceManager::getVoiceMIDI(uint8_t midiNote) noexcept {
    #ifdef DEBUG
    if (!isValidMidiNote(midiNote)) return voices_[0];
    #endif
    return voices_[midiNote];
}

const Voice& VoiceManager::getVoiceMIDI(uint8_t midiNote) const noexcept {
    #ifdef DEBUG
    if (!isValidMidiNote(midiNote)) return voices_[0];
    #endif
    return voices_[midiNote];
}

void VoiceManager::setRealTimeMode(bool enabled) noexcept {
    rtMode_.store(enabled);
    Voice::setRealTimeMode(enabled);
}

// ===== PRIVATE HELPER METHODS =====

void VoiceManager::initializeVoicesWithInstruments(Logger& logger) {
    logger.log("VoiceManager/initializeVoicesWithInstruments", "info", 
              "Initializing all 128 voices with loaded instruments and shared envelope system...");
    
    for (int i = 0; i < 128; ++i) {
        uint8_t midiNote = static_cast<uint8_t>(i);
        const Instrument& inst = instrumentLoader_.getInstrumentNote(midiNote);
        Voice& voice = voices_[i];
        
        // KLÍČOVÉ: Předáváme per-instance envelope_ wrapper, který interně deleguje na statická data
        voice.initialize(inst, currentSampleRate_, envelope_, logger);
        voice.prepareToPlay(512);
    }
    
    logger.log("VoiceManager/initializeVoicesWithInstruments", "info", 
              "All voices initialized with instruments and shared envelope system successfully");
}

bool VoiceManager::needsReinitialization(int targetSampleRate) const noexcept {
    return (currentSampleRate_ != targetSampleRate) || 
           (instrumentLoader_.getActualSampleRate() != targetSampleRate);
}

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

// ===== GLOBAL VOICE CONTROL =====

void VoiceManager::setAllVoicesMasterGainMIDI(uint8_t midi_gain, Logger& logger) {
    if (midi_gain > 127) {
        logSafe("VoiceManager/setAllVoicesMasterGain", "error", 
               "Invalid master MIDI gain " + std::to_string(midi_gain) + 
               " (must be 0.0-1.0)", logger);
        return;
    }
    
    float gain = midi_gain / 127.0f;

    for (int i = 0; i < 128; ++i) {
        voices_[i].setMasterGain(gain, logger);
    }
    
    logSafe("VoiceManager/setAllVoicesMasterGain", "info", 
           "Master gain set to " + std::to_string(gain) + " for all voices", logger);
}

void VoiceManager::setAllVoicesPanMIDI(uint8_t midi_pan) noexcept {
    // Clamp pan to valid range
    if (midi_pan > 127) {
        return;
    }

    float pan = (midi_pan - 64.0f) / 63.0f;
    
    for (int i = 0; i < 128; ++i) {
        voices_[i].setPan(pan);
    }
}

void VoiceManager::logSafe(const std::string& component, const std::string& severity, 
                          const std::string& message, Logger& logger) const {
    if (!rtMode_.load()) {
        logger.log(component, severity, message);
    }
}