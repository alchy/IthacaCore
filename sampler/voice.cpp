#include "voice.h"
#include <algorithm>  // Pro std::min a clamp
#include <cmath>      // Pro float výpočty

// Static member initialization
std::atomic<bool> Voice::rtMode_{false};

// Default konstruktor - OPRAVENÁ inicializace gain
Voice::Voice() : midiNote_(0), instrument_(nullptr), sampleRate_(0),
                 state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
                 envelope_gain_(0.0f), velocity_gain_(0.0f), master_gain_(0.8f),  // NOVÉ
                 releaseStartPosition_(0), releaseSamples_(0) {
    // Žádná akce - čeká na initialize pro plnou inicializaci
}

// Konstruktor pro VoiceManager (pool mód) - bez loggeru, inicializuje se později
Voice::Voice(uint8_t midiNote)
    : midiNote_(midiNote), instrument_(nullptr), sampleRate_(0),
      state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
      envelope_gain_(0.0f), velocity_gain_(0.0f), master_gain_(0.8f),  // NOVÉ
      releaseStartPosition_(0), releaseSamples_(0) {
    // Logger se předá při initialize
}

/**
 * @brief Inicializuje voice s instrumentem a sample rate.
 * NON-RT SAFE: Obsahuje logging operations.
 */
void Voice::initialize(const Instrument& instrument, int sampleRate, Logger& logger) {
    instrument_ = &instrument;
    sampleRate_ = sampleRate;
    
    // Validace sampleRate - musí být kladný pro správné výpočty obálky
    if (sampleRate_ <= 0) {
        logSafe("Voice/initialize", "error", 
               "Invalid sampleRate " + std::to_string(sampleRate_) + 
               " - must be > 0. Terminating.", logger);
        std::exit(1);
    }
    
    // Reset všech stavů pro čistý start
    state_ = VoiceState::Idle;
    position_ = 0;
    currentVelocityLayer_ = 0;
    envelope_gain_ = 0.0f;
    velocity_gain_ = 0.0f;  // NOVÉ - reset velocity gain
    master_gain_ = 0.8f;    // NOVÉ - default master gain
    releaseStartPosition_ = 0;
    
    calculateReleaseSamples();  // RT-safe calculation
    
    // Initialize gain buffer with reasonable default size
    // Will be properly sized by prepareToPlay()
    gainBuffer_.reserve(512);
    
    logSafe("Voice/initialize", "info", 
           "Voice initialized for MIDI " + std::to_string(midiNote_) + 
           " with sampleRate " + std::to_string(sampleRate_) +
           ", master gain " + std::to_string(master_gain_), logger);
}

/**
 * @brief NON-RT SAFE: Updates internal buffer size for DAW block size changes
 * Must be called from audio thread during buffer size changes, NOT during processing
 */
void Voice::prepareToPlay(int maxBlockSize) noexcept {
    // NON-RT SAFE: This method should be called from audio thread during setup/buffer size changes
    // Never call during active audio processing
    
    if (maxBlockSize > 0 && gainBuffer_.size() < static_cast<size_t>(maxBlockSize)) {
        try {
            gainBuffer_.resize(maxBlockSize);
        } catch (...) {
            // In case of allocation failure, keep old buffer
            // This is better than crashing, though not ideal
        }
    }
}

/**
 * @brief Cleanup: Reset na idle stav, zastaví přehrávání.
 * NON-RT SAFE: Obsahuje logging operations.
 */
void Voice::cleanup(Logger& logger) {
    state_ = VoiceState::Idle;
    position_ = 0;
    envelope_gain_ = 0.0f;
    velocity_gain_ = 0.0f;  // NOVÉ - reset velocity gain
    master_gain_ = 0.8f;    // NOVÉ - reset na default
    releaseStartPosition_ = 0;
    
    logSafe("Voice/cleanup", "info", 
           "Voice cleaned up and reset to idle for MIDI " + std::to_string(midiNote_), logger);
}

/**
 * @brief Reinicializuje s novým instrumentem a sample rate.
 * NON-RT SAFE: Deleguje na initialize s loggingem.
 */
void Voice::reinitialize(const Instrument& instrument, int sampleRate, Logger& logger) {
    initialize(instrument, sampleRate, logger);
    logSafe("Voice/reinitialize", "info", 
           "Voice reinitialized with new instrument and sampleRate for MIDI " + std::to_string(midiNote_), logger);
}

/**
 * @brief OPRAVENÁ setNoteState: Nyní SPRÁVNĚ aplikuje velocity na hlasitost.
 * RT-SAFE: Žádné alokace, žádné string operations, pure state management.
 */
void Voice::setNoteState(bool isOn, uint8_t velocity) noexcept {
    if (!instrument_ || sampleRate_ <= 0) {
        // RT-safe: žádné logging, jen return
        return;
    }
    
    // Mapování velocity 0-127 na layer 0-7: 0-15 → 0, 16-31 → 1, ..., 112-127 → 7
    currentVelocityLayer_ = std::min(static_cast<uint8_t>(velocity / 16), static_cast<uint8_t>(7));
    
    if (isOn) {
        // KRITICKÁ OPRAVA: Velocity nyní ovlivňuje hlasitost
        updateVelocityGain(velocity);  // NOVÉ - nastaví velocity_gain_
        
        state_ = VoiceState::Sustaining;
        position_ = 0;  // Začátek sample
        envelope_gain_ = 1.0f;  // Plný envelope gain pro sustain
        
    } else {
        // Stop note: Přejdi do release, pokud byl aktivní
        if (state_ == VoiceState::Sustaining || state_ == VoiceState::Attacking) {
            state_ = VoiceState::Releasing;
            releaseStartPosition_ = position_;  // Uložit aktuální pozici pro útlum
            // envelope_gain_ will be calculated dynamically in calculateBlockGains
        }
    }
}

/**
 * @brief RT-SAFE: Pre-calculates envelope gains for entire block
 */
bool Voice::calculateBlockGains(float* gainBuffer, int numSamples) noexcept {
    if (state_ == VoiceState::Idle || !gainBuffer || numSamples <= 0) {
        return false;
    }
    
    switch (state_) {
        case VoiceState::Sustaining:
        case VoiceState::Attacking: {
            // Constant gain for sustain/attack
            const float constantGain = envelope_gain_;
            for (int i = 0; i < numSamples; ++i) {
                gainBuffer[i] = constantGain;
            }
            return true;
        }
        
        case VoiceState::Releasing: {
            // Linear release fade
            constexpr float targetGain = 0.001f;
            constexpr float estimatedStartGain = 1.0f;
            
            const sf_count_t releaseElapsed = position_ - releaseStartPosition_;
            
            for (int i = 0; i < numSamples; ++i) {
                const float elapsedLocal = static_cast<float>(releaseElapsed + i);
                
                if (releaseSamples_ <= 0 || elapsedLocal >= static_cast<float>(releaseSamples_)) {
                    gainBuffer[i] = targetGain;
                } else {
                    const float progress = elapsedLocal / static_cast<float>(releaseSamples_);
                    gainBuffer[i] = estimatedStartGain * (1.0f - progress) + targetGain * progress;
                    gainBuffer[i] = std::max(gainBuffer[i], targetGain);
                    gainBuffer[i] = std::min(gainBuffer[i], estimatedStartGain);
                }
            }
            
            // Update envelope_gain_ to last calculated value for getter consistency
            envelope_gain_ = gainBuffer[numSamples - 1];
            
            // Check if release is complete
            return envelope_gain_ > targetGain * 1.1f;
        }
        
        default:
            return false;
    }
}

/**
 * @brief RT-SAFE: Posune pozici ve sample o 1 frame (pro non-real-time processing).
 * OPTIMALIZOVÁNO: Eliminovány expensive function calls.
 */
void Voice::advancePosition() noexcept {
    if (state_ == VoiceState::Idle || !instrument_) {
        return;  // Nic k posunu
    }
    
    const sf_count_t maxFrames = instrument_->get_frame_count(currentVelocityLayer_);
    if (position_ < maxFrames) {
        ++position_;  // Posun o 1 frame (stereo pár L,R)
    }
    
    // Release processing se nyní řeší jen v processRelease()
    // - žádné gain updates zde
}

/**
 * @brief OPRAVENÁ getCurrentAudioData: Nyní aplikuje kompletní gain chain.
 * RT-SAFE: Inline gain application s envelope * velocity * master.
 */
bool Voice::getCurrentAudioData(AudioData& data) const noexcept {
    if (state_ == VoiceState::Idle || !instrument_ || 
        position_ >= instrument_->get_frame_count(currentVelocityLayer_)) {
        return false;  // Neaktivní nebo konec sample
    }
    
    const float* stereoPtr = instrument_->get_sample_begin_pointer(currentVelocityLayer_);
    if (!stereoPtr) return false;
    
    // KRITICKÁ OPRAVA: Aplikuj všechny gain komponenty
    const float finalGain = envelope_gain_ * velocity_gain_ * master_gain_;
    const sf_count_t idx = position_ * 2;
    data.left = stereoPtr[idx] * finalGain;
    data.right = stereoPtr[idx + 1] * finalGain;
    
    return true;
}

/**
 * @brief RT-SAFE HLAVNÍ METODA: Zpracuje audio blok s OPRAVENOU centralizovanou aplikací gain.
 * 
 * KOMPLETNĚ REFAKTOROVÁNO PRO KOMPLETNÍ GAIN CHAIN:
 * - Volá calculateBlockGains() pro výpočet envelope
 * - Aplikuje envelope_gain_ * velocity_gain_ * master_gain_ v jednom centralizovaném loopu
 * - Řídí voice state transitions na základě výsledků gain calculation
 * - Provádí mixdown sčítání do sdílených output bufferů
 */
bool Voice::processBlock(float* outputLeft, float* outputRight, 
                        int numSamples, int activeVoiceCount) noexcept {
    // Early exit conditions - RT optimized
    if (state_ == VoiceState::Idle || !instrument_ || sampleRate_ <= 0) {
        return false;  // Voice není aktivní
    }
    
    if (!outputLeft || !outputRight || numSamples <= 0) {
        // RT-safe: žádné logging, jen return false
        return false;
    }
    
    // Cache frequently used values (avoid repeated method calls)
    const float* stereoBuffer = instrument_->get_sample_begin_pointer(currentVelocityLayer_);
    const sf_count_t maxFrames = instrument_->get_frame_count(currentVelocityLayer_);

    if (!stereoBuffer || maxFrames == 0) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    // OPTIMALIZACE: Calculate how many samples we can actually process
    // Eliminuje per-sample boundary checking
    const sf_count_t samplesUntilEnd = maxFrames - position_;
    const int samplesToProcess = static_cast<int>(std::min(
        static_cast<sf_count_t>(numSamples), 
        samplesUntilEnd
    ));
    
    // Use pre-allocated buffer, ensure it's large enough
    if (gainBuffer_.size() < static_cast<size_t>(samplesToProcess)) {
        // This should not happen if prepareToPlay() was called correctly
        // But provide fallback for safety
        return false;
    }
    
    // Pre-calculate envelope gains for the block
    if (!calculateBlockGains(gainBuffer_.data(), samplesToProcess)) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    // Dynamic gain scaling to prevent clipping with multiple voices
    const float voiceScaling = (activeVoiceCount > 1) ? 
        1.0f / std::sqrt(static_cast<float>(activeVoiceCount)) : 1.0f;
    
    // KRITICKÁ OPRAVA: KOMPLETNÍ GAIN CHAIN - envelope * velocity * master * voiceScaling
    const sf_count_t startIndex = position_ * 2;
    const float* srcPtr = stereoBuffer + startIndex;
    
    // Kombinuj všechny gain komponenty mimo envelope (který je per-sample)
    const float baseGain = velocity_gain_ * master_gain_ * voiceScaling;
    
    for (int i = 0; i < samplesToProcess; ++i) {
        // Kompletní gain chain: envelope (per-sample) * velocity * master * voiceScaling
        const float finalGain = gainBuffer_.data()[i] * baseGain * 0.8;
        const int srcIndex = i * 2;
        
        // Mixdown: Add to shared output buffers (multiple voices sum together)
        outputLeft[i] += srcPtr[srcIndex] * finalGain;
        outputRight[i] += srcPtr[srcIndex + 1] * finalGain;
    }
    
    // Batch position update - once per block instead of per sample
    position_ += samplesToProcess;
    
    // End condition check - only once per block
    if (position_ >= maxFrames) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    return state_ != VoiceState::Idle;
}

/**
 * @brief RT-SAFE: Alternativní processBlock s AudioData strukturou.
 * DEPRECATED: Používá se pouze pro zpětnou kompatibilitu.
 * Pro optimální výkon použijte processBlock s float pointers.
 */
bool Voice::processBlock(AudioData* outputBuffer, int numSamples) noexcept {
    if (!outputBuffer || numSamples <= 0) {
        return false;
    }
    
    // Allocate temporary buffers for delegation
    float* leftBuffer = new(std::nothrow) float[numSamples];
    float* rightBuffer = new(std::nothrow) float[numSamples];
    
    if (!leftBuffer || !rightBuffer) {
        delete[] leftBuffer;
        delete[] rightBuffer;
        return false;
    }
    
    // Initialize buffers to zero
    memset(leftBuffer, 0, numSamples * sizeof(float));
    memset(rightBuffer, 0, numSamples * sizeof(float));
    
    // Delegate to optimized version
    bool result = processBlock(leftBuffer, rightBuffer, numSamples);
    
    // Copy results to AudioData structure (additive mixing)
    for (int i = 0; i < numSamples; ++i) {
        outputBuffer[i].left += leftBuffer[i];
        outputBuffer[i].right += rightBuffer[i];
    }
    
    delete[] leftBuffer;
    delete[] rightBuffer;
    
    return result;
}

// ===== PRIVATE RT-SAFE METODY =====

/**
 * @brief RT-SAFE: Výpočet releaseSamples na základě sampleRate_ (500 ms release).
 */
void Voice::calculateReleaseSamples() noexcept {
    if (sampleRate_ > 0) {
        releaseSamples_ = static_cast<sf_count_t>(0.5 * sampleRate_);  // 500 ms = 0.5 s
    } else {
        releaseSamples_ = 0;
    }
}

/**
 * @brief RT-SAFE: NOVÁ metoda pro aplikaci MIDI velocity na hlasitost
 * Mapuje velocity 0-127 na velocity_gain_ 0.0-1.0 s logaritmickou křivkou.
 */
void Voice::updateVelocityGain(uint8_t velocity) noexcept {
    // Mapování MIDI velocity 0-127 na gain 0.0-1.0
    // Použití curve pro přirozenější pocit hlasitosti
    
    if (velocity == 0) {
        velocity_gain_ = 0.0f;  // Silence pro velocity 0
        return;
    }
    
    // Logaritmická křivka pro přirozenější velocity response
    // velocity 1 → ~0.05, velocity 64 → ~0.57, velocity 127 → 1.0
    float normalized = static_cast<float>(velocity) / 127.0f;
    
    // Sqrt curve pro smooth velocity response (místo lineární)
    velocity_gain_ = std::sqrt(normalized);
    
    // Alternative linear mapping (jednodušší):
    // velocity_gain_ = normalized;
    
    // Clamp pro bezpečnost
    velocity_gain_ = std::max(0.0f, std::min(1.0f, velocity_gain_));
}

/**
 * @brief NON-RT SAFE: Nastaví master gain pro voice (0.0-1.0)
 */
void Voice::setMasterGain(float gain, Logger& logger) {
    if (gain < 0.0f || gain > 1.0f) {
        logSafe("Voice/setMasterGain", "error", 
               "Invalid master gain " + std::to_string(gain) + 
               " (must be 0.0-1.0) for MIDI " + std::to_string(midiNote_), logger);
        return;
    }
    master_gain_ = gain;
    logSafe("Voice/setMasterGain", "info", 
           "Master gain set to " + std::to_string(master_gain_) + 
           " for MIDI " + std::to_string(midiNote_), logger);
}

/**
 * @brief NON-RT SAFE: Získá debug informace o gain structure
 */
std::string Voice::getGainDebugInfo(Logger& logger) const {
    std::string info = "MIDI " + std::to_string(midiNote_) + 
                      " Gains - Envelope: " + std::to_string(envelope_gain_) +
                      ", Velocity: " + std::to_string(velocity_gain_) +
                      ", Master: " + std::to_string(master_gain_) +
                      ", Final: " + std::to_string(getFinalGain()) +
                      ", State: " + std::to_string(static_cast<int>(state_));
    
    logSafe("Voice/getGainDebugInfo", "info", info, logger);
    return info;
}

/**
 * @brief NON-RT: Safe logging wrapper pro non-critical operations.
 * Používá se pouze v initialize/cleanup/reinitialize metodách.
 */
void Voice::logSafe(const std::string& component, const std::string& severity, 
                   const std::string& message, Logger& logger) const {
    // Conditional logging - skip pokud je RT mode zapnutý
    if (!rtMode_.load()) {
        logger.log(component, severity, message);
    }
    // V RT módu: žádné logging = žádné I/O operations
}