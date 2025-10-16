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
      instrumentLoader_(nullptr),
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
      dampingActive_(false),
      stereoFieldGainLeft_(1.0f),
      stereoFieldGainRight_(1.0f),
      stereoFieldAmount_(0) {
    
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
      instrumentLoader_(nullptr),
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
      dampingActive_(false),
      stereoFieldGainLeft_(1.0f),
      stereoFieldGainRight_(1.0f),
      stereoFieldAmount_(0) {
    
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
                      const InstrumentLoader* instrumentLoader,
                      uint8_t attackMIDI, uint8_t releaseMIDI, uint8_t sustainMIDI) {
    instrument_ = &instrument;
    instrumentLoader_ = instrumentLoader;
    sampleRate_ = sampleRate;
    envelope_ = &envelope;
    
    // ===== PARAMETER VALIDATION =====
    
    if (sampleRate_ <= 0) {
        const std::string errorMsg = "[Voice/initialize] error: Invalid sampleRate " + 
                                   std::to_string(sampleRate_) + " - must be > 0";
        logger.log("Voice/initialize", LogSeverity::Error, errorMsg);
        throw std::invalid_argument("Invalid sample rate: " + std::to_string(sampleRate_));
    }
    
    if (!envelope_) {
        const std::string errorMsg = "[Voice/initialize] error: Envelope reference is null";
        logger.log("Voice/initialize", LogSeverity::Error, errorMsg);
        throw std::invalid_argument("Envelope reference is null");
    }
    
    if (!EnvelopeStaticData::isInitialized()) {
        const std::string errorMsg = "[Voice/initialize] error: EnvelopeStaticData not initialized";
        logger.log("Voice/initialize", LogSeverity::Error, errorMsg);
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
        logger.log("Voice/initialize", LogSeverity::Warning, errorMsg);
        gainBuffer_.reserve(16384);
    }
    
    // ===== LOG INITIALIZATION SUCCESS =====
    
    logger.log("Voice/initialize", LogSeverity::Info,
           "Voice initialized for MIDI " + std::to_string(midiNote_) +
           " with static envelope system (A:" + std::to_string(envelope_->getAttackLength(sampleRate_)) +
           ", R:" + std::to_string(envelope_->getReleaseLength(sampleRate_)) + " ms)" +
           " and damping buffer (" + std::to_string(dampingLength_) + " samples = " +
           std::to_string(DAMPING_RELEASE_MS) + "ms)");
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
    
    logger.log("Voice/cleanup", LogSeverity::Info,
           "Voice cleaned up and reset to idle for MIDI " + std::to_string(midiNote_));
}

void Voice::reinitialize(const Instrument& instrument, int sampleRate,
                         Envelope& envelope, Logger& logger,
                         const InstrumentLoader* instrumentLoader,
                         uint8_t attackMIDI, uint8_t releaseMIDI, uint8_t sustainMIDI) {
    // Full re-initialization including damping buffer recalculation
    initialize(instrument, sampleRate, envelope, logger, instrumentLoader, attackMIDI, releaseMIDI, sustainMIDI);

    logger.log("Voice/reinitialize", LogSeverity::Info,
           "Voice reinitialized with new instrument, sampleRate and ADSR envelope for MIDI " +
           std::to_string(midiNote_));
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

    // Select velocity layer based on MIDI velocity with dynamic layer count
    // Layer size adapts to actual velocity layer count (1-8 layers)
    float layerSize = getVelocityLayerSize();
    int maxLayer = (instrumentLoader_ ? instrumentLoader_->getVelocityLayerCount() : 8) - 1;
    currentVelocityLayer_ = std::min(
        static_cast<uint8_t>(velocity / layerSize),
        static_cast<uint8_t>(maxLayer)
    );
    
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

void Voice::setStereoFieldAmountMIDI(uint8_t midiValue) noexcept {
    stereoFieldAmount_ = midiValue;
    
    // Immediately calculate and cache stereo field gains
    // This is done here (not in startNote) for RT efficiency
    calculateStereoFieldGains();
}

void Voice::setMasterGain(float gain) noexcept {
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
    logger.log("Voice/getGainDebugInfo", LogSeverity::Info, info);
    
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
    // Uses dynamic layer size based on actual velocity layer count (1-8 layers)
    const float layerSize = getVelocityLayerSize();
    const float layerCenterOffset = getVelocityLayerCenterOffset();
    const float layerCenter = (currentVelocityLayer_ * layerSize) + layerCenterOffset;

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
    // Range is symmetric and adapts to layer size (e.g., -7.5 to +7.5 for 8 layers)
    const float layerHalfSize = getVelocityLayerHalfSize();
    const float positionInLayer = offsetFromCenter / layerHalfSize;
    
    // Apply configurable gain modulation based on position in layer
    // Default ±8% compensates for discrete layer selection while maintaining sample authenticity
    const float layerModulation = 1.0f + (positionInLayer * VELOCITY_LAYER_MODULATION);
    
    // ===== FINAL GAIN CALCULATION =====
    
    velocity_gain_ = baseGain * layerModulation;
    
    // Clamp to valid range (should always be in range, but safety first)
    velocity_gain_ = std::max(0.0f, std::min(1.0f, velocity_gain_));
}

void Voice::calculateStereoFieldGains() noexcept {
    // =====================================================================
    // STEREO FIELD GAIN CALCULATION
    // =====================================================================
    // Simulates physical piano string position in stereo image
    // Lower notes (bass) are positioned more to the left
    // Higher notes (treble) are positioned more to the right
    // Middle C remains centered
    // =====================================================================
    
    // ===== HANDLE DISABLED STATE =====
    
    if (stereoFieldAmount_ == 0) {
        // Stereo field disabled - neutral position
        stereoFieldGainLeft_ = 1.0f;
        stereoFieldGainRight_ = 1.0f;
        return;
    }
    
    // ===== HANDLE MIDDLE C (NO OFFSET) =====
    
    if (midiNote_ == MIDI_MIDDLE_C) {
        // Middle C stays centered regardless of stereo field amount
        stereoFieldGainLeft_ = 1.0f;
        stereoFieldGainRight_ = 1.0f;
        return;
    }
    
    // ===== CALCULATE DISTANCE FROM MIDDLE C =====
    
    const int distanceFromCenter = static_cast<int>(midiNote_) - MIDI_MIDDLE_C;
    
    // Normalize distance to -1.0 (lowest note) to +1.0 (highest note)
    float normalizedDistance;
    if (distanceFromCenter < 0) {
        // Lower notes: MIDI 21-59 (39 semitones below Middle C)
        normalizedDistance = static_cast<float>(distanceFromCenter) / 39.0f;
    } else {
        // Higher notes: MIDI 61-108 (48 semitones above Middle C)
        normalizedDistance = static_cast<float>(distanceFromCenter) / 48.0f;
    }
    
    // ===== APPLY STEREO FIELD INTENSITY =====
    
    // Scale by MIDI amount (0-127)
    const float fieldIntensity = stereoFieldAmount_ / 127.0f;
    
    // Calculate final stereo offset: -0.2 to +0.2 (±20% at maximum intensity)
    const float stereoOffset = normalizedDistance * fieldIntensity * STEREO_FIELD_MAX_OFFSET;
    
    // ===== CALCULATE CHANNEL GAINS (BALANCE MODE) =====
    
    // Balance mode: one channel boosted, other reduced
    // Maintains total perceived loudness while creating stereo width
    if (stereoOffset < 0.0f) {
        // Lower notes: boost left, reduce right
        stereoFieldGainLeft_ = 1.0f + std::abs(stereoOffset);  // up to 1.2
        stereoFieldGainRight_ = 1.0f - std::abs(stereoOffset); // down to 0.8
    } else {
        // Higher notes: boost right, reduce left
        stereoFieldGainLeft_ = 1.0f - stereoOffset;  // down to 0.8
        stereoFieldGainRight_ = 1.0f + stereoOffset; // up to 1.2
    }
    
    // ===== SAFETY CLAMP =====

    // Ensure gains stay within expected range
    stereoFieldGainLeft_ = std::max(0.8f, std::min(1.2f, stereoFieldGainLeft_));
    stereoFieldGainRight_ = std::max(0.8f, std::min(1.2f, stereoFieldGainRight_));
}

// =====================================================================
// DYNAMIC VELOCITY LAYER CALCULATION
// =====================================================================

float Voice::getVelocityLayerSize() const noexcept {
    // If instrumentLoader is available, use actual velocity layer count
    // Otherwise fallback to default 8 layers (16 MIDI values per layer)
    if (instrumentLoader_) {
        int layerCount = instrumentLoader_->getVelocityLayerCount();
        return 128.0f / static_cast<float>(layerCount);
    }
    return VELOCITY_LAYER_SIZE; // Fallback to static value (16.0f)
}

float Voice::getVelocityLayerHalfSize() const noexcept {
    return getVelocityLayerSize() / 2.0f;
}

float Voice::getVelocityLayerCenterOffset() const noexcept {
    // Center offset is (layerSize - 1) / 2
    // For 8 layers: (16-1)/2 = 7.5
    // For 4 layers: (32-1)/2 = 15.5
    // For 2 layers: (64-1)/2 = 31.5
    // For 1 layer: (128-1)/2 = 63.5
    float layerSize = getVelocityLayerSize();
    return (layerSize - 1.0f) / 2.0f;
}

