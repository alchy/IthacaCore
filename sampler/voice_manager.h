#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include "voice.h"
#include "envelopes/envelope.h"
#include "envelopes/envelope_static_data.h"
#include "instrument_loader.h"
#include "sampler.h"
#include "lfopan.h"
#include "dsp/dsp_chain.h"
#include "dsp/limiter/limiter.h"

#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <array>

/**
 * @class VoiceManager
 * @brief Správce polyfonního audio systému s optimalizovaným využitím zdrojů a LFO panningem
 * 
 * Hlavní vlastnosti:
 * - 128 simultánních hlasů (jeden na MIDI notu)
 * - Dynamická správa vzorkovací frekvence (44100/48000 Hz)
 * - Sdílený systém obálek pro efektivní využití paměti
 * - Konstantní panning s předpočítanými tabulkami
 * - Automatický LFO panning pro efekty elektrického piana
 * - RT-safe zpracování audia s předem alokovanými buffery
 * - Swap-and-pop optimalizace pro O(1) výkon
 * - Podpora sustain pedálu (MIDI CC64) s odloženým note-off
 */
class VoiceManager {
public:
    // ===== CONSTRUCTOR =====
    
    /**
     * @brief Create VoiceManager with sample directory and velocity layer count
     * @param sampleDir Path to sample directory
     * @param logger Reference to Logger
     * @param velocityLayerCount Number of velocity layers (1-8), default 8
     *
     * Prerequisites:
     * - EnvelopeStaticData must be initialized before construction
     * - Sample directory must exist and contain valid samples
     *
     * Initializes:
     * - 128-voice pool with pre-allocated buffers
     * - Constant power panning lookup tables
     * - LFO panning lookup tables
     * - Sustain pedal state arrays
     * - Error callbacks for envelope system
     * - Dynamic velocity layer configuration
     */
    VoiceManager(const std::string& sampleDir, Logger& logger, int velocityLayerCount = 8);
    
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
     * @note Sustain pedal: if pedal is pressed, note-off events are delayed
     */
    void setNoteStateMIDI(uint8_t midiNote, bool isOn, uint8_t velocity) noexcept;

    /**
     * @brief Set MIDI note state with default velocity
     * @param midiNote MIDI note number (0-127)
     * @param isOn true for note-on, false for note-off
     * @note RT-safe: uses DEFAULT_VELOCITY for note-on
     * @note Sustain pedal: if pedal is pressed, note-off events are delayed
     */
    void setNoteStateMIDI(uint8_t midiNote, bool isOn) noexcept;

    // ===== SUSTAIN PEDAL API =====
    
    /**
     * @brief Set sustain pedal state (MIDI CC64)
     * @param pedalDown true = pedal pressed (MIDI value 64-127), 
     *                  false = pedal released (MIDI value 0-63)
     * 
     * Behavior:
     * - Pedal DOWN: All subsequent note-off events are delayed (stored but not executed)
     * - Pedal UP: All delayed note-off events are immediately sent to their respective voices
     * 
     * Memory footprint: 129 bytes (1 atomic bool + 128 bool array)
     * Complexity: O(1) for pedal down, O(n) for pedal up where n = number of delayed notes
     * 
     * @note RT-safe: no allocations, only flag operations and voice method calls
     */
    void setSustainPedalMIDI(bool pedalDown) noexcept;
    
    /**
     * @brief Get current sustain pedal state
     * @return true if pedal is currently pressed
     * @note RT-safe: atomic read operation
     */
    bool getSustainPedalActive() const noexcept { 
        return sustainPedalActive_.load(); 
    }

    // ===== AUDIO PROCESSING =====
    
    /**
     * @brief Zpracuje audio blok v neprokládaném formátu
     * @param outputLeft Výstupní buffer levého kanálu
     * @param outputRight Výstupní buffer pravého kanálu
     * @param samplesPerBlock Počet vzorků v bloku
     * @return true pokud je nějaký hlas aktivní
     * @note RT-safe
     */
    bool processBlockUninterleaved(float* outputLeft, float* outputRight, int samplesPerBlock) noexcept;

    /**
     * @brief Zpracuje audio blok v prokládaném formátu
     * @param outputBuffer Výstupní buffer (prokládaný stereo)
     * @param samplesPerBlock Počet vzorků v bloku
     * @return true pokud je nějaký hlas aktivní
     * @note RT-safe
     */
    bool processBlockInterleaved(AudioData* outputBuffer, int samplesPerBlock) noexcept;
    
    /**
     * @brief Aplikuje LFO panning na finální mix
     * @param leftOut Výstupní buffer levého kanálu
     * @param rightOut Výstupní buffer pravého kanálu
     * @param numSamples Počet vzorků ke zpracování
     * @note RT-safe: aplikuje LFO panning s vyhlazováním gainu
     */
    void applyLfoPanToFinalMix(float* leftOut, float* rightOut, int numSamples) noexcept;

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
     * @param midi_pan Pan as MIDI value (0-127, 64=center)
     * @note RT-safe: converts MIDI to -1.0/+1.0 range
     */
    void setAllVoicesPanMIDI(uint8_t midi_pan) noexcept;

    /**
     * @brief Set attack time for all voices via MIDI value
     * @param midi_attack Attack as MIDI value (0-127)
     * @note RT-safe: delegates to envelope system
     */
    void setAllVoicesAttackMIDI(uint8_t midi_attack) noexcept;

    /**
     * @brief Set release time for all voices via MIDI value
     * @param midi_release Release as MIDI value (0-127)
     * @note RT-safe: delegates to envelope system
     */
    void setAllVoicesReleaseMIDI(uint8_t midi_release) noexcept;

    /**
     * @brief Set sustain level for all voices via MIDI value
     * @param midi_sustain Sustain level as MIDI value (0-127)
     * @note RT-safe: delegates to envelope system
     */
    void setAllVoicesSustainLevelMIDI(uint8_t midi_sustain) noexcept;

    /**
     * @brief Set stereo field amount for all voices via MIDI value
     * @param midi_stereo Stereo field as MIDI value (0-127)
     * @note RT-safe: calculates physical piano string position simulation
     */
    void setAllVoicesStereoFieldAmountMIDI(uint8_t midi_stereo) noexcept;

    // ===== LFO PANNING CONTROL =====

    /**
     * @brief Set LFO panning speed for all voices
     * @param midi_speed Speed as MIDI value (0-127, maps to 0-2 Hz)
     * @note RT-safe: updates LFO phase increment
     */
    void setAllVoicesPanSpeedMIDI(uint8_t midi_speed) noexcept;

    /**
     * @brief Set LFO panning depth for all voices
     * @param midi_depth Depth as MIDI value (0-127, maps to 0-1.0)
     * @note RT-safe: updates LFO amplitude modulation
     */
    void setAllVoicesPanDepthMIDI(uint8_t midi_depth) noexcept;

    /**
     * @brief Check if LFO panning is currently active
     * @return true if LFO is modulating pan position
     * @note RT-safe: checks if speed and depth are non-zero
     */
    bool isLfoPanningActive() const noexcept;

    // ===== VOICE ACCESS =====

    /**
     * @brief Get reference to specific voice by MIDI note
     * @param midiNote MIDI note number (0-127)
     * @return Reference to voice
     * @note Non-RT: for parameter setup and inspection
     */
    Voice& getVoiceMIDI(uint8_t midiNote) noexcept;

    /**
     * @brief Get configured number of velocity layers
     * @return Number of velocity layers (1-8)
     */
    int getVelocityLayerCount() const noexcept { return velocityLayerCount_; }

    // ===== STATISTICS =====
    
    /**
     * @brief Get count of active voices (any non-idle state)
     * @return Number of voices in Attacking, Sustaining, or Releasing state
     */
    int getActiveVoicesCount() const noexcept { return activeVoicesCount_.load(); }
    
    /**
     * @brief Get count of sustaining voices
     * @return Number of voices in Sustaining state
     */
    int getSustainingVoicesCount() const noexcept;
    
    /**
     * @brief Get count of releasing voices
     * @return Number of voices in Releasing state
     */
    int getReleasingVoicesCount() const noexcept;

    // ===== REAL-TIME MODE =====
    
    /**
     * @brief Set real-time processing mode
     * @param enabled true to enable RT mode (disables logging)
     * @note Affects logging behavior in RT-critical paths
     */
    void setRealTimeMode(bool enabled) noexcept;
    
    /**
     * @brief Get current RT mode status
     * @return true if RT mode is enabled
     */
    bool isRealTimeMode() const noexcept { return rtMode_.load(); }

    // ===== SYSTEM DIAGNOSTICS =====

    /**
     * @brief Log comprehensive system statistics including LFO panning and sustain pedal
     * @param logger Reference to Logger
     * @note Non-RT: comprehensive system analysis and logging
     */
    void logSystemStatistics(Logger& logger);

    // ========================================================================
    // DSP EFFECTS API - MIDI Interface (0-127) - RT-safe
    // ========================================================================

    /**
     * @brief Nastaví limiter threshold pomocí MIDI CC (0-127)
     * @param midiValue 0 = -20 dB (hard limiting), 127 = 0 dB (transparent/off)
     *
     * @note RT-safe - lze volat z audio threadu
     * @note Mapování: 0-127 → -20 až 0 dB (lineární)
     */
    void setLimiterThresholdMIDI(uint8_t midiValue) noexcept;

    /**
     * @brief Nastaví limiter release pomocí MIDI CC (0-127)
     * @param midiValue 0 = 1 ms (fast), 127 = 1000 ms (slow)
     *
     * @note RT-safe - lze volat z audio threadu
     * @note Mapování: exponenciální pro plynulou kontrolu
     */
    void setLimiterReleaseMIDI(uint8_t midiValue) noexcept;

    /**
     * @brief Zapne/vypne limiter (0 = off, 1-127 = on)
     * @param midiValue 0 = disabled, jinak enabled
     *
     * @note RT-safe - lze volat z audio threadu
     */
    void setLimiterEnabledMIDI(uint8_t midiValue) noexcept;

    /**
     * @brief Získá limiter threshold jako MIDI hodnotu
     * @return MIDI hodnota (0-127)
     *
     * @note RT-safe
     */
    uint8_t getLimiterThresholdMIDI() const noexcept;

    /**
     * @brief Získá limiter release jako MIDI hodnotu
     * @return MIDI hodnota (0-127)
     *
     * @note RT-safe
     */
    uint8_t getLimiterReleaseMIDI() const noexcept;

    /**
     * @brief Získá stav limiteru (0 = off, 127 = on)
     * @return MIDI hodnota (0 nebo 127)
     *
     * @note RT-safe
     */
    uint8_t getLimiterEnabledMIDI() const noexcept;

    /**
     * @brief Získá aktuální gain reduction pro metering (0-127)
     * @return 127 = no reduction, 0 = maximum reduction
     *
     * @note RT-safe
     * @note Použití v GUI pro vizualizaci limitingu
     */
    uint8_t getLimiterGainReductionMIDI() const noexcept;

    // ========================================================================
    // DSP EFFECTS API - Direct Access (pokročilé použití)
    // ========================================================================

    /**
     * @brief Získá ukazatel na DspChain pro pokročilý přístup k efektům
     * @return Pointer na DspChain (nikdy nullptr)
     *
     * @note Pro pokročilé použití - přímý přístup k DSP chain
     * @note Umožňuje volat settery/gettery přímo na efektech
     */
    DspChain* getDspChain() { return &dspChain_; }

private:
    // ===== CORE COMPONENTS =====
    
    SamplerIO samplerIO_;               // Sample directory scanner and file manager
    InstrumentLoader instrumentLoader_; // Sample loading and instrument creation
    Envelope envelope_;                 // Per-instance envelope state wrapper
    
    // ===== SYSTEM STATE =====

    int currentSampleRate_;            // Current sample rate (0 = not initialized)
    std::string sampleDir_;            // Sample directory path
    bool systemInitialized_;           // Initialization completion flag
    int velocityLayerCount_;           // Number of velocity layers (1-8), default 8
    
    // ===== VOICE MANAGEMENT =====
    
    std::vector<Voice> voices_;        // Fixed pool of 128 voices (one per MIDI note)
    std::vector<Voice*> activeVoices_; // Pointers to currently active voices
    std::vector<Voice*> voicesToRemove_; // Cleanup buffer for RT processing
    
    mutable std::atomic<int> activeVoicesCount_{0}; // Thread-safe active voice counter
    std::atomic<bool> rtMode_{false};               // RT mode flag

    // ===== SUSTAIN PEDAL STATE =====
    
    /**
     * @brief Global sustain pedal state
     * 
     * When true: all note-off events are delayed (stored in delayedNoteOffs_)
     * When false: note-off events are processed immediately
     */
    std::atomic<bool> sustainPedalActive_{false};
    
    /**
     * @brief Per-note delayed note-off flags
     * 
     * Array indexed by MIDI note number (0-127)
     * true = this note received note-off while pedal was down
     * false = no delayed note-off pending
     * 
     * Memory: 128 bytes (std::array uses stack allocation)
     */
    std::array<bool, 128> delayedNoteOffs_{};

    // ===== LFO PANNING STATE =====

    float panSpeed_;                   // Current interpolated LFO frequency in Hz (0.0-2.0)
    float panSpeedTarget_;             // Target LFO frequency from MIDI (0.0-2.0)
    float panDepth_;                   // Current interpolated LFO depth (0.0-1.0)
    float panDepthTarget_;             // Target LFO depth from MIDI (0.0-1.0)
    float panSmoothingTime_;           // Smoothing time for both speed and depth (default: 0.5s)
    float lfoPhase_;                   // Current LFO phase (0.0-2π)
    std::vector<float> lfoPanBuffer_;  // Pre-calculated per-sample pan values

    // Členy pro vyhlazování LFO panningu
    float previousPanLeft_ = 1.0f;      // Předchozí gain levého kanálu
    float previousPanRight_ = 1.0f;     // Předchozí gain pravého kanálu
    static constexpr float LFO_SMOOTHING = 0.995f; // Konstanta pro exponenciální vyhlazování

    // ===== DSP EFFECTS CHAIN =====

    DspChain dspChain_;                // DSP effects chain (serial processing)
    Limiter* limiterEffect_;           // Quick pointer k limiteru (convenience)

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

    // ===== SUSTAIN PEDAL HELPERS =====
    
    /**
     * @brief Process all delayed note-offs when pedal is released
     * 
     * Called internally by setSustainPedalMIDI when pedal transitions from ON to OFF.
     * Iterates through all 128 MIDI notes and sends note-off to voices with delayed flags.
     * 
     * Complexity: O(128) but only executed on pedal release events
     * @note RT-safe: only flag checks and voice method calls
     */
    void processDelayedNoteOffs() noexcept;
    
    /**
     * @brief Clear delayed note-off flag for specific MIDI note
     * @param midiNote MIDI note number (0-127)
     * @note RT-safe: simple array write operation
     */
    void clearDelayedNoteOff(uint8_t midiNote) noexcept {
        if (midiNote <= 127) {
            delayedNoteOffs_[midiNote] = false;
        }
    }
    
    /**
     * @brief Check if note has pending delayed note-off
     * @param midiNote MIDI note number (0-127)
     * @return true if note-off is delayed for this note
     * @note RT-safe: simple array read operation
     */
    bool hasDelayedNoteOff(uint8_t midiNote) const noexcept {
        return (midiNote <= 127) && delayedNoteOffs_[midiNote];
    }

    // ===== LFO PANNING HELPERS =====

    /**
     * @brief Apply LFO panning per-sample to eliminate zipper noise
     * @param samplesPerBlock Number of samples in current block
     * @note RT-safe: updates LFO phase and pan for each sample independently
     * @note Per-sample processing ensures smooth panning at high depth/speed values
     */
    void applyLfoPanningPerSample(int samplesPerBlock) noexcept;

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
     * @brief Validate MIDI note range
     * @param midiNote MIDI note to validate
     * @return true if note is in valid range (0-127)
     */
    bool isValidMidiNote(uint8_t midiNote) const noexcept {
        return midiNote <= 127;
    }
};

#endif // VOICE_MANAGER_H