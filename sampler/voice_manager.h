#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include "voice.h"
#include "envelopes/envelope.h"
#include "envelopes/envelope_static_data.h"
#include "instrument_loader.h"
#include "sampler.h"
#include "lfopan.h"

#include <vector>
#include <string>
#include <atomic>
#include <memory>

/**
 * @class VoiceManager
 * @brief Polyphonic audio system manager with optimized resource usage and LFO panning
 * 
 * Core Features:
 * - 128 simultaneous voices (one per MIDI note)
 * - Dynamic sample rate management (44100/48000 Hz)
 * - Shared envelope data system for memory efficiency
 * - Constant power panning with pre-calculated lookup tables
 * - LFO automatic panning for electric piano effects
 * - RT-safe audio processing with pre-allocated buffers
 * - Swap-and-pop vector optimizations for O(1) performance
 * 
 * LFO Panning Features:
 * - Automatic sinusoidal panning motion (0-2 Hz frequency range)
 * - MIDI-controllable speed and depth (0-127 values)
 * - RT-safe operation with pre-calculated sine tables
 * - Global synchronization across all voices
 * - Independent from static pan control
 * 
 * Architecture:
 * - Stack-allocated components (SamplerIO, InstrumentLoader)
 * - Shared EnvelopeStaticData between instances (saves ~3.1GB for 16 channels)
 * - JUCE integration ready (prepareToPlay pattern)
 * - Modular initialization pipeline
 * 
 * Thread Safety:
 * - processBlock methods are fully RT-safe
 * - Initialization methods may log and allocate
 * - Atomic counters for voice statistics
 * 
 * Memory Usage:
 * - Fixed 128 voice pool: ~8MB for gain buffers (64KB each)
 * - Shared envelope lookup tables: ~2MB total (global)
 * - Pan lookup tables: <1KB per instance
 * - LFO lookup tables: ~16KB (global, shared)
 */
class VoiceManager {
public:
    // ===== CONSTRUCTOR =====
    
    /**
     * @brief Create VoiceManager with sample directory
     * @param sampleDir Path to sample directory
     * @param logger Reference to Logger
     * 
     * Prerequisites:
     * - EnvelopeStaticData must be initialized before construction
     * - Sample directory must exist and contain valid samples
     * 
     * Initializes:
     * - 128-voice pool with pre-allocated buffers
     * - Constant power panning lookup tables
     * - LFO panning lookup tables
     * - Error callbacks for envelope system
     */
    VoiceManager(const std::string& sampleDir, Logger& logger);
    
    // ===== CONSTANT POWER PANNING =====
    
    /**
     * @brief Get pan gains from pre-calculated lookup table
     * @param pan Pan position (-1.0 = left, 0.0 = center, +1.0 = right)
     * @param leftGain Output left channel gain
     * @param rightGain Output right channel gain
     * @note RT-safe: uses pre-calculated sin/cos values
     */
    static void getPanGains(float pan, float& leftGain, float& rightGain) noexcept;
    
    // ===== INITIALIZATION PIPELINE =====
    
    /**
     * @brief Phase 1: System initialization and directory scanning
     * @param logger Reference to Logger
     * @note Non-RT: may allocate and log
     */
    void initializeSystem(Logger& logger);

    /**
     * @brief Phase 2: Load sample data for specific sample rate
     * @param sampleRate Target sample rate (44100 or 48000 Hz)
     * @param logger Reference to Logger
     * @note Non-RT: may allocate and log
     */
    void loadForSampleRate(int sampleRate, Logger& logger);

    // ===== SAMPLE RATE MANAGEMENT =====
    
    /**
     * @brief Change sample rate and reinitialize system
     * @param newSampleRate New sample rate (44100 or 48000 Hz)
     * @param logger Reference to Logger
     */
    void changeSampleRate(int newSampleRate, Logger& logger);
    
    /**
     * @brief Get current sample rate
     * @return Current sample rate in Hz (0 if not initialized)
     */
    int getCurrentSampleRate() const noexcept { return currentSampleRate_; }

    // ===== JUCE INTEGRATION =====
    
    /**
     * @brief Prepare all voices for audio processing
     * @param maxBlockSize Maximum expected block size from DAW
     * @note RT-safe: validates pre-allocated buffers
     */
    void prepareToPlay(int maxBlockSize) noexcept;

    // ===== CORE AUDIO API =====

    /**
     * @brief Set MIDI note state with velocity control
     * @param midiNote MIDI note number (0-127)
     * @param isOn true for note-on, false for note-off
     * @param velocity MIDI velocity (0-127) affecting volume
     * @note RT-safe: no allocations, automatic voice pool management
     */
    void setNoteStateMIDI(uint8_t midiNote, bool isOn, uint8_t velocity) noexcept;

    /**
     * @brief Set MIDI note state with default velocity
     * @param midiNote MIDI note number (0-127)
     * @param isOn true for note-on, false for note-off
     * @note RT-safe: uses DEFAULT_VELOCITY for note-on
     */
    void setNoteStateMIDI(uint8_t midiNote, bool isOn) noexcept;

    // ===== AUDIO PROCESSING =====
    
    /**
     * @brief Process audio block in interleaved format
     * @param outputBuffer Interleaved stereo buffer (AudioData array)
     * @param samplesPerBlock Number of samples to process
     * @return true if any voices are active
     * @note RT-safe: pre-allocated temp buffers, additive mixing, includes LFO panning
     */
    bool processBlockInterleaved(AudioData* outputBuffer, int samplesPerBlock) noexcept;
    
    /**
     * @brief Process audio block in uninterleaved format (JUCE style)
     * @param outputLeft Left channel buffer (float array)
     * @param outputRight Right channel buffer (float array) 
     * @param samplesPerBlock Number of samples to process
     * @return true if any voices are active
     * @note RT-safe: direct mixing to separate channel buffers, includes LFO panning
     */
    bool processBlockUninterleaved(float* outputLeft, float* outputRight, int samplesPerBlock) noexcept;

    // ===== VOICE CONTROL =====
    
    /**
     * @brief Stop all active voices (send note-off to all)
     * @note RT-safe: initiates release phase for active voices
     */
    void stopAllVoices() noexcept;
    
    /**
     * @brief Reset all voices to idle state and LFO parameters
     * @param logger Reference to Logger
     * @note Non-RT: may log reset operation, resets LFO phase and parameters
     */
    void resetAllVoices(Logger& logger);

    // ===== GLOBAL VOICE PARAMETERS =====

    /**
     * @brief Set master gain for all voices via MIDI value
     * @param midi_gain Master gain as MIDI value (0-127)
     * @param logger Reference to Logger
     * @note Non-RT: may log parameter changes
     */
    void setAllVoicesMasterGainMIDI(uint8_t midi_gain, Logger& logger);

    /**
     * @brief Set static pan for all voices via MIDI value
     * @param midi_pan Pan as MIDI value (0-127, where 64 = center)
     * @note RT-safe: direct parameter update, overridden by LFO panning if active
     */
    void setAllVoicesPanMIDI(uint8_t midi_pan) noexcept;

    /**
     * @brief Set attack time for all voices via MIDI value
     * @param midi_attack Attack time as MIDI value (0-127)
     * @note RT-safe: direct parameter update
     */
    void setAllVoicesAttackMIDI(uint8_t midi_attack) noexcept;

    /**
     * @brief Set release time for all voices via MIDI value
     * @param midi_release Release time as MIDI value (0-127)
     * @note RT-safe: direct parameter update
     */
    void setAllVoicesReleaseMIDI(uint8_t midi_release) noexcept;

    /**
     * @brief Set sustain level for all voices via MIDI value
     * @param midi_sustain Sustain level as MIDI value (0-127)
     * @note RT-safe: direct parameter update
     */
    void setAllVoicesSustainLevelMIDI(uint8_t midi_sustain) noexcept;

    // ===== LFO PANNING CONTROL =====

    /**
     * @brief Set LFO panning speed for all voices via MIDI value
     * @param midi_speed LFO speed as MIDI value (0-127)
     *                   0 = panning disabled (0 Hz)
     *                   127 = fastest panning (2 Hz, 500ms cycle)
     * @note RT-safe: updates LFO frequency and phase increment
     */
    void setAllVoicesPanSpeedMIDI(uint8_t midi_speed) noexcept;

    /**
     * @brief Set LFO panning depth for all voices via MIDI value
     * @param midi_depth LFO depth as MIDI value (0-127)
     *                   0 = no panning effect (static position)
     *                   127 = full range panning (-1.0 to +1.0)
     * @note RT-safe: updates LFO amplitude scaling
     */
    void setAllVoicesPanDepthMIDI(uint8_t midi_depth) noexcept;

    /**
     * @brief Get current LFO panning speed
     * @return Current LFO frequency in Hz (0.0-2.0)
     */
    float getLfoPanSpeed() const noexcept { return panSpeed_; }

    /**
     * @brief Get current LFO panning depth
     * @return Current LFO depth (0.0-1.0)
     */
    float getLfoPanDepth() const noexcept { return panDepth_; }

    /**
     * @brief Check if LFO panning is active
     * @return true if both speed and depth are > 0
     */
    bool isLfoPanningActive() const noexcept { return panSpeed_ > 0.0f && panDepth_ > 0.0f; }

    /**
     * @brief Set stereo field amount for all voices via MIDI value
     * @param midi_stereo_field Stereo field as MIDI value (0-127)
     *                          0 = disabled/mono (no stereo widening)
     *                          127 = maximum stereo width (±20% for extreme notes)
     * @note RT-safe: pre-calculates gains for each voice based on MIDI note position
     */
    void setAllVoicesStereoFieldAmountMIDI(uint8_t midi_stereo_field) noexcept;

    // ===== VOICE ACCESS AND STATISTICS =====

    /**
     * @brief Get maximum number of voices
     * @return Always 128 (fixed pool size)
     */
    int getMaxVoices() const noexcept { return 128; }
    
    /**
     * @brief Get number of active voices
     * @return Number of voices in attacking, sustaining, or releasing state
     * @note Thread-safe: atomic counter
     */
    int getActiveVoicesCount() const noexcept { return activeVoicesCount_.load(); }
    
    /**
     * @brief Get number of voices in sustaining state
     * @return Count of sustaining voices
     * @note May iterate through active voice list
     */
    int getSustainingVoicesCount() const noexcept;
    
    /**
     * @brief Get number of voices in releasing state
     * @return Count of releasing voices
     * @note May iterate through active voice list
     */
    int getReleasingVoicesCount() const noexcept;

    /**
     * @brief Get voice by MIDI note number
     * @param midiNote MIDI note number (0-127)
     * @return Reference to Voice object
     * @note Direct array access - validate midiNote in debug builds
     */
    Voice& getVoiceMIDI(uint8_t midiNote) noexcept;
    
    /**
     * @brief Get const voice by MIDI note number
     * @param midiNote MIDI note number (0-127)
     * @return Const reference to Voice object
     * @note Direct array access - validate midiNote in debug builds
     */
    const Voice& getVoiceMIDI(uint8_t midiNote) const noexcept;

    // ===== RT MODE CONTROL =====
    
    /**
     * @brief Set real-time mode for debugging/profiling
     * @param enabled true = RT mode (minimal logging), false = normal mode
     * @note Affects both VoiceManager and all Voice instances
     */
    void setRealTimeMode(bool enabled) noexcept;
    
    /**
     * @brief Get current RT mode status
     * @return true if RT mode is enabled
     */
    bool isRealTimeMode() const noexcept { return rtMode_.load(); }

    // ===== SYSTEM DIAGNOSTICS =====
    
    /**
     * @brief Log comprehensive system statistics including LFO panning
     * @param logger Reference to Logger
     * @note Non-RT: comprehensive system analysis and logging
     */
    void logSystemStatistics(Logger& logger);

private:
    // ===== CORE COMPONENTS =====
    
    SamplerIO samplerIO_;               // Sample directory scanner and file manager
    InstrumentLoader instrumentLoader_; // Sample loading and instrument creation
    Envelope envelope_;                 // Per-instance envelope state wrapper
    
    // ===== SYSTEM STATE =====
    
    int currentSampleRate_;            // Current sample rate (0 = not initialized)
    std::string sampleDir_;            // Sample directory path
    bool systemInitialized_;           // Initialization completion flag
    
    // ===== VOICE MANAGEMENT =====
    
    std::vector<Voice> voices_;        // Fixed pool of 128 voices (one per MIDI note)
    std::vector<Voice*> activeVoices_; // Pointers to currently active voices
    std::vector<Voice*> voicesToRemove_; // Cleanup buffer for RT processing
    
    mutable std::atomic<int> activeVoicesCount_{0}; // Thread-safe active voice counter
    std::atomic<bool> rtMode_{false};               // RT mode flag

    // ===== LFO PANNING STATE =====
    
    float panSpeed_;                   // LFO frequency in Hz (0.0-2.0)
    float panDepth_;                   // LFO amplitude (0.0-1.0)
    float lfoPhase_;                   // Current LFO phase (0.0-2π)
    float lfoPhaseIncrement_;          // Phase increment per sample

    // ===== PRIVATE HELPER METHODS =====
    
    /**
     * @brief Initialize all voices with loaded instrument data
     * @param logger Reference to Logger
     */
    void initializeVoicesWithInstruments(Logger& logger);
    
    /**
     * @brief Check if sample rate reinitialization is needed
     * @param targetSampleRate Target sample rate
     * @return true if reinitialization required
     */
    bool needsReinitialization(int targetSampleRate) const noexcept;
    
    /**
     * @brief Reinitialize system if sample rate changed
     * @param targetSampleRate Target sample rate
     * @param logger Reference to Logger
     */
    void reinitializeIfNeeded(int targetSampleRate, Logger& logger);

    // ===== LFO PANNING HELPERS =====
    
    /**
     * @brief Update LFO phase for current audio block
     * @param samplesPerBlock Number of samples in current block
     * @note RT-safe: advances LFO phase and wraps to valid range
     */
    void updateLfoPhase(int samplesPerBlock) noexcept;
    
    /**
     * @brief Apply LFO panning to all active voices
     * @note RT-safe: calculates current pan position and applies to voices
     */
    void applyLfoPanning() noexcept;
    
    /**
     * @brief Reset LFO parameters to default values
     * @note Resets speed, depth, and phase to initial state
     */
    void resetLfoParameters() noexcept;

    // ===== VOICE POOL MANAGEMENT =====
    
    /**
     * @brief Add voice to active pool
     * @param voice Pointer to voice to add
     * @note RT-safe: updates atomic counter
     */
    void addActiveVoice(Voice* voice) noexcept;
    
    /**
     * @brief Remove voice from active pool using swap-and-pop optimization
     * @param voice Pointer to voice to remove
     * @note RT-safe: O(1) operation, updates atomic counter
     */
    void removeActiveVoice(Voice* voice) noexcept;
    
    /**
     * @brief Clean up voices marked for removal
     * @note RT-safe: processes voicesToRemove_ buffer
     */
    void cleanupInactiveVoices() noexcept;
    
    /**
     * @brief Safe logging wrapper for non-RT operations
     * @param component Component name
     * @param severity Log severity level
     * @param message Log message
     * @param logger Reference to Logger
     */
    void logSafe(const std::string& component, const std::string& severity, 
                const std::string& message, Logger& logger) const;
    
    /**
     * @brief Validate MIDI note range
     * @param midiNote MIDI note to validate
     * @return true if note is in valid range (0-127)
     */
    bool isValidMidiNote(uint8_t midiNote) const noexcept {
        return midiNote <= 127;
    }
};

#endif // VOICE_MANAGER_H