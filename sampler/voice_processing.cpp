#include <algorithm>
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159f
#endif

#include "IthacaConfig.h"
#include "voice.h"

// ===== DEBUG CONTROL =====
#define VOICE_DEBUG_ENABLED 0

#if VOICE_DEBUG_ENABLED
    #define DEBUG_PRINT(x) std::cout << x << std::endl
    #define DEBUG_ENVELOPE_TO_RIGHT_CHANNEL 1
#else
    #define DEBUG_PRINT(x) do {} while(0)
    #define DEBUG_ENVELOPE_TO_RIGHT_CHANNEL 0
#endif

// =====================================================================
// AUDIO PROCESSING - MAIN ENTRY POINT
// =====================================================================

bool Voice::processBlock(float* outputLeft, float* outputRight, int samplesPerBlock) noexcept {
    // =====================================================================
    // PHASE 1: PROCESS DAMPING BUFFER (if retrigger active)
    // =====================================================================
    
    if (dampingActive_) {
        // Calculate how many damping samples remain
        const int dampingSamplesRemaining = dampingLength_ - dampingPosition_;
        const int dampingSamplesToProcess = std::min(samplesPerBlock, dampingSamplesRemaining);
        
        // Mix pre-computed damping buffer directly into output
        // No gain calculations needed - buffer already contains final audio
        for (int i = 0; i < dampingSamplesToProcess; ++i) {
            const int bufferIndex = dampingPosition_ + i;
            outputLeft[i] += dampingBufferLeft_[bufferIndex];
            outputRight[i] += dampingBufferRight_[bufferIndex];
        }
        
        // Update damping playback position
        dampingPosition_ += dampingSamplesToProcess;
        
        // Deactivate damping if we've finished playback
        if (dampingPosition_ >= dampingLength_) {
            dampingActive_ = false;
            dampingPosition_ = 0; // Reset for potential future use
        }
    }
    
    // =====================================================================
    // PHASE 2: PROCESS MAIN VOICE (new note after retrigger or normal playback)
    // =====================================================================
    
    // ===== EARLY EXIT CONDITIONS =====
    
    if (!isVoiceReady() || state_ == VoiceState::Idle) {
        return false;
    }
    
    if (!outputLeft || !outputRight || samplesPerBlock <= 0) {
        return false;
    }
    
    // ===== GET INSTRUMENT DATA =====
    
    const float* stereoBuffer = instrument_->get_sample_begin_pointer(currentVelocityLayer_);
    const int maxFrames = instrument_->get_frame_count(currentVelocityLayer_);

    if (!stereoBuffer || maxFrames == 0) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    // ===== CALCULATE SAMPLES TO PROCESS =====
    
    const int samplesUntilEnd = maxFrames - position_;
    const int samplesToProcess = std::min(samplesPerBlock, samplesUntilEnd);
    
    // ===== VERIFY BUFFER CAPACITY =====
    
    // Critical error must be visible even in RT context
    if (gainBuffer_.capacity() < static_cast<size_t>(samplesToProcess)) {
        std::cout << "[Voice/processBlock] error: Buffer capacity insufficient - need " 
                  << samplesToProcess << " samples, have " << gainBuffer_.capacity() << std::endl;
        return false; // Buffer capacity insufficient - visible failure
    }
    
    // ===== ENSURE BUFFER IS SIZED CORRECTLY =====
    
    if (gainBuffer_.size() < static_cast<size_t>(samplesToProcess)) {
        gainBuffer_.resize(samplesToProcess);
    }
    
    // ===== CALCULATE ENVELOPE GAINS =====
    
    if (!calculateBlockGains(gainBuffer_.data(), samplesToProcess)) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    // ===== APPLY AUDIO PROCESSING WITH GAIN CHAIN =====
    
    processAudioWithGains(outputLeft, outputRight, stereoBuffer, samplesToProcess);
    
    // ===== UPDATE POSITION =====
    
    position_ += samplesToProcess;
    
    // ===== CHECK FOR END CONDITION =====
    
    if (position_ >= maxFrames) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    return state_ != VoiceState::Idle;
}

// =====================================================================
// ENVELOPE GAIN CALCULATION
// =====================================================================

bool Voice::calculateBlockGains(float* gainBuffer, int numSamples) noexcept {
    // ===== EARLY EXIT CONDITIONS =====
    
    if (state_ == VoiceState::Idle || !gainBuffer || numSamples <= 0) {
        return false;
    }
    
    // ===== BUFFER OVERFLOW PROTECTION =====
    
    // Critical error must be visible even in RT context
    if (static_cast<size_t>(numSamples) > gainBuffer_.capacity()) {
        std::cout << "[Voice/calculateBlockGains] error: Buffer overflow - requested " 
                  << numSamples << " samples, capacity " << gainBuffer_.capacity() << std::endl;
        return false; // Fail gracefully but visibly
    }
    
    // ===== PROCESS CURRENT ENVELOPE STATE =====
    
    switch (state_) {
        case VoiceState::Attacking:
            return processAttackPhase(gainBuffer, numSamples);
            
        case VoiceState::Sustaining:
            return processSustainPhase(gainBuffer, numSamples);
            
        case VoiceState::Releasing:
            return processReleasePhase(gainBuffer, numSamples);

        default:
            return false;
    }
}

// =====================================================================
// ENVELOPE PHASE PROCESSING
// =====================================================================

bool Voice::processAttackPhase(float* gainBuffer, int numSamples) noexcept {
    DEBUG_PRINT("VoiceState::Attacking");
    
    // ===== GET ATTACK GAINS FROM ENVELOPE =====
    
    bool attackContinues = envelope_->getAttackGains(gainBuffer, numSamples, 
                                                   envelope_attack_position_, sampleRate_);
    
    envelope_attack_position_ += numSamples;
    
    // ===== CHECK IF ATTACK PHASE IS COMPLETE =====
    
    if (!attackContinues || gainBuffer[numSamples - 1] >= ENVELOPE_TRIGGERS_END_ATTACK) {
        // Transition to sustain phase
        state_ = VoiceState::Sustaining;
        
        // Get sustain level for transition
        const float sustainLevel = envelope_->getSustainLevel();
        
        // Fill remainder of block with sustain values
        for (int i = 0; i < numSamples; ++i) {
            if (gainBuffer[i] >= ENVELOPE_TRIGGERS_END_ATTACK) {
                gainBuffer[i] = sustainLevel;
            }
        }
        
        envelope_gain_ = sustainLevel;
        release_start_gain_ = sustainLevel;
    } else {
        // Attack continues - update current envelope gain
        envelope_gain_ = gainBuffer[numSamples - 1];
        release_start_gain_ = envelope_gain_;
    }
    
    DEBUG_PRINT("Release start gain set to: " << release_start_gain_);
    return true;
}

bool Voice::processSustainPhase(float* gainBuffer, int numSamples) noexcept {
    DEBUG_PRINT("VoiceState::Sustaining");
    
    // ===== FILL BUFFER WITH CONSTANT SUSTAIN LEVEL =====
    
    const float sustainLevel = envelope_->getSustainLevel();
    
    for (int i = 0; i < numSamples; ++i) {
        gainBuffer[i] = sustainLevel;
    }
    
    envelope_gain_ = sustainLevel;
    release_start_gain_ = sustainLevel;
    
    DEBUG_PRINT("Release start gain set to: " << release_start_gain_);
    return true;
}

bool Voice::processReleasePhase(float* gainBuffer, int numSamples) noexcept {
    DEBUG_PRINT("VoiceState::Releasing");
    DEBUG_PRINT("Release start gain: " << release_start_gain_);
    
    // ===== GET RELEASE GAINS FROM ENVELOPE =====
    
    bool releaseContinues = envelope_->getReleaseGains(gainBuffer, numSamples, 
                                                     envelope_release_position_, sampleRate_);

    // ===== SCALE RELEASE GAINS TO MATCH START LEVEL =====
    
    // Release curve is 1.0 -> 0.0, but we need to scale it to
    // match the level at which release started (could be mid-attack)
    for (int i = 0; i < numSamples; ++i) {
        gainBuffer[i] *= release_start_gain_;
    }
    
    envelope_release_position_ += numSamples;
    envelope_gain_ = gainBuffer[numSamples - 1];
    
    // ===== CHECK IF RELEASE PHASE IS COMPLETE =====
    
    if (!releaseContinues || envelope_gain_ <= ENVELOPE_TRIGGERS_END_RELEASE) {
        // Transition to idle - voice is finished
        state_ = VoiceState::Idle;
        envelope_gain_ = 0.0f;
        return false;
    }
    
    return true;
}

// =====================================================================
// AUDIO RENDERING WITH GAIN CHAIN
// =====================================================================

void Voice::processAudioWithGains(float* outputLeft, float* outputRight, 
                                 const float* stereoBuffer, int samplesToProcess) noexcept {
    // ===== CALCULATE SOURCE POINTER =====
    
    const int startIndex = position_ * 2; // Convert position to stereo frame index
    const float* srcPtr = stereoBuffer + startIndex;
    
    // ===== CALCULATE PAN GAINS =====
    
    float pan_left_gain, pan_right_gain;
    calculatePanGains(pan_, pan_left_gain, pan_right_gain);

    // ===== APPLY COMPLETE GAIN CHAIN AND MIX TO OUTPUT =====
    
    // Full gain chain: sample * envelope * velocity * pan * master * stereoField
    // Stereo field gains are pre-calculated and cached for RT efficiency
    // Output is additive mixing (+=) to allow multiple voices
    for (int i = 0; i < samplesToProcess; ++i) {
        const int srcIndex = i * 2;
        
        // Left channel: apply full gain chain including stereo field
        outputLeft[i] += srcPtr[srcIndex] * gainBuffer_[i] * velocity_gain_ * 
                         pan_left_gain * master_gain_ * stereoFieldGainLeft_;
        
#if DEBUG_ENVELOPE_TO_RIGHT_CHANNEL
        // Debug mode: output envelope shape to right channel
        outputRight[i] += gainBuffer_[i];
#else
        // Normal mode: right channel with full gain chain including stereo field
        outputRight[i] += srcPtr[srcIndex + 1] * gainBuffer_[i] * velocity_gain_ * 
                          pan_right_gain * master_gain_ * stereoFieldGainRight_;
#endif
    }
}

// =====================================================================
// DAMPING BUFFER CAPTURE (RETRIGGER HANDLING)
// =====================================================================

void Voice::captureDampingBuffer() noexcept {
    // =====================================================================
    // RETRIGGER DAMPING BUFFER CAPTURE
    // =====================================================================
    // This function captures the current playback state into a pre-computed
    // buffer that will be mixed with the new note to eliminate clicks.
    // The buffer contains final audio samples with linear fade-out applied.
    // =====================================================================
    
    // ===== SAFETY CHECKS =====
    
    if (!instrument_ || dampingLength_ <= 0) {
        dampingActive_ = false;
        return;
    }
    
    // ===== GET SOURCE BUFFER =====
    
    const float* stereoBuffer = instrument_->get_sample_begin_pointer(currentVelocityLayer_);
    const int maxFrames = instrument_->get_frame_count(currentVelocityLayer_);
    
    if (!stereoBuffer || position_ >= maxFrames) {
        dampingActive_ = false;
        return;
    }
    
    // ===== CALCULATE AVAILABLE SAMPLES =====
    
    const int samplesAvailable = maxFrames - position_;
    const int samplesToCapture = std::min(dampingLength_, samplesAvailable);
    
    // ===== CALCULATE CURRENT GAIN PARAMETERS =====
    
    // Get pan gains using constant power panning
    float pan_left_gain, pan_right_gain;
    calculatePanGains(pan_, pan_left_gain, pan_right_gain);
    
    // Calculate base gain from all current voice parameters
    const float baseGain = envelope_gain_ * velocity_gain_ * master_gain_;
    
    // ===== FILL DAMPING BUFFER WITH PRE-COMPUTED AUDIO =====
    
    const int startIndex = position_ * 2;  // Convert to stereo frame index
    
    for (int i = 0; i < samplesToCapture; ++i) {
        // Linear damping gain: 1.0 at start -> 0.0 at end
        const float dampingGain = 1.0f - (static_cast<float>(i) / static_cast<float>(dampingLength_));
        
        // Combine all gains: base * damping * pan
        const float totalGain = baseGain * dampingGain;
        
        // Pre-compute final samples with all gain processing applied
        // This eliminates the need for any gain calculations during playback
        const int srcIndex = startIndex + (i * 2);
        dampingBufferLeft_[i] = stereoBuffer[srcIndex] * totalGain * pan_left_gain;
        dampingBufferRight_[i] = stereoBuffer[srcIndex + 1] * totalGain * pan_right_gain;
    }
    
    // ===== FILL REMAINING BUFFER WITH SILENCE =====
    
    // If we hit end of sample before filling entire damping buffer, pad with silence
    for (int i = samplesToCapture; i < dampingLength_; ++i) {
        dampingBufferLeft_[i] = 0.0f;
        dampingBufferRight_[i] = 0.0f;
    }
    
    // ===== ACTIVATE DAMPING PLAYBACK =====
    
    dampingPosition_ = 0;
    dampingActive_ = true;
}

// =====================================================================
// PANNING CALCULATION
// =====================================================================

void Voice::calculatePanGains(float pan, float& leftGain, float& rightGain) noexcept {
    // =====================================================================
    // CONSTANT POWER PANNING
    // =====================================================================
    // Uses sinusoidal curves to maintain constant perceived loudness
    // across the stereo field. Total power (L² + R²) remains constant.
    // Temporary implementation - will be moved to pan_utils.h
    // =====================================================================
    
    // ===== CLAMP PAN VALUE =====
    
    const float clamped_pan = std::max(-1.0f, std::min(1.0f, pan));
    
    // ===== CONVERT TO ANGLE =====
    
    // Map pan range -1..1 to angle 0..π/2
    const float normalized = (clamped_pan + 1.0f) * 0.5f;  // -1..1 → 0..1
    const float angle = normalized * (M_PI / 2.0f);         // 0..1 → 0..π/2
    
    // ===== CALCULATE CONSTANT POWER GAINS =====
    
    leftGain = std::cos(angle);   // 1.0 at left, 0.707 at center, 0.0 at right
    rightGain = std::sin(angle);  // 0.0 at left, 0.707 at center, 1.0 at right
}