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

// ===== DEBUG CONTROL =====
#define VOICE_DEBUG_ENABLED 0

#if VOICE_DEBUG_ENABLED
    #define DEBUG_PRINT(x) std::cout << x << std::endl
#else
    #define DEBUG_PRINT(x) do {} while(0)
#endif

// ===== STATIC MEMBER INITIALIZATION =====
std::atomic<bool> Voice::rtMode_{false};

// =====================================================================
// CONSTRUCTORS
// =====================================================================

Voice::Voice() 
    : midiNote_(0), 
      instrument_(nullptr), 
      sampleRate_(0),
      state_(VoiceState::Idle), 
      position_(0), 
      currentVelocityLayer_(0),
      envelope_gain_(0.0f), 
      velocity_gain_(0.0f), 
      master_gain_(1.0f),
      pan_(0.0f),
      envelope_(nullptr),
      envelope_attack_position_(0), 
      envelope_release_position_(0), 
      release_start_gain_(1.0f),
      dampingLength_(0),
      dampingPosition_(0),
      dampingActive_(false) {
    
    // Pre-allocate gain buffer with large reserve (64KB for maximum buffer size)
    gainBuffer_.reserve(16384);
    
    // Pre-allocate damping buffers (will be properly sized in initialize())
    // Reserve ~21ms @ 48kHz = 1024 samples
    dampingBufferLeft_.reserve(1024);
    dampingBufferRight_.reserve(1024);
}

Voice::Voice(uint8_t midiNote)
    : midiNote_(midiNote), 
      instrument_(nullptr), 
      sampleRate_(0),
      state_(VoiceState::Idle), 
      position_(0), 
      currentVelocityLayer_(0),
      envelope_gain_(0.0f), 
      velocity_gain_(0.0f), 
      master_gain_(1.0f),
      pan_(0.0f),
      envelope_(nullptr),
      envelope_attack_position_(0), 
      envelope_release_position_(0), 
      release_start_gain_(1.0f),
      dampingLength_(0),
      dampingPosition_(0),
      dampingActive_(false) {
    
    // Pre-allocate gain buffer with large reserve (64KB for maximum buffer size)
    gainBuffer_.reserve(16384);
    
    // Pre-allocate damping buffers (will be properly sized in initialize())
    // Reserve ~21ms @ 48kHz = 1024 samples
    dampingBufferLeft_.reserve(1024);
    dampingBufferRight_.reserve(1024);
}

// =====================================================================
// INITIALIZATION AND LIFECYCLE
// =====================================================================

void Voice::initialize(const Instrument& instrument, int sampleRate, 
                      Envelope& envelope, Logger& logger,
                      uint8_t attackMIDI, uint8_t releaseMIDI, uint8_t sustainMIDI) {
    instrument_ = &instrument;
    sampleRate_ = sampleRate;
    envelope_ = &envelope;
    
    // ===== PARAMETER VALIDATION =====
    
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
    
    // ===== CALCULATE DAMPING BUFFER LENGTH =====
    
    // Convert milliseconds to samples based on sample rate
    dampingLength_ = static_cast<int>((DAMPING_RELEASE_MS / 1000.0f) * sampleRate_);
    
    // Ensure damping buffers have sufficient capacity
    if (dampingBufferLeft_.capacity() < static_cast<size_t>(dampingLength_)) {
        dampingBufferLeft_.reserve(dampingLength_);
        dampingBufferRight_.reserve(dampingLength_);
    }
    
    // Resize to exact length needed
    dampingBufferLeft_.resize(dampingLength_);
    dampingBufferRight_.resize(dampingLength_);
    
    // ===== RESET STATE =====
    
    // Reset all states for clean start
    resetVoiceState();
    
    // ===== CONFIGURE ENVELOPE =====
    
    envelope_->setAttackMIDI(attackMIDI);
    envelope_->setReleaseMIDI(releaseMIDI);
    envelope_->setSustainLevelMIDI(sustainMIDI);

    // ===== VERIFY GAIN BUFFER =====
    
    if (gainBuffer_.capacity() < 16384) {
        const std::string errorMsg = "[Voice/initialize] warning: gainBuffer capacity insufficient, attempting reserve";
        logSafe("Voice/initialize", "warning", errorMsg, logger);
        gainBuffer_.reserve(16384);
    }
    
    // ===== LOG INITIALIZATION SUCCESS =====
    
    logSafe("Voice/initialize", "info", 
           "Voice initialized for MIDI " + std::to_string(midiNote_) + 
           " with static envelope system (A:" + std::to_string(envelope_->getAttackLength(sampleRate_)) + 
           ", R:" + std::to_string(envelope_->getReleaseLength(sampleRate_)) + " ms)" +
           " and damping buffer (" + std::to_string(dampingLength_) + " samples = " + 
           std::to_string(DAMPING_RELEASE_MS) + "ms)", logger);
}

void Voice::prepareToPlay(int maxBlockSize) noexcept {
    // ===== BUFFER SIZE VALIDATION =====
    
    // Critical errors must be logged even in RT context for visibility
    if (maxBlockSize > 16384) {
        std::cout << "[Voice/prepareToPlay] error: Block size " << maxBlockSize 
                  << " exceeds pre-allocated buffer capacity 16384" << std::endl;
        return; // Fail gracefully but visibly
    }
    
    // ===== RESIZE GAIN BUFFER IF NEEDED =====
    
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
    // Full re-initialization including damping buffer recalculation
    initialize(instrument, sampleRate, envelope, logger, attackMIDI, releaseMIDI, sustainMIDI);

    logSafe("Voice/reinitialize", "info", 
           "Voice reinitialized with new instrument, sampleRate and ADSR envelope for MIDI " + 
           std::to_string(midiNote_), logger);
}

// =====================================================================
// NOTE CONTROL
// =====================================================================

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

void Voice::startNote(uint8_t velocity) noexcept {
    // =====================================================================
    // RETRIGGER DETECTION AND DAMPING BUFFER CAPTURE
    // =====================================================================
    
    // If voice is already playing (not idle), this is a retrigger
    // Capture current audio state into damping buffer for click-free transition
    if (state_ != VoiceState::Idle) {
        captureDampingBuffer();
    }
    
    // =====================================================================
    // START NEW NOTE
    // =====================================================================
    
    // Select velocity layer based on MIDI velocity (0-127 mapped to 0-7)
    currentVelocityLayer_ = std::min(static_cast<uint8_t>(velocity / 16), static_cast<uint8_t>(7));
    
    // Update velocity gain with logarithmic scaling
    updateVelocityGain(velocity);
    
    // Initialize attack phase
    state_ = VoiceState::Attacking;
    position_ = 0;
    envelope_gain_ = 0.0f;
    envelope_attack_position_ = 0;
}

void Voice::stopNote() noexcept {
    // ===== INITIATE RELEASE PHASE =====
    
    // Only transition to release if currently attacking or sustaining
    if (state_ == VoiceState::Attacking || state_ == VoiceState::Sustaining) {
        DEBUG_PRINT("Voice state set to: Releasing");
        
        state_ = VoiceState::Releasing;
        envelope_release_position_ = 0;
        release_start_gain_ = envelope_gain_; // Capture current gain for scaled release
        
        DEBUG_PRINT("Release start gain set to: " << release_start_gain_);
    }
}

// =====================================================================
// ENVELOPE CONTROL
// =====================================================================

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

// =====================================================================
// GAIN CONTROL
// =====================================================================

void Voice::setPan(float pan) noexcept {
    pan_ = pan;
}

void Voice::setMasterGain(float gain, Logger& logger) {
    // ===== VALIDATE GAIN RANGE =====
    
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
    // RT-safe version: silent validation without logging
    if (gain >= 0.0f && gain <= 1.0f) {
        master_gain_ = gain;
    }
}

// =====================================================================
// DEBUG AND DIAGNOSTICS
// =====================================================================

std::string Voice::getGainDebugInfo(Logger& logger) const {
    std::ostringstream oss;
    
    oss << "MIDI " << static_cast<int>(midiNote_) 
        << " | State: " << static_cast<int>(state_)
        << " | Gains - Envelope: " << envelope_gain_
        << ", Velocity: " << velocity_gain_
        << ", Master: " << master_gain_
        << ", Pan: " << pan_;
    
    if (dampingActive_) {
        oss << " | Damping: Active (" << dampingPosition_ << "/" << dampingLength_ << ")";
    }
    
    std::string info = oss.str();
    logSafe("Voice/getGainDebugInfo", "info", info, logger);
    
    return info;
}

// =====================================================================
// PRIVATE HELPER METHODS
// =====================================================================

void Voice::resetVoiceState() noexcept {
    // ===== RESET MAIN VOICE STATE =====
    
    state_ = VoiceState::Idle;
    position_ = 0;
    currentVelocityLayer_ = 0;
    
    // ===== RESET GAIN VALUES =====
    
    master_gain_ = 1.0f;
    velocity_gain_ = 0.0f;
    envelope_gain_ = 0.0f;
    pan_ = 0.0f;
    
    // ===== RESET ENVELOPE STATE =====
    
    envelope_attack_position_ = 0;
    envelope_release_position_ = 0;
    release_start_gain_ = 1.0f;
    
    // ===== RESET DAMPING STATE =====
    
    dampingPosition_ = 0;
    dampingActive_ = false;
}

bool Voice::isVoiceReady() const noexcept {
    return instrument_ && sampleRate_ > 0 && envelope_;
}

void Voice::updateVelocityGain(uint8_t velocity) noexcept {
    // ===== HANDLE ZERO VELOCITY =====
    
    if (velocity == 0) {
        velocity_gain_ = 0.0f;
        return;
    }
    
    // ===== LAYER CENTER CALCULATION =====
    
    // Each layer represents a specific dynamic range in the sampled instrument
    // Samples are already recorded at their natural dynamic level
    // Calculate exact center of the selected velocity layer
    const float layerCenter = (currentVelocityLayer_ * VELOCITY_LAYER_SIZE) + VELOCITY_LAYER_CENTER_OFFSET;
    
    // ===== BASE GAIN FOR LAYER CENTER =====
    
    // Calculate base gain for the center of this layer
    // Uses logarithmic curve for natural velocity response
    const float normalizedCenter = layerCenter / 127.0f;
    const float baseGain = std::sqrt(normalizedCenter);
    
    // ===== SYMMETRIC MODULATION WITHIN LAYER =====
    
    // Fine-tune gain based on position within the layer
    // This provides smooth transitions within the selected velocity layer
    const float offsetFromCenter = static_cast<float>(velocity) - layerCenter;
    
    // Normalize to -1.0 to +1.0 range (divide by half size for symmetry)
    // Range is symmetric: -7.5 to +7.5 MIDI values from center
    const float positionInLayer = offsetFromCenter / VELOCITY_LAYER_HALF_SIZE;
    
    // Apply configurable gain modulation based on position in layer
    // Default ±8% compensates for discrete layer selection while maintaining sample authenticity
    const float layerModulation = 1.0f + (positionInLayer * VELOCITY_LAYER_MODULATION);
    
    // ===== FINAL GAIN CALCULATION =====
    
    velocity_gain_ = baseGain * layerModulation;
    
    // Clamp to valid range (should always be in range, but safety first)
    velocity_gain_ = std::max(0.0f, std::min(1.0f, velocity_gain_));
}

void Voice::logSafe(const std::string& component, const std::string& severity, 
                   const std::string& message, Logger& logger) const {
    // ===== CHOOSE LOGGING METHOD BASED ON RT MODE =====
    
    if (!rtMode_.load()) {
        // Non-RT context: use proper logger with full functionality
        logger.log(component, severity, message);
    } else {
        // RT context: use lightweight stdout logging for visibility
        // Avoids potential allocations and locks in logger
        std::cout << "[" << component << "] " << severity << ": " << message << std::endl;
    }
}