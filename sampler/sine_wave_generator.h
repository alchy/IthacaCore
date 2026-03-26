/**
 * @file sine_wave_generator.h
 * @brief Sine wave generator for initial plugin state (no sample bank loaded)
 *
 * Generates stereo sine waves with slight phase offset for width effect.
 * Compatible with InstrumentLoader data structures (stereo interleaved float).
 */

#ifndef SINE_WAVE_GENERATOR_H
#define SINE_WAVE_GENERATOR_H

#include <vector>
#include <cstdint>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @class SineWaveGenerator
 * @brief Generates sine wave samples for MIDI notes with velocity layers
 *
 * Purpose: Provide fallback audio when no sample bank is loaded.
 * Output format: Stereo interleaved float [L1,R1,L2,R2,...] matching InstrumentLoader.
 */
class SineWaveGenerator {
public:
    /**
     * @brief Generate stereo sine wave for given MIDI note and velocity layer
     * @param midiNote MIDI note number (0-127)
     * @param velocityLayer Velocity layer (1-8), affects amplitude
     * @param totalVelocityLayers Total velocity layers (1-8)
     * @param sampleRate Sample rate (44100 or 48000)
     * @param durationSeconds Duration in seconds (default 2.0)
     * @return Vector of stereo interleaved float samples [L1,R1,L2,R2,...]
     *
     * Features:
     * - MIDI note → frequency conversion (A4 = 440 Hz)
     * - Linear velocity layer → amplitude mapping
     * - Stereo with slight phase offset (0.05 radians) for width
     * - No envelope applied — Voice ADSR handles attack/release
     */
    static std::vector<float> generateStereoSine(
        uint8_t midiNote,
        int velocityLayer,
        int totalVelocityLayers,
        int sampleRate,
        float durationSeconds = 2.0f
    );

    /**
     * @brief Convert MIDI note to frequency in Hz
     * @param midiNote MIDI note number (0-127)
     * @return Frequency in Hz (A4 = 440 Hz)
     *
     * Formula: f = 440 * 2^((midiNote - 69) / 12)
     */
    static float midiNoteToFrequency(uint8_t midiNote);

    /**
     * @brief Calculate amplitude for velocity layer
     * @param layer Current velocity layer (1-8)
     * @param totalLayers Total velocity layers (1-8)
     * @return Amplitude multiplier (0.0 to 1.0)
     *
     * Linear mapping: layer 1 = 1/totalLayers, layer N = N/totalLayers
     * Example (8 layers): [0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0]
     */
    static float velocityLayerToAmplitude(int layer, int totalLayers);

private:
    // Stereo phase offset in radians (small offset for width effect)
    static constexpr float STEREO_PHASE_OFFSET = 0.05f;
};

#endif // SINE_WAVE_GENERATOR_H
