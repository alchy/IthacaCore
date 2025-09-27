#ifndef VOICE_H
#define VOICE_H

#include "IthacaConfig.h"
#include <cstdint>
#include <atomic>
#include <cstring>
#include <vector>
#include "instrument_loader.h"
#include "core_logger.h"
#include "envelopes/envelope.h"


// Simulation of JUCE AudioBuffer for stereo output (without JUCE dependency)
struct AudioData {
    float left;
    float right;
    AudioData() : left(0.0f), right(0.0f) {}
    AudioData(float l, float r) : left(l), right(r) {}
};

/**
 * @enum VoiceState
 * @brief Voice lifecycle states for envelope and processing control
 */
enum class VoiceState {
    Idle = 0,       
    Attacking = 1,    
    Sustaining = 2, 
    Releasing = 3   
};

/**
 * @class Voice
 * @brief Single voice unit for sample playback with envelope and state management
 * 
 * Features:
 * - RT-safe audio processing with pre-allocated buffers
 * - ADSR envelope with separate attack/sustain/release phases
 * - Velocity layers and gain control
 * - Constant power panning
 * - Comprehensive error handling and logging
 * 
 * Thread Safety:
 * - processBlock() and related RT methods are fully RT-safe
 * - initialize/cleanup methods may log and should be called from non-RT thread
 * - Atomic RT mode flag controls logging behavior
 */
class Voice {
public:
    // ===== CONSTRUCTORS =====
    
    /**
     * @brief Default constructor with pre-allocated 64KB gain buffer
     */
    Voice();

    /**
     * @brief Constructor with MIDI note assignment
     * @param midiNote MIDI note number (0-127)
     */
    Voice(uint8_t midiNote);

    // ===== INITIALIZATION AND LIFECYCLE =====

    /**
     * @brief Initialize voice with instrument and envelope configuration
     * @param instrument Reference to Instrument data
     * @param sampleRate Sample rate in Hz (44100 or 48000)
     * @param envelope Reference to Envelope processor
     * @param logger Reference to Logger
     * @param attackMIDI Initial attack MIDI value (0-127)
     * @param releaseMIDI Initial release MIDI value (0-127)
     * @param sustainMIDI Initial sustain MIDI value (0-127)
     * @throws std::invalid_argument for invalid parameters
     * @throws std::runtime_error if EnvelopeStaticData not initialized
     */
    void initialize(const Instrument& instrument, int sampleRate, Envelope& envelope, Logger& logger,
               uint8_t attackMIDI = 0, uint8_t releaseMIDI = 16, uint8_t sustainMIDI = 127);

    /**
     * @brief Prepare voice for audio processing with specified buffer size
     * @param maxBlockSize Maximum expected block size from DAW
     * @note RT-safe: only validates, no allocations if pre-allocation sufficient
     */
    void prepareToPlay(int maxBlockSize) noexcept;

    /**
     * @brief Clean up and reset voice to idle state
     * @param logger Reference to Logger
     */
    void cleanup(Logger& logger);

    /**
     * @brief Reinitialize voice with new configuration
     * @param instrument New Instrument data
     * @param sampleRate New sample rate
     * @param envelope New Envelope processor
     * @param logger Reference to Logger
     * @param attackMIDI Attack MIDI value (0-127)
     * @param releaseMIDI Release MIDI value (0-127)
     * @param sustainMIDI Sustain MIDI value (0-127)
     */
    void reinitialize(const Instrument& instrument, int sampleRate, Envelope& envelope, Logger& logger,
                 uint8_t attackMIDI = 0, uint8_t releaseMIDI = 16, uint8_t sustainMIDI = 127);

    // ===== NOTE CONTROL =====

    /**
     * @brief Set note state with velocity control
     * @param isOn true for note-on, false for note-off
     * @param velocity MIDI velocity (0-127) affecting volume
     * @note RT-safe: no allocations, only state changes
     */
    void setNoteState(bool isOn, uint8_t velocity) noexcept;

    /**
     * @brief Set note state with default velocity
     * @param isOn true for note-on, false for note-off
     * @note RT-safe: uses DEFAULT_VELOCITY for note-on
     */
    void setNoteState(bool isOn) noexcept;

    // ===== ENVELOPE CONTROL =====

    /**
     * @brief Set attack time via MIDI value
     * @param midi_value MIDI value (0-127) for attack speed
     * @note RT-safe
     */
    void setAttackMIDI(uint8_t midi_value) noexcept;

    /**
     * @brief Set release time via MIDI value
     * @param midi_value MIDI value (0-127) for release speed
     * @note RT-safe
     */
    void setReleaseMIDI(uint8_t midi_value) noexcept;

    /**
     * @brief Set sustain level via MIDI value
     * @param midi_value MIDI value (0-127) for sustain level
     * @note RT-safe
     */
    void setSustainLevelMIDI(uint8_t midi_value) noexcept;

    // ===== AUDIO PROCESSING =====

    /**
     * @brief Calculate envelope gains for audio block
     * 
     * Separates gain calculation from audio processing for modularity.
     * Handles all envelope states and voice lifecycle transitions.
     * 
     * @param gainBuffer Pre-allocated buffer for per-sample gains
     * @param numSamples Number of samples to calculate
     * @return true if voice remains active, false if should be deactivated
     * @note RT-safe with buffer overflow protection
     */
    bool calculateBlockGains(float* gainBuffer, int numSamples) noexcept;

    /**
     * @brief Process audio block with complete gain chain
     * 
     * Main RT processing method applying:
     * - Envelope gains (calculated via calculateBlockGains)
     * - Velocity scaling
     * - Constant power panning
     * - Master gain
     * - Mixdown to shared output buffers (additive)
     * 
     * @param outputLeft Left channel output buffer (additive mixing)
     * @param outputRight Right channel output buffer (additive mixing)
     * @param samplesPerBlock Number of samples to process
     * @return true if voice remains active
     * @note RT-safe: no allocations, pre-calculated gains
     */
    bool processBlock(float* outputLeft, float* outputRight, int samplesPerBlock) noexcept;

    // ===== GAIN CONTROL =====

    /**
     * @brief Set pan position
     * @param pan Pan value (-1.0 = hard left, 0.0 = center, +1.0 = hard right)
     * @note RT-safe
     */
    void setPan(float pan) noexcept;

    /**
     * @brief Set master gain with validation and logging
     * @param gain Master gain (0.0-1.0)
     * @param logger Reference to Logger
     */
    void setMasterGain(float gain, Logger& logger);

    /**
     * @brief Set master gain without logging (RT-safe version)
     * @param gain Master gain (0.0-1.0)
     * @note RT-safe: no logging, silent validation
     */
    void setMasterGainRTSafe(float gain) noexcept;

    // ===== GETTERS =====

    uint8_t getMidiNote() const noexcept { return midiNote_; }
    bool isActive() const noexcept { return state_ != VoiceState::Idle; }
    VoiceState getState() const noexcept { return state_; }
    int getPosition() const noexcept { return position_; }
    uint8_t getCurrentVelocityLayer() const noexcept { return currentVelocityLayer_; }
    
    // Gain getters
    float getCurrentEnvelopeGain() const noexcept { return envelope_gain_; }
    float getVelocityGain() const noexcept { return velocity_gain_; }
    float getMasterGain() const noexcept { return master_gain_; }

    // ===== RT MODE CONTROL =====

    /**
     * @brief Set real-time mode for all Voice instances
     * @param enabled true = RT mode (minimal logging), false = normal mode
     */
    static void setRealTimeMode(bool enabled) noexcept { rtMode_.store(enabled); }
    
    /**
     * @brief Get current RT mode status
     * @return true if RT mode is enabled
     */
    static bool isRealTimeMode() noexcept { return rtMode_.load(); }

    // ===== DEBUG AND DIAGNOSTICS =====

    /**
     * @brief Get detailed gain information for debugging
     * @param logger Reference to Logger
     * @return Formatted string with gain details
     */
    std::string getGainDebugInfo(Logger& logger) const;

private:
    // ===== MEMBER VARIABLES =====
    
    // Core identification and configuration
    uint8_t             midiNote_;                  // MIDI note number (0-127)
    const Instrument*   instrument_;                // Pointer to instrument data (non-owning)
    int                 sampleRate_;                // Sample rate for envelope calculations
    Envelope*           envelope_;                  // Pointer to envelope processor (non-owning)
    
    // Voice state and position
    VoiceState          state_;                     // Current voice state
    int                 position_;                  // Current position in sample frames
    uint8_t             currentVelocityLayer_;      // Current velocity layer (0-7)
    
    // Gain controls
    float               master_gain_;               // Master volume control
    float               velocity_gain_;             // MIDI velocity gain (0.0-1.0)
    float               envelope_gain_;             // Current envelope gain (0.0-1.0)
    float               pan_;                       // Pan position (-1.0 to +1.0)
    
    // Envelope state tracking
    int                 envelope_attack_position_;  // Current position in attack phase
    int                 envelope_release_position_; // Current position in release phase
    float               release_start_gain_;        // Gain value when release started
    
    // Pre-allocated RT buffer (64KB reserve)
    mutable std::vector<float> gainBuffer_;
    
    // Shared RT mode flag
    static std::atomic<bool> rtMode_;

    // ===== PRIVATE HELPER METHODS =====
    
    /**
     * @brief Reset all voice state variables to defaults
     */
    void resetVoiceState() noexcept;
    
    /**
     * @brief Check if voice is ready for processing
     * @return true if instrument, sampleRate, and envelope are valid
     */
    bool isVoiceReady() const noexcept;
    
    /**
     * @brief Start note with specified velocity
     * @param velocity MIDI velocity (0-127)
     */
    void startNote(uint8_t velocity) noexcept;
    
    /**
     * @brief Stop current note (initiate release phase)
     */
    void stopNote() noexcept;
    
    /**
     * @brief Update velocity gain based on MIDI velocity
     * @param velocity MIDI velocity (0-127)
     * @note Uses logarithmic scaling for natural response
     */
    void updateVelocityGain(uint8_t velocity) noexcept;
    
    /**
     * @brief Process attack phase of envelope
     * @param gainBuffer Buffer for gain values
     * @param numSamples Number of samples to process
     * @return true if voice remains active
     */
    bool processAttackPhase(float* gainBuffer, int numSamples) noexcept;
    
    /**
     * @brief Process sustain phase of envelope
     * @param gainBuffer Buffer for gain values
     * @param numSamples Number of samples to process
     * @return true if voice remains active
     */
    bool processSustainPhase(float* gainBuffer, int numSamples) noexcept;
    
    /**
     * @brief Process release phase of envelope
     * @param gainBuffer Buffer for gain values
     * @param numSamples Number of samples to process
     * @return true if voice remains active
     */
    bool processReleasePhase(float* gainBuffer, int numSamples) noexcept;
    
    /**
     * @brief Apply audio processing with calculated gains
     * @param outputLeft Left channel output buffer
     * @param outputRight Right channel output buffer
     * @param stereoBuffer Source stereo sample data
     * @param samplesToProcess Number of samples to process
     */
    void processAudioWithGains(float* outputLeft, float* outputRight, 
                              const float* stereoBuffer, int samplesToProcess) noexcept;
    
    /**
     * @brief Calculate constant power panning gains
     * @param pan Pan position (-1.0 to +1.0)
     * @param leftGain Output left channel gain
     * @param rightGain Output right channel gain
     * @note Temporary implementation - will be moved to pan_utils.h
     */
    void calculatePanGains(float pan, float& leftGain, float& rightGain) noexcept;
    
    /**
     * @brief Safe logging wrapper for non-RT operations
     * @param component Component name
     * @param severity Log severity level
     * @param message Log message
     * @param logger Reference to Logger
     */
    void logSafe(const std::string& component, const std::string& severity, 
                const std::string& message, Logger& logger) const;
};

#endif // VOICE_H