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

// ===== DAMPING RELEASE CONFIGURATION =====
// Duration of damping release envelope for retrigger click elimination
#ifndef DAMPING_RELEASE_MS
#define DAMPING_RELEASE_MS 3.0f  // Milliseconds
#endif

// ===== VELOCITY LAYER MODULATION CONFIGURATION =====
// Fine-tune gain adjustment within velocity layers for smooth dynamic response
#ifndef VELOCITY_LAYER_MODULATION
#define VELOCITY_LAYER_MODULATION 0.08f  // ±8% gain adjustment within layer
#endif

#ifndef VELOCITY_LAYER_SIZE
#define VELOCITY_LAYER_SIZE 16.0f  // MIDI range per layer (128/8 layers)
#endif

#ifndef VELOCITY_LAYER_HALF_SIZE
#define VELOCITY_LAYER_HALF_SIZE 8.0f  // Half size for symmetric modulation
#endif

#ifndef VELOCITY_LAYER_CENTER_OFFSET
#define VELOCITY_LAYER_CENTER_OFFSET 7.5f  // Exact center offset within 16-value layer
#endif

// ===== STEREO FIELD CONFIGURATION =====
// Simulates physical string position on piano in stereo image
#ifndef STEREO_FIELD_MAX_OFFSET
#define STEREO_FIELD_MAX_OFFSET 0.20f  // ±20% gain offset for extreme notes
#endif

#ifndef MIDI_MIDDLE_C
#define MIDI_MIDDLE_C 60
#endif

#ifndef MIDI_LOWEST_NOTE
#define MIDI_LOWEST_NOTE 21  // A0
#endif

#ifndef MIDI_HIGHEST_NOTE
#define MIDI_HIGHEST_NOTE 108  // C8
#endif

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
 * - Retrigger damping for click-free note retriggering
 * - Comprehensive error handling and logging
 * 
 * Retrigger Damping:
 * When a note is retriggered while already playing, a damping buffer captures
 * the current audio state and plays it out with a short linear fade to prevent
 * clicks. The new note starts immediately while the damping buffer finishes.
 * 
 * Implementation Details:
 * - Damping buffer is pre-computed during retrigger detection in startNote()
 * - Contains final audio samples with all gain parameters already applied
 * - Linear fade-out from 1.0 to 0.0 over DAMPING_RELEASE_MS duration
 * - Requires no runtime gain calculations, only direct mixing
 * - Duration configurable via DAMPING_RELEASE_MS macro
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
     * @brief Default constructor with pre-allocated buffers
     * 
     * Pre-allocates:
     * - 64KB gain buffer for envelope processing
     * - ~1024 samples for damping buffers (sized properly in initialize())
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
     * 
     * Calculates damping buffer length based on sample rate and allocates
     * all necessary buffers for RT-safe operation.
     * 
     * Damping buffer size = (DAMPING_RELEASE_MS / 1000.0f) * sampleRate
     * 
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
     * 
     * Resets all state including damping buffer to inactive.
     * 
     * @param logger Reference to Logger
     */
    void cleanup(Logger& logger);

    /**
     * @brief Reinitialize voice with new configuration
     * 
     * Recalculates damping buffer length if sample rate changed.
     * Reallocates buffers as necessary.
     * 
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
     * 
     * Automatically detects and handles retriggers:
     * - If voice is idle: starts note normally
     * - If voice is active (attacking/sustaining/releasing): captures damping
     *   buffer from current position, then starts new note
     * 
     * Retrigger Process:
     * 1. Detect state != Idle in startNote()
     * 2. Capture current audio + apply linear fade-out to damping buffer
     * 3. Reset voice state and start new note from beginning
     * 4. During processBlock(), mix damping buffer with new note
     * 
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
     * @brief Process audio block with complete gain chain and damping
     * 
     * Main RT processing method with two-phase processing:
     * 
     * PHASE 1 - Damping Buffer (if retrigger active):
     * - Mix pre-computed damping samples directly to output
     * - No gain calculations needed (already in buffer)
     * - Runs concurrently with Phase 2
     * 
     * PHASE 2 - Main Voice:
     * - Calculate envelope gains via calculateBlockGains()
     * - Apply velocity scaling
     * - Apply constant power panning
     * - Apply master gain
     * - Mix to output buffers (additive)
     * 
     * This two-phase approach ensures smooth, click-free retriggering while
     * maintaining full control over both the damping tail and new note.
     * 
     * @param outputLeft Left channel output buffer (additive mixing)
     * @param outputRight Right channel output buffer (additive mixing)
     * @param samplesPerBlock Number of samples to process
     * @param panBuffer Optional per-sample pan buffer for LFO panning (nullptr = use static pan_)
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
     * @brief Set stereo field amount for spatial positioning
     * 
     * Simulates physical piano string position in stereo image.
     * Higher notes pan more right, lower notes pan more left, relative to Middle C (MIDI 60).
     * Stereo field gains are calculated immediately and cached for RT performance.
     * 
     * @param midiValue MIDI value 0-127 (0 = disabled/mono, 127 = maximum stereo width ±20%)
     * @note RT-safe, pre-calculates gains
     */
    void setStereoFieldAmountMIDI(uint8_t midiValue) noexcept;

    /**
     * @brief Set master gain with validation
     * @param gain Master gain (0.0-1.0)
     * @param logger Reference to Logger
     */
    void setMasterGain(float gain) noexcept;;


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
    
    // Stereo field getters
    uint8_t getStereoFieldAmountMIDI() const noexcept { return stereoFieldAmount_; }
    float getStereoFieldGainLeft() const noexcept { return stereoFieldGainLeft_; }
    float getStereoFieldGainRight() const noexcept { return stereoFieldGainRight_; }
    
    // Damping state getters (for diagnostics)
    bool isDampingActive() const noexcept { return dampingActive_; }
    int getDampingPosition() const noexcept { return dampingPosition_; }
    int getDampingLength() const noexcept { return dampingLength_; }

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
     * 
     * Includes damping buffer status if active.
     * 
     * @param logger Reference to Logger
     * @return Formatted string with gain details
     */
    std::string getGainDebugInfo(Logger& logger) const;

private:
    // ===== MEMBER VARIABLES =====
    
    // --- Core identification and configuration ---
    uint8_t             midiNote_;                  // MIDI note number (0-127)
    const Instrument*   instrument_;                // Pointer to instrument data (non-owning)
    int                 sampleRate_;                // Sample rate for envelope calculations
    Envelope*           envelope_;                  // Pointer to envelope processor (non-owning)
    
    // --- Voice state and position ---
    VoiceState          state_;                     // Current voice state
    int                 position_;                  // Current position in sample frames
    uint8_t             currentVelocityLayer_;      // Current velocity layer (0-7)
    
    // --- Gain controls ---
    float               master_gain_;               // Master volume control
    float               velocity_gain_;             // MIDI velocity gain (0.0-1.0)
    float               envelope_gain_;             // Current envelope gain (0.0-1.0)
    float               pan_;                       // Pan position (-1.0 to +1.0)
    
    // --- Stereo field simulation ---
    float               stereoFieldGainLeft_;       // Left channel stereo field modifier (0.8-1.2)
    float               stereoFieldGainRight_;      // Right channel stereo field modifier (0.8-1.2)
    uint8_t             stereoFieldAmount_;         // MIDI 0-127, intensity of stereo effect
    
    // --- Envelope state tracking ---
    int                 envelope_attack_position_;  // Current position in attack phase
    int                 envelope_release_position_; // Current position in release phase
    float               release_start_gain_;        // Gain value when release started
    
    // --- Pre-allocated RT buffers ---
    mutable std::vector<float> gainBuffer_;         // Envelope gain buffer (64KB reserve)
    
    // --- Damping release buffers (retrigger click elimination) ---
    std::vector<float>  dampingBufferLeft_;         // Pre-computed damping samples (left channel)
    std::vector<float>  dampingBufferRight_;        // Pre-computed damping samples (right channel)
    int                 dampingLength_;             // Total damping buffer length in samples
    int                 dampingPosition_;           // Current playback position in damping buffer
    bool                dampingActive_;             // Flag indicating damping playback is active
    
    // --- Shared RT mode flag ---
    static std::atomic<bool> rtMode_;

    // ===== PRIVATE HELPER METHODS =====
    
    /**
     * @brief Reset all voice state variables to defaults
     * 
     * Includes resetting damping state to inactive.
     */
    void resetVoiceState() noexcept;
    
    /**
     * @brief Check if voice is ready for processing
     * @return true if instrument, sampleRate, and envelope are valid
     */
    bool isVoiceReady() const noexcept;
    
    /**
     * @brief Start note with specified velocity
     * 
     * Automatically detects retrigger (state != Idle) and captures damping
     * buffer before starting new note. This provides click-free retriggering.
     * 
     * Retrigger Detection:
     * - If state == Idle: normal note start
     * - If state != Idle: call captureDampingBuffer() then start note
     * 
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
     * @brief Calculate stereo field gain modifiers based on note position
     * 
     * Called when stereo field amount changes via setStereoFieldAmountMIDI().
     * Calculates and caches stereo field gains based on MIDI note distance from Middle C.
     * Updates stereoFieldGainLeft_ and stereoFieldGainRight_.
     * 
     * Distance calculation:
     * - Notes below Middle C (21-59): progressively boost left, reduce right
     * - Middle C (60): no stereo offset (1.0, 1.0)
     * - Notes above Middle C (61-108): progressively boost right, reduce left
     * 
     * @note Private method, called automatically by setStereoFieldAmountMIDI()
     */
    void calculateStereoFieldGains() noexcept;
    
    /**
     * @brief Capture current audio into damping buffer for retrigger
     * 
     * Called automatically when retrigger is detected in startNote().
     * 
     * Process:
     * 1. Get current playback position and sample data
     * 2. Calculate current gain state (envelope * velocity * master * pan)
     * 3. For each sample in damping buffer:
     *    - Apply linear fade: gain = baseGain * (1.0 - i/length)
     *    - Pre-compute final audio: sample * totalGain
     * 4. Store in dampingBufferLeft/Right
     * 5. Set dampingActive = true, dampingPosition = 0
     * 
     * The resulting buffer contains final audio ready for direct mixing,
     * requiring no further gain calculations during playback.
     * 
     * Buffer contents:
     * - Sample 0: Full gain (baseGain * 1.0)
     * - Sample N/2: Half gain (baseGain * 0.5)
     * - Sample N: Zero gain (baseGain * 0.0)
     * 
     * @note RT-safe: only reads from pre-allocated buffers
     */
    void captureDampingBuffer() noexcept;
    
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
     * @brief Zpracuje audio s vypočtenými gainy
     * @param outputLeft Výstupní buffer levého kanálu
     * @param outputRight Výstupní buffer pravého kanálu
     * @param stereoBuffer Zdrojová stereo data vzorků
     * @param samplesToProcess Počet vzorků ke zpracování
     * @note Používá pouze statický panning (pan_) bez LFO modulace
     */
    void processAudioWithGains(float* outputLeft, float* outputRight,
                              const float* stereoBuffer, int samplesToProcess) noexcept;
    
    /**
     * @brief Vypočítá gainy pro konstantní panning
     * 
     * Používá sinusové křivky pro udržení konstantní vnímané hlasitosti.
     * Celkový výkon (L² + R²) zůstává konstantní přes stereo pole.
     * 
     * @param pan Pozice panoramy (-1.0 až +1.0)
     * @param leftGain Výstupní gain levého kanálu
     * @param rightGain Výstupní gain pravého kanálu
     * @note Dočasná implementace - bude přesunuta do pan_utils.h
     */
    void calculatePanGains(float pan, float& leftGain, float& rightGain) noexcept;

};

#endif // VOICE_H