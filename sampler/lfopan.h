#ifndef LFOPAN_H
#define LFOPAN_H

#include <cstdint>

/**
 * @class LfoPanning
 * @brief Handles LFO-based automatic panning with pre-calculated lookup tables
 * 
 * Provides RT-safe LFO panning calculations for electric piano effects.
 * Uses pre-computed sine wave tables for smooth panning motion between channels.
 * Supports MIDI-based speed and depth control with linear interpolation.
 */
class LfoPanning {
public:
    /**
     * @brief Initialize the LFO panning lookup tables
     * @note Called once during VoiceManager construction
     */
    static void initializeLfoTables();

    /**
     * @brief Convert MIDI speed value to LFO frequency in Hz
     * @param midi_speed MIDI value (0-127)
     * @return LFO frequency (0.0-2.0 Hz)
     * @note RT-safe: linear interpolation from pre-calculated table
     */
    static float getFrequencyFromMIDI(uint8_t midi_speed) noexcept;

    /**
     * @brief Convert MIDI depth value to amplitude multiplier
     * @param midi_depth MIDI value (0-127)
     * @return Amplitude multiplier (0.0-1.0)
     * @note RT-safe: linear interpolation from pre-calculated table
     */
    static float getDepthFromMIDI(uint8_t midi_depth) noexcept;

    /**
     * @brief Get sine wave value from pre-calculated lookup table
     * @param phase Phase value (0.0-2π)
     * @return Sine value (-1.0 to +1.0)
     * @note RT-safe: uses pre-calculated sine values with interpolation
     */
    static float getSineValue(float phase) noexcept;

    /**
     * @brief Calculate phase increment per sample for given frequency
     * @param frequency LFO frequency in Hz
     * @param sampleRate Sample rate in Hz
     * @return Phase increment per sample
     * @note RT-safe: direct calculation
     */
    static float calculatePhaseIncrement(float frequency, int sampleRate) noexcept;

    /**
     * @brief Wrap phase to valid range (0.0-2π)
     * @param phase Input phase value
     * @return Wrapped phase (0.0-2π)
     * @note RT-safe: modulo operation with 2π
     */
    static float wrapPhase(float phase) noexcept;

private:
    // Lookup table sizes
    static constexpr int MIDI_TABLE_SIZE = 128;
    static constexpr int SINE_TABLE_SIZE = 1024;
    static constexpr float TWO_PI = 6.2831f;
    
    // Pre-calculated lookup tables
    static float frequency_table[MIDI_TABLE_SIZE];
    static float depth_table[MIDI_TABLE_SIZE];
    static float sine_table[SINE_TABLE_SIZE];
    static bool tables_initialized;
};

#endif // LFOPAN_H