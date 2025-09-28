#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>

#ifndef M_PI
#define M_PI 3.14159f
#endif

#include "IthacaConfig.h"
#include "voice.h"
#include "envelopes/envelope_static_data.h"

// Debug control
#define VOICE_DEBUG_ENABLED 0

#if VOICE_DEBUG_ENABLED
    #define DEBUG_PRINT(x) std::cout << x << std::endl
    #define DEBUG_ENVELOPE_TO_RIGHT_CHANNEL 1
#else
    #define DEBUG_PRINT(x) do {} while(0)
    #define DEBUG_ENVELOPE_TO_RIGHT_CHANNEL 0
#endif

// Static member initialization
std::atomic<bool> Voice::rtMode_{false};

// ===== CONSTRUCTORS =====

Voice::Voice() : midiNote_(0), instrument_(nullptr), sampleRate_(0),
                 state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
                 envelope_gain_(0.0f), velocity_gain_(0.0f), master_gain_(1.0f),
                 pan_(0),
                 envelope_(nullptr),
                 envelope_attack_position_(0), envelope_release_position_(0), 
                 release_start_gain_(1.0f) {
    
    // Pre-allocate buffer with large reserve (64KB for maximum buffer size)
    gainBuffer_.reserve(16384);
}

Voice::Voice(uint8_t midiNote)
    : midiNote_(midiNote), instrument_(nullptr), sampleRate_(0),
      state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
      envelope_gain_(0.0f), velocity_gain_(0.0f), master_gain_(1.0f),
      pan_(0),
      envelope_(nullptr),
      envelope_attack_position_(0), envelope_release_position_(0), 
      release_start_gain_(1.0f) {
    
    // Pre-allocate buffer with large reserve (64KB for maximum buffer size)
    gainBuffer_.reserve(16384);
}

// ===== INITIALIZATION AND LIFECYCLE =====

void Voice::initialize(const Instrument& instrument, int sampleRate, 
                      Envelope& envelope, Logger& logger,
                      uint8_t attackMIDI, uint8_t releaseMIDI, uint8_t sustainMIDI) {
    instrument_ = &instrument;
    sampleRate_ = sampleRate;
    envelope_ = &envelope;
    
    // Validate parameters with unified error handling
    if (sampleRate_ <= 0) {
        const std::string errorMsg = "[Voice/initialize] error: Invalid sampleRate " + 
                                   std::to_string(sampleRate_) + " - must be > 0";
        logSafe("Voice/initialize", "error", errorMsg, logger);
        throw std::invalid_argument("Invalid sample rate: " + std::to_string(sampleRate_));
    }
    
    if (!envelope_) {
        const std::string errorMsg = "[Voice/initialize] error: Envelope reference is null";
        logSafe("Voice/initialize", "error", errorMsg, logger);
        throw std::invalid_argument("Envelope reference is null");
    }
    
    if (!EnvelopeStaticData::isInitialized()) {
        const std::string errorMsg = "[Voice/initialize] error: EnvelopeStaticData not initialized";
        logSafe("Voice/initialize", "error", errorMsg, logger);
        throw std::runtime_error("EnvelopeStaticData not initialized. Call EnvelopeStaticData::initialize() first");
    }
    
    // Reset all states for clean start
    resetVoiceState();
    
    // Configure envelope parameters
    envelope_->setAttackMIDI(attackMIDI);
    envelope_->setReleaseMIDI(releaseMIDI);
    envelope_->setSustainLevelMIDI(sustainMIDI);

    // Verify buffer is pre-allocated
    if (gainBuffer_.capacity() < 16384) {
        const std::string errorMsg = "[Voice/initialize] warning: gainBuffer capacity insufficient, attempting reserve";
        logSafe("Voice/initialize", "warning", errorMsg, logger);
        gainBuffer_.reserve(16384);
    }
    
    logSafe("Voice/initialize", "info", 
           "Voice initialized for MIDI " + std::to_string(midiNote_) + 
           " with static envelope system (A:" + std::to_string(envelope_->getAttackLength(sampleRate_)) + 
           ", R:" + std::to_string(envelope_->getReleaseLength(sampleRate_)) + " ms)", logger);
}

void Voice::prepareToPlay(int maxBlockSize) noexcept {
    // Buffer size validation - critical errors must be logged even in RT
    if (maxBlockSize > 16384) {
        std::cout << "[Voice/prepareToPlay] error: Block size " << maxBlockSize 
                  << " exceeds pre-allocated buffer capacity 16384" << std::endl;
        return; // Fail gracefully but visibly
    }
    
    // Resize only if buffer is smaller and we're in safe context
    if (gainBuffer_.size() < static_cast<size_t>(maxBlockSize)) {
        gainBuffer_.resize(maxBlockSize);
    }
}

void Voice::cleanup(Logger& logger) {
    resetVoiceState();
    
    logSafe("Voice/cleanup", "info", 
           "Voice cleaned up and reset to idle for MIDI " + std::to_string(midiNote_), logger);
}

void Voice::reinitialize(const Instrument& instrument, int sampleRate,
                         Envelope& envelope, Logger& logger,
                         uint8_t attackMIDI, uint8_t releaseMIDI, uint8_t sustainMIDI) {
    initialize(instrument, sampleRate, envelope, logger, attackMIDI, releaseMIDI, sustainMIDI);

    logSafe("Voice/reinitialize", "info", 
           "Voice reinitialized with new instrument, sampleRate and ADSR envelope for MIDI " + std::to_string(midiNote_), logger);
}

// ===== NOTE CONTROL =====

void Voice::setNoteState(bool isOn, uint8_t velocity) noexcept {
    if (!isVoiceReady()) return;
    
    if (isOn) {
        startNote(velocity);
    } else {
        stopNote();
    }
}

void Voice::setNoteState(bool isOn) noexcept {
    if (!isVoiceReady()) return;

    if (isOn) {
        startNote(DEFAULT_VELOCITY);
    } else {
        stopNote();
    }
}

// ===== ENVELOPE CONTROL =====

void Voice::setAttackMIDI(uint8_t midi_value) noexcept {
    if (envelope_) {
        envelope_->setAttackMIDI(midi_value);
    }
}

void Voice::setReleaseMIDI(uint8_t midi_value) noexcept {
    if (envelope_) {
        envelope_->setReleaseMIDI(midi_value);
    }
}

void Voice::setSustainLevelMIDI(uint8_t midi_value) noexcept {
    if (envelope_) {
        envelope_->setSustainLevelMIDI(midi_value);
    }
}

// ===== AUDIO PROCESSING =====

bool Voice::calculateBlockGains(float* gainBuffer, int numSamples) noexcept {
    if (state_ == VoiceState::Idle || !gainBuffer || numSamples <= 0) {
        return false;
    }
    
    // Buffer overflow protection - critical error must be visible
    if (static_cast<size_t>(numSamples) > gainBuffer_.capacity()) {
        std::cout << "[Voice/calculateBlockGains] error: Buffer overflow - requested " 
                  << numSamples << " samples, capacity " << gainBuffer_.capacity() << std::endl;
        return false; // Fail gracefully but visibly
    }
    
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

bool Voice::processBlock(float* outputLeft, float* outputRight, int samplesPerBlock) noexcept {
    // Early exit conditions
    if (!isVoiceReady() || state_ == VoiceState::Idle) {
        return false;
    }
    
    if (!outputLeft || !outputRight || samplesPerBlock <= 0) {
        return false;
    }
    
    // Get instrument data
    const float* stereoBuffer = instrument_->get_sample_begin_pointer(currentVelocityLayer_);
    const int maxFrames = instrument_->get_frame_count(currentVelocityLayer_);

    if (!stereoBuffer || maxFrames == 0) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    // Calculate samples to process
    const int samplesUntilEnd = maxFrames - position_;
    const int samplesToProcess = std::min(samplesPerBlock, samplesUntilEnd);
    
    // Verify buffer capacity - critical error must be visible
    if (gainBuffer_.capacity() < static_cast<size_t>(samplesToProcess)) {
        std::cout << "[Voice/processBlock] error: Buffer capacity insufficient - need " 
                  << samplesToProcess << " samples, have " << gainBuffer_.capacity() << std::endl;
        return false; // Buffer capacity insufficient - visible failure
    }
    
    // Ensure buffer is sized correctly for this block
    if (gainBuffer_.size() < static_cast<size_t>(samplesToProcess)) {
        gainBuffer_.resize(samplesToProcess);
    }
    
    // Calculate envelope gains for the block
    if (!calculateBlockGains(gainBuffer_.data(), samplesToProcess)) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    // Apply audio processing with gain chain
    processAudioWithGains(outputLeft, outputRight, stereoBuffer, samplesToProcess);
    
    // Update position
    position_ += samplesToProcess;
    
    // Check for end condition
    if (position_ >= maxFrames) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    return state_ != VoiceState::Idle;
}

// ===== GAIN CONTROL =====

void Voice::setPan(float pan) noexcept {
    pan_ = pan;
}

void Voice::setMasterGain(float gain, Logger& logger) {
    if (gain < 0.0f || gain > 1.0f) {
        const std::string errorMsg = "[Voice/setMasterGain] error: Invalid master gain " + 
                                   std::to_string(gain) + " (must be 0.0-1.0) for MIDI " + 
                                   std::to_string(midiNote_);
        logSafe("Voice/setMasterGain", "error", errorMsg, logger);
        return;
    }
    master_gain_ = gain;
    logSafe("Voice/setMasterGain", "info", 
           "Master gain set to " + std::to_string(master_gain_) + 
           " for MIDI " + std::to_string(midiNote_), logger);
}

void Voice::setMasterGainRTSafe(float gain) noexcept {
    if (gain >= 0.0f && gain <= 1.0f) {
        master_gain_ = gain;
    }
}

// ===== DEBUG AND DIAGNOSTICS =====

std::string Voice::getGainDebugInfo(Logger& logger) const {
    std::string info = "MIDI " + std::to_string(midiNote_) + 
                      " Gains - Envelope: " + std::to_string(envelope_gain_) +
                      ", Velocity: " + std::to_string(velocity_gain_) +
                      ", Master: " + std::to_string(master_gain_) +
                      ", State: " + std::to_string(static_cast<int>(state_));
    
    logSafe("Voice/getGainDebugInfo", "info", info, logger);
    return info;
}

// ===== PRIVATE HELPER METHODS =====

void Voice::resetVoiceState() noexcept {
    state_ = VoiceState::Idle;
    position_ = 0;
    currentVelocityLayer_ = 0;
    master_gain_ = 1.0f;
    velocity_gain_ = 0.0f;
    envelope_gain_ = 0.0f;
    pan_ = 0.0f;
    envelope_attack_position_ = 0;
    envelope_release_position_ = 0;
    release_start_gain_ = 1.0f;
}

bool Voice::isVoiceReady() const noexcept {
    return instrument_ && sampleRate_ > 0 && envelope_;
}

void Voice::startNote(uint8_t velocity) noexcept {
    currentVelocityLayer_ = std::min(static_cast<uint8_t>(velocity / 16), static_cast<uint8_t>(7));
    updateVelocityGain(velocity);
    state_ = VoiceState::Attacking;
    position_ = 0;
    envelope_gain_ = 0.0f;
    envelope_attack_position_ = 0;
}

void Voice::stopNote() noexcept {
    if (state_ == VoiceState::Attacking || state_ == VoiceState::Sustaining) {
        DEBUG_PRINT("Voice state set to: Releasing");
        state_ = VoiceState::Releasing;
        envelope_release_position_ = 0;
        release_start_gain_ = envelope_gain_;
        DEBUG_PRINT("Release start gain set to: " << release_start_gain_);
    }
}

void Voice::updateVelocityGain(uint8_t velocity) noexcept {
    if (velocity == 0) {
        velocity_gain_ = 0.0f;
        return;
    }
    
    // Logarithmic curve for more natural velocity response
    float normalized = static_cast<float>(velocity) / 127.0f;
    velocity_gain_ = std::sqrt(normalized);
    velocity_gain_ = std::max(0.0f, std::min(1.0f, velocity_gain_));
}

bool Voice::processAttackPhase(float* gainBuffer, int numSamples) noexcept {
    DEBUG_PRINT("VoiceState::Attacking");
    bool attackContinues = envelope_->getAttackGains(gainBuffer, numSamples, 
                                                   envelope_attack_position_, sampleRate_);
    
    envelope_attack_position_ += numSamples;
    
    // Check if attack phase is complete
    if (!attackContinues || gainBuffer[numSamples - 1] >= ENVELOPE_TRIGGERS_END_ATTACK) {
        state_ = VoiceState::Sustaining;
        // Fill block with sustain values
        const float sustainLevel = envelope_gain_;
        for (int i = 0; i < numSamples; ++i) {
            if (gainBuffer[i] >= ENVELOPE_TRIGGERS_END_ATTACK) {
                gainBuffer[i] = sustainLevel;
            }
        }
        release_start_gain_ = sustainLevel;
    } else {
        envelope_gain_ = gainBuffer[numSamples - 1];
        release_start_gain_ = envelope_gain_;
    }
    DEBUG_PRINT("Release start gain set to: " << release_start_gain_);
    return true;
}

bool Voice::processSustainPhase(float* gainBuffer, int numSamples) noexcept {
    DEBUG_PRINT("VoiceState::Sustaining");
    const float sustainLevel = envelope_->getSustainLevel();
    for (int i = 0; i < numSamples; ++i) {
        gainBuffer[i] = sustainLevel;
    }
    envelope_gain_ = sustainLevel;
    release_start_gain_ = envelope_gain_;
    DEBUG_PRINT("Release start gain set to: " << release_start_gain_);
    return true;
}

bool Voice::processReleasePhase(float* gainBuffer, int numSamples) noexcept {
    DEBUG_PRINT("VoiceState::Releasing");
    DEBUG_PRINT("Release start gain: " << release_start_gain_);
    
    bool releaseContinues = envelope_->getReleaseGains(gainBuffer, numSamples, 
                                                     envelope_release_position_, sampleRate_);

    // Scale release gain according to last value
    for (int i = 0; i < numSamples; ++i) {
        gainBuffer[i] *= release_start_gain_;
    }
    
    envelope_release_position_ += numSamples;
    envelope_gain_ = gainBuffer[numSamples - 1];
    
    // Transition to idle when release ends
    if (!releaseContinues || envelope_gain_ <= ENVELOPE_TRIGGERS_END_RELEASE) {
        state_ = VoiceState::Idle;
        envelope_gain_ = 0.0f;
        return false;
    }
    return true;
}

void Voice::processAudioWithGains(float* outputLeft, float* outputRight, 
                                 const float* stereoBuffer, int samplesToProcess) noexcept {
    const int startIndex = position_ * 2; // Convert position to stereo frame index
    const float* srcPtr = stereoBuffer + startIndex;
    
    // Calculate pan gains using constant power panning
    float pan_left_gain, pan_right_gain;
    calculatePanGains(pan_, pan_left_gain, pan_right_gain);

    // Apply gain chain: sample * envelope * velocity * pan * master
    for (int i = 0; i < samplesToProcess; ++i) {
        const int srcIndex = i * 2;
        
        outputLeft[i] += srcPtr[srcIndex] * gainBuffer_[i] * pan_left_gain * master_gain_;
        
#if DEBUG_ENVELOPE_TO_RIGHT_CHANNEL
        outputRight[i] += gainBuffer_[i]; // Debug: envelope to right channel
#else
        outputRight[i] += srcPtr[srcIndex + 1] * gainBuffer_[i] * pan_right_gain * master_gain_;
#endif
    }
}

void Voice::calculatePanGains(float pan, float& leftGain, float& rightGain) noexcept {
    // Constant power panning implementation
    // Temporary implementation - will be moved to pan_utils.h
    float clamped_pan = std::max(-1.0f, std::min(1.0f, pan));
    
    // Convert to angle (0 to π/2)
    float normalized = (clamped_pan + 1.0f) * 0.5f;  // -1..1 → 0..1
    float angle = normalized * (M_PI / 2.0f);        // 0..1 → 0..π/2
    
    // Constant power panning
    leftGain = std::cos(angle);   // 1.0 at left, 0.707 at center, 0.0 at right
    rightGain = std::sin(angle);  // 0.0 at left, 0.707 at center, 1.0 at right
}

void Voice::logSafe(const std::string& component, const std::string& severity, 
                   const std::string& message, Logger& logger) const {
    if (!rtMode_.load()) {
        // Non-RT context: use proper logger
        logger.log(component, severity, message);
    } else {
        // RT context or development: always log to stdout for visibility
        std::cout << "[" << component << "] " << severity << ": " << message << std::endl;
    }
}