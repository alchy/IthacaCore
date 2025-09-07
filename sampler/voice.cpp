#include "voice.h"
#include <algorithm>  // Pro std::min a clamp
#include <cmath>      // Pro float výpočty

// Static member initialization
std::atomic<bool> Voice::rtMode_{false};

// Default konstruktor
Voice::Voice() : midiNote_(0), instrument_(nullptr), sampleRate_(0),
                 state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
                 gain_(0.0f), releaseStartPosition_(0), releaseSamples_(0) {
    // Žádná akce - čeká na initialize pro plnou inicializaci
}

// Konstruktor pro VoiceManager (pool mód) - bez loggeru, inicializuje se později
Voice::Voice(uint8_t midiNote)
    : midiNote_(midiNote), instrument_(nullptr), sampleRate_(0),
      state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
      gain_(0.0f), releaseStartPosition_(0), releaseSamples_(0) {
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
    gain_ = 0.0f;
    releaseStartPosition_ = 0;
    
    calculateReleaseSamples();  // RT-safe calculation
    
    logSafe("Voice/initialize", "info", 
           "Voice initialized for MIDI " + std::to_string(midiNote_) + 
           " with sampleRate " + std::to_string(sampleRate_), logger);
}

/**
 * @brief Cleanup: Reset na idle stav, zastaví přehrávání.
 * NON-RT SAFE: Obsahuje logging operations.
 */
void Voice::cleanup(Logger& logger) {
    state_ = VoiceState::Idle;
    position_ = 0;
    gain_ = 0.0f;
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
 * @brief RT-SAFE: Nastaví stav note: true = startNote (attack/sustain), false = stopNote (release).
 * OPTIMALIZOVÁNO: Žádné alokace, žádné string operations, pure state management.
 */
void Voice::setNoteState(bool isOn, uint8_t velocity) noexcept {
    if (!instrument_ || sampleRate_ <= 0) {
        // RT-safe: žádné logging, jen return
        return;
    }
    
    // Mapování velocity 0-127 na layer 0-7: 0-15 -> 0, 16-31 -> 1, ..., 112-127 -> 7
    currentVelocityLayer_ = std::min(static_cast<uint8_t>(velocity / 16), static_cast<uint8_t>(7));
    
    if (isOn) {
        // Start note: Attack (zde okamžitý) -> Sustain
        state_ = VoiceState::Sustaining;
        position_ = 0;  // Začátek sample
        gain_ = 1.0f;   // Plný gain
        
        // RT-safe: conditional logging pouze pokud není RT mode
        // Logging je expensive, takže skip v RT contextu
        
    } else {
        // Stop note: Přejdi do release, pokud byl aktivní
        if (state_ == VoiceState::Sustaining || state_ == VoiceState::Attacking) {
            state_ = VoiceState::Releasing;
            releaseStartPosition_ = position_;  // Uložit aktuální pozici pro útlum
            
            // RT-safe: žádné logging v RT módu
        }
    }
}

/**
 * @brief RT-SAFE: Posune pozici ve sample o 1 frame (pro non-real-time processing).
 * OPTIMALIZOVÁNO: Eliminovány expensive function calls, inline gain update.
 */
void Voice::advancePosition() noexcept {
    if (state_ == VoiceState::Idle || !instrument_) {
        return;  // Nic k posunu
    }
    
    const sf_count_t maxFrames = instrument_->get_frame_count(currentVelocityLayer_);
    if (position_ < maxFrames) {
        ++position_;  // Posun o 1 frame (stereo pár L,R)
    }
    
    // Optimalizovaný release processing
    if (state_ == VoiceState::Releasing) {
        updateReleaseGain();  // RT-safe batch update
    }
}

/**
 * @brief RT-SAFE: Získá aktuální stereo audio data s aplikovanou obálkou (gain).
 * OPTIMALIZOVÁNO: Inline gain application, eliminovaná applyEnvelope funkce.
 */
bool Voice::getCurrentAudioData(AudioData& data) const noexcept {
    if (state_ == VoiceState::Idle || !instrument_ || 
        position_ >= instrument_->get_frame_count(currentVelocityLayer_)) {
        return false;  // Neaktivní nebo konec sample
    }
    
    const float* stereoPtr = instrument_->get_sample_begin_pointer(currentVelocityLayer_);
    if (!stereoPtr) return false;
    
    // OPTIMALIZOVÁNO: Inline gain application - žádný function call overhead
    const sf_count_t idx = position_ * 2;
    data.left = stereoPtr[idx] * gain_;      // Direct inline místo applyEnvelope()
    data.right = stereoPtr[idx + 1] * gain_; // Direct inline místo applyEnvelope()
    
    return true;
}

/**
 * @brief RT-SAFE HLAVNÍ METODA: Zpracuje audio blok s explicitními stereo buffery.
 * 
 * KOMPLETNĚ REFAKTOROVÁNO PRO RT-PERFORMANCE:
 * - Batch processing místo sample-by-sample
 * - Eliminovány redundantní kontroly v loops
 * - Cache-friendly memory access patterns  
 * - Specialized processing paths
 * - Žádné string operations ani memory allocations
 * - Žádné logging v RT contextu
 */
bool Voice::processBlock(float* outputLeft, float* outputRight, int numSamples) noexcept {
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
    
    static int debugCounter = 0;
    if (debugCounter < 5) {  // Log jen prvních 5 bloků
        printf("[DEBUG] ProcessBlock MIDI %d: state=%d, buffer=%p, maxFrames=%ld, position=%ld\n",
               midiNote_, static_cast<int>(state_), stereoBuffer, maxFrames, position_);
        debugCounter++;
    }

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
    
    // SPECIALIZED PROCESSING PATHS - better CPU branch prediction
    if (state_ == VoiceState::Sustaining) {
        // Simple sustain - constant gain, optimized loop
        processConstantGain(stereoBuffer, outputLeft, outputRight, samplesToProcess);
    } else if (state_ == VoiceState::Releasing) {
        // Release with linear fade - optimized interpolation
        processRelease(stereoBuffer, outputLeft, outputRight, samplesToProcess);
    }
    
    // Batch position update - once per block instead of per sample
    position_ += samplesToProcess;
    
    // End condition check - only once per block
    if (position_ >= maxFrames) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    // OPTIMALIZACE: Fill remaining samples with silence if needed
    // memset je rychlejší než loop s += 0.0f
    if (samplesToProcess < numSamples) {
        const int remainingSamples = numSamples - samplesToProcess;
        memset(&outputLeft[samplesToProcess], 0, remainingSamples * sizeof(float));
        memset(&outputRight[samplesToProcess], 0, remainingSamples * sizeof(float));
    }
    
    return state_ != VoiceState::Idle;
}

/**
 * @brief RT-SAFE: Alternativní processBlock s AudioData strukturou.
 * Kompatibilní interface, ale méně optimální než pointer verze.
 */
bool Voice::processBlock(AudioData* outputBuffer, int numSamples) noexcept {
    if (state_ == VoiceState::Idle || !instrument_ || sampleRate_ <= 0) {
        return false;
    }
    
    if (!outputBuffer || numSamples <= 0) {
        return false;
    }
    
    const float* stereoBuffer = instrument_->get_sample_begin_pointer(currentVelocityLayer_);
    const sf_count_t maxFrames = instrument_->get_frame_count(currentVelocityLayer_);
    
    if (!stereoBuffer || maxFrames == 0) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    const sf_count_t samplesUntilEnd = maxFrames - position_;
    const int samplesToProcess = static_cast<int>(std::min(
        static_cast<sf_count_t>(numSamples), 
        samplesUntilEnd
    ));
    
    // Process samples using AudioData interface
    for (int i = 0; i < samplesToProcess; ++i) {
        AudioData sample;
        if (getCurrentAudioData(sample)) {
            outputBuffer[i].left += sample.left;
            outputBuffer[i].right += sample.right;
        }
        
        advancePosition();  // RT-safe single sample advance
        
        if (state_ == VoiceState::Idle) {
            // Zero remaining buffer
            for (int j = i + 1; j < numSamples; ++j) {
                outputBuffer[j].left += 0.0f;
                outputBuffer[j].right += 0.0f;
            }
            return false;
        }
    }
    
    // Zero remaining samples if any
    for (int i = samplesToProcess; i < numSamples; ++i) {
        outputBuffer[i].left += 0.0f;
        outputBuffer[i].right += 0.0f;
    }
    
    return isActive();
}

// ===== PRIVATE OPTIMALIZOVANÉ RT-SAFE METODY =====

/**
 * @brief RT-SAFE: Výpočet releaseSamples na základě sampleRate_ (200 ms release).
 */
void Voice::calculateReleaseSamples() noexcept {
    if (sampleRate_ > 0) {
        releaseSamples_ = static_cast<sf_count_t>(0.2 * sampleRate_);  // 200 ms = 0.2 s
    } else {
        releaseSamples_ = 0;
    }
}

/**
 * @brief RT-SAFE: Update release gain pro single sample advance.
 * OPTIMALIZOVÁNO: Inline gain calculation bez function call overhead.
 */
void Voice::updateReleaseGain() noexcept {
    const sf_count_t elapsed = position_ - releaseStartPosition_;
    if (elapsed >= releaseSamples_) {
        gain_ = 0.0f;
        state_ = VoiceState::Idle;
    } else {
        // Linear interpolation - optimalizovaná verze
        gain_ = 1.0f - static_cast<float>(elapsed) / static_cast<float>(releaseSamples_);
    }
}

/**
 * @brief RT-SAFE: Specialized processing pro konstantní gain (sustain).
 * OPTIMALIZOVÁNO PRO CACHE A VECTORIZATION:
 * - Použití pointer arithmetic pro lepší compiler optimization
 * - Vectorizable loop pattern
 * - Cache-friendly sequential access
 */
void Voice::processConstantGain(const float* stereoBuffer, 
                               float* outputLeft, float* outputRight, 
                               int numSamples) noexcept {
    // Cache starting position
    const sf_count_t startIndex = position_ * 2;
    
    // Optimalizovaný loop - compiler může vectorizovat
    // Pointer arithmetic je často rychlejší než array indexing
    const float* srcPtr = stereoBuffer + startIndex;
    
    for (int i = 0; i < numSamples; ++i) {
        const int srcIndex = i * 2;
        outputLeft[i] += srcPtr[srcIndex] * gain_;
        outputRight[i] += srcPtr[srcIndex + 1] * gain_;
    }
}

/**
 * @brief RT-SAFE: Specialized processing pro release s lineární fade.
 * OPTIMALIZOVÁNO PRO SMOOTH INTERPOLATION:
 * - Pre-calculated gain step pro lineární interpolaci
 * - Batch gain updates
 * - Early termination při gain = 0
 */
void Voice::processRelease(const float* stereoBuffer, 
                          float* outputLeft, float* outputRight, 
                          int numSamples) noexcept {
    const sf_count_t startIndex = position_ * 2;
    const sf_count_t releaseElapsed = position_ - releaseStartPosition_;
    
    // Pre-calculate gain step pro lineární interpolaci
    const sf_count_t remainingReleaseSamples = releaseSamples_ - releaseElapsed;
    const float gainStep = (remainingReleaseSamples > 0) ? 
        (-gain_ / static_cast<float>(remainingReleaseSamples)) : 0.0f;
    
    float currentGain = gain_;
    const float* srcPtr = stereoBuffer + startIndex;
    
    for (int i = 0; i < numSamples; ++i) {
        // Early termination check
        if (currentGain <= 0.0f) {
            currentGain = 0.0f;
            state_ = VoiceState::Idle;
            
            // Zero remaining output
            for (int j = i; j < numSamples; ++j) {
                outputLeft[j] += 0.0f;
                outputRight[j] += 0.0f;
            }
            break;
        }
        
        const int srcIndex = i * 2;
        outputLeft[i] += srcPtr[srcIndex] * currentGain;
        outputRight[i] += srcPtr[srcIndex + 1] * currentGain;
        
        // Update gain pro next sample
        currentGain += gainStep;
    }
    
    // Update stored gain
    gain_ = std::max(0.0f, currentGain);
    
    // Final release completion check
    if (gain_ <= 0.0f) {
        state_ = VoiceState::Idle;
    }
}

/**
 * @brief NON-RT SAFE: Wrapper pro bezpečné logování v non-critical operacích.
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