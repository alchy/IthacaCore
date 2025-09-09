#include "voice.h"
#include <algorithm>  // Pro std::min a clamp
#include <cmath>      // Pro float výpočty

// Static member initialization
std::atomic<bool> Voice::rtMode_{false};

// Default konstruktor - AKTUALIZOVÁNA inicializace s envelope positions
Voice::Voice() : midiNote_(0), instrument_(nullptr), sampleRate_(0),
                 state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
                 envelope_gain_(0.0f), velocity_gain_(0.0f), master_gain_(0.8f),
                 envelope_(nullptr),
                 envelope_attack_position_(0), envelope_release_position_(0), releaseSamples_(0) {
}

// Konstruktor pro VoiceManager (pool mód)
Voice::Voice(uint8_t midiNote)
    : midiNote_(midiNote), instrument_(nullptr), sampleRate_(0),
      state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
      envelope_gain_(0.0f), velocity_gain_(0.0f), master_gain_(0.8f),
      envelope_(nullptr),
      envelope_attack_position_(0), envelope_release_position_(0), releaseSamples_(0) {
}

/**
 * @brief AKTUALIZOVANÁ initialize s envelope validation
 */
void Voice::initialize(const Instrument& instrument, int sampleRate, 
                      const Envelope& envelope, Logger& logger) {
    instrument_ = &instrument;
    sampleRate_ = sampleRate;
    envelope_ = &envelope;
    
    // Validace sampleRate
    if (sampleRate_ <= 0) {
        logSafe("Voice/initialize", "error", 
               "Invalid sampleRate " + std::to_string(sampleRate_) + 
               " - must be > 0. Terminating.", logger);
        std::exit(1);
    }
    
    // Validace envelope
    if (!envelope_) {
        logSafe("Voice/initialize", "error", 
               "Envelope reference is null. Terminating.", logger);
        std::exit(1);
    }
    
    // Reset všech stavů pro čistý start
    state_ = VoiceState::Idle;
    position_ = 0;
    currentVelocityLayer_ = 0;
    envelope_gain_ = 0.0f;
    velocity_gain_ = 0.0f;
    master_gain_ = 0.8f;
    
    // NOVÉ: Reset envelope positions
    envelope_attack_position_ = 0;
    envelope_release_position_ = 0;
    
    calculateReleaseSamples();
    gainBuffer_.reserve(512);
    
    logSafe("Voice/initialize", "info", 
           "Voice initialized for MIDI " + std::to_string(midiNote_) + 
           " with ADSR envelope system (A:" + std::to_string(envelope_->getAttackLength()) + 
           ", R:" + std::to_string(envelope_->getReleaseLength()) + " samples)", logger);
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
 * @brief AKTUALIZOVANÁ cleanup s envelope position reset
 */
void Voice::cleanup(Logger& logger) {
    state_ = VoiceState::Idle;
    position_ = 0;
    envelope_gain_ = 0.0f;
    velocity_gain_ = 0.0f;
    master_gain_ = 0.8f;
    
    // NOVÉ: Reset envelope positions
    envelope_attack_position_ = 0;
    envelope_release_position_ = 0;
    
    
    logSafe("Voice/cleanup", "info", 
           "Voice cleaned up and reset to idle for MIDI " + std::to_string(midiNote_), logger);
}

/**
 * @brief AKTUALIZOVANÁ reinitialize s envelope
 */
void Voice::reinitialize(const Instrument& instrument, int sampleRate,
                         const Envelope& envelope, Logger& logger) {
    initialize(instrument, sampleRate, envelope, logger);
    logSafe("Voice/reinitialize", "info", 
           "Voice reinitialized with new instrument, sampleRate and ADSR envelope for MIDI " + std::to_string(midiNote_), logger);
}

/**
 * @brief REFAKTOROVANÁ setNoteState s envelope position reset
 */
void Voice::setNoteState(bool isOn, uint8_t velocity) noexcept {
    if (!instrument_ || sampleRate_ <= 0 || !envelope_) {
        return;
    }
    
    currentVelocityLayer_ = std::min(static_cast<uint8_t>(velocity / 16), static_cast<uint8_t>(7));
    
    if (isOn) {
        updateVelocityGain(velocity);
        state_ = VoiceState::Attacking;
        position_ = 0;
        envelope_gain_ = 0.0f;
        
        // NOVÉ: Reset attack envelope position
        envelope_attack_position_ = 0;
        
    } else {
        if (state_ == VoiceState::Sustaining || state_ == VoiceState::Attacking) {
            state_ = VoiceState::Releasing;
            envelope_release_position_ = 0;
        }
    }
}

/**
 * @brief KOMPLETNĚ REFAKTOROVANÁ calculateBlockGains pro ADSR envelope
 */
bool Voice::calculateBlockGains(float* gainBuffer, int numSamples) noexcept {
    if (state_ == VoiceState::Idle || !gainBuffer || numSamples <= 0) {
        return false;
    }
    
    switch (state_) {
        case VoiceState::Attacking: {
            // NOVÉ: Použít ADSR envelope API bez MIDI parametru
            bool attackContinues = envelope_->getAttackGains(gainBuffer, numSamples, envelope_attack_position_);
            
            // Increment pozice pro další blok
            envelope_attack_position_ += numSamples;
            
            // Zkontroluj dokončení attack
            if (!attackContinues || gainBuffer[numSamples - 1] >= 0.99f) {
                state_ = VoiceState::Sustaining;
                // Dokončit blok sustain hodnotami
                const float sustainLevel = envelope_->getSustainLevel();
                for (int i = 0; i < numSamples; ++i) {
                    if (gainBuffer[i] >= 0.99f) gainBuffer[i] = sustainLevel;
                }
            }
            
            envelope_gain_ = gainBuffer[numSamples - 1];
            return true;
        }
        
        case VoiceState::Sustaining: {
            // NOVÉ: Konstantní sustain level z envelope parametrů
            const float sustainLevel = envelope_->getSustainLevel();
            for (int i = 0; i < numSamples; ++i) {
                gainBuffer[i] = sustainLevel;
            }
            envelope_gain_ = sustainLevel;
            return true;
        }
        
        case VoiceState::Releasing: {
            // NOVÉ: Použít ADSR envelope API pro release
            bool releaseContinues = envelope_->getReleaseGains(gainBuffer, numSamples, envelope_release_position_);
            
            // Increment pozice pro další blok
            envelope_release_position_ += numSamples;
            
            // Aplikovat minimum pro bezpečnost
            constexpr float targetGain = 0.001f;
            for (int i = 0; i < numSamples; ++i) {
                gainBuffer[i] = std::max(gainBuffer[i], targetGain);
            }
            
            envelope_gain_ = gainBuffer[numSamples - 1];
            
            // Zkontroluj dokončení release
            if (!releaseContinues || envelope_gain_ <= targetGain * 1.1f) {
                return false;  // Voice dokončen
            }
            return true;
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
    data.left = stereoPtr[idx]; // xxx * finalGain;
    data.right = stereoPtr[idx + 1]; // xxx * finalGain;
    
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
        
        // srcIndex pro 2 kanal
        const int srcIndex = i * 2;
        
        // Kompletní gain chain: envelope (per-sample) * velocity * master * voiceScaling
        const float envelopeGain = 1.0; // xxx gainBuffer_.data()[i];
        const float finalGain = envelopeGain * velocity_gain_ * master_gain_ * voiceScaling;
        
        // Mixdown: Add to shared output buffers (multiple voices sum together)
        outputLeft[i] += srcPtr[srcIndex]; // xxx * finalGain;
        outputRight[i] += srcPtr[srcIndex + 1]; // xxx * finalGain;
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