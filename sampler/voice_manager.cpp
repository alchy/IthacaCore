#include "voice_manager.h"
#include <algorithm>  // Pro std::find, std::remove

/**
 * @brief Konstruktor: Vytvoří fixní pool 128 voice, uloží sampleDir, sampleRate.
 */
VoiceManager::VoiceManager(const std::string& sampleDir, int sampleRate, Logger& logger)
    : sampleRate_(sampleRate), sampleDir_(sampleDir) {
    
    if (sampleRate_ <= 0) {
        logSafe("VoiceManager/constructor", "error", 
               "Invalid sampleRate " + std::to_string(sampleRate_) + " - must be > 0. Terminating.", logger);
        std::exit(1);
    }
    
    if (sampleDir_.empty()) {
        logSafe("VoiceManager/constructor", "error", 
               "Invalid sampleDir - cannot be empty. Terminating.", logger);
        std::exit(1);
    }
    
    // Inicializace fixního poolu 128 voices
    voices_.resize(128);
    for (int i = 0; i < 128; ++i) {
        voices_[i] = Voice(static_cast<uint8_t>(i));
    }
    
    // Pre-alokace bufferů pro active voice tracking
    activeVoices_.reserve(128);
    voicesToRemove_.reserve(128);
    
    logSafe("VoiceManager/constructor", "info", 
           "VoiceManager created with fixed 128 voices (one per MIDI note), sampleDir '" + sampleDir_ + 
           "' and sampleRate " + std::to_string(sampleRate_), logger);
}

/**
 * @brief RT-SAFE: Nastaví stav note pro danou MIDI notu.
 */
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
 * @brief RT-SAFE HLAVNÍ METODA: Procesuje audio blok s explicitními stereo buffery.
 */
bool VoiceManager::processBlock(float* outputLeft, float* outputRight, int numSamples) noexcept {
    if (!outputLeft || !outputRight || numSamples <= 0) {
        return false;
    }
    
    // Vynulování výstupních bufferů
    memset(outputLeft, 0, numSamples * sizeof(float));
    memset(outputRight, 0, numSamples * sizeof(float));
    
    if (activeVoices_.empty()) {
        return false;
    }
    
    bool anyActive = false;
    
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            if (voice->processBlock(outputLeft, outputRight, numSamples)) {
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
 * @brief RT-SAFE: Alternativní processBlock s AudioData strukturou.
 */
bool VoiceManager::processBlock(AudioData* outputBuffer, int numSamples) noexcept {
    if (!outputBuffer || numSamples <= 0) {
        return false;
    }
    
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

/**
 * @brief RT-SAFE: Stopne všechny aktivní hlasy (panic button).
 */
void VoiceManager::stopAllVoices() noexcept {
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            voice->setNoteState(false, 0);
        }
    }
}

/**
 * @brief NON-RT SAFE: Resetuje všechny hlasy na idle stav.
 */
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

/**
 * @brief RT-SAFE: Getter pro počet sustaining voices.
 */
int VoiceManager::getSustainingVoicesCount() const noexcept {
    int count = 0;
    for (const Voice* voice : activeVoices_) {
        if (voice && voice->getState() == VoiceState::Sustaining) {
            ++count;
        }
    }
    return count;
}

/**
 * @brief RT-SAFE: Getter pro počet releasing voices.
 */
int VoiceManager::getReleasingVoicesCount() const noexcept {
    int count = 0;
    for (const Voice* voice : activeVoices_) {
        if (voice && voice->getState() == VoiceState::Releasing) {
            ++count;
        }
    }
    return count;
}

/**
 * @brief RT-SAFE: Getter pro referenci na konkrétní voice.
 */
Voice& VoiceManager::getVoice(uint8_t midiNote) noexcept {
    #ifdef DEBUG
    if (!isValidMidiNote(midiNote)) {
        return voices_[0];
    }
    #endif
    
    return voices_[midiNote];
}

/**
 * @brief RT-SAFE: Const getter pro referenci na konkrétní voice.
 */
const Voice& VoiceManager::getVoice(uint8_t midiNote) const noexcept {
    #ifdef DEBUG
    if (!isValidMidiNote(midiNote)) {
        return voices_[0];
    }
    #endif
    
    return voices_[midiNote];
}

/**
 * @brief RT-SAFE: Nastaví RT mode pro všechny voices.
 */
void VoiceManager::setRealTimeMode(bool enabled) noexcept {
    rtMode_.store(enabled);
    Voice::setRealTimeMode(enabled);
}

/**
 * @brief NON-RT SAFE: Získá detailní statistiky pro debugging.
 */
void VoiceManager::logStatistics(Logger& logger) const {
    const int active = getActiveVoicesCount();
    const int sustaining = getSustainingVoicesCount();
    const int releasing = getReleasingVoicesCount();
    
    logSafe("VoiceManager/statistics", "info", 
           "Voice Statistics - Active: " + std::to_string(active) +
           ", Sustaining: " + std::to_string(sustaining) +
           ", Releasing: " + std::to_string(releasing) +
           ", Max: 128", logger);
}

// ===== PRIVATE OPTIMALIZOVANÉ RT-SAFE METODY =====

/**
 * @brief RT-SAFE: Přidá voice do active tracking.
 */
void VoiceManager::addActiveVoice(Voice* voice) noexcept {
    if (!voice) return;
    
    auto it = std::find(activeVoices_.begin(), activeVoices_.end(), voice);
    if (it == activeVoices_.end()) {
        activeVoices_.push_back(voice);
        activeVoicesCount_.fetch_add(1);
    }
}

/**
 * @brief RT-SAFE: Odebere voice z active tracking.
 */
void VoiceManager::removeActiveVoice(Voice* voice) noexcept {
    if (!voice) return;
    
    auto it = std::find(activeVoices_.begin(), activeVoices_.end(), voice);
    if (it != activeVoices_.end()) {
        activeVoices_.erase(it);
        activeVoicesCount_.fetch_sub(1);
    }
}

/**
 * @brief RT-SAFE: Cleanup neaktivních voices z active tracking.
 */
void VoiceManager::cleanupInactiveVoices() noexcept {
    for (Voice* voice : voicesToRemove_) {
        removeActiveVoice(voice);
    }
    
    voicesToRemove_.clear();
}

/**
 * @brief NON-RT SAFE: Logging wrapper pro non-critical operations.
 */
void VoiceManager::logSafe(const std::string& component, const std::string& severity, 
                          const std::string& message, Logger& logger) const {
    if (!rtMode_.load()) {
        logger.log(component, severity, message);
    }
}