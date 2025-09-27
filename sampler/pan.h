#ifndef PAN_H
#define PAN_H

#include <cstdint>

/**
 * @class Panning
 * @brief Handles constant power panning with pre-calculated lookup table
 * 
 * Provides RT-safe panning calculations for stereo audio using a pre-computed table.
 * The table maps MIDI pan values (0-127) to left and right channel gains.
 */
class Panning {
public:
    /**
     * @brief Initialize the panning lookup table
     * @note Called once during VoiceManager construction
     */
    static void initializePanTables();

    /**
     * @brief Get panning gains from pre-calculated lookup table
     * @param pan Pan position (-1.0 = left, 0.0 = center, +1.0 = right)
     * @param leftGain Output left channel gain
     * @param rightGain Output right channel gain
     * @note RT-safe: uses pre-calculated sin/cos values
     */
    static void getPanGains(float pan, float& leftGain, float& rightGain) noexcept;

private:
    static constexpr int PAN_TABLE_SIZE = 128;
    static float pan_left_gains[PAN_TABLE_SIZE];
    static float pan_right_gains[PAN_TABLE_SIZE];
    static bool pan_tables_initialized;
};

#endif // PAN_H