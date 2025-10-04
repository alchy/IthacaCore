/**
 * @file biquad_filter.h
 * @brief Professional biquad IIR filter implementation for audio DSP
 *
 * Implementation of industry-standard biquad (2-pole, 2-zero) IIR filter
 * using Direct Form I topology. Supports multiple filter types commonly
 * used in audio processing and BBE sound enhancement.
 *
 * Key Features:
 * - Block processing for better cache performance
 * - Denormal protection for numerical stability
 * - RT-safe processing with no allocations
 * - In-place processing support
 *
 * @author IthacaCore Audio Team
 * @version 1.0.0
 * @date 2025
 */

#ifndef BIQUAD_FILTER_H
#define BIQUAD_FILTER_H

#include <cmath>
#include <algorithm>

/**
 * @class BiquadFilter
 * @brief Biquad IIR filter with multiple filter types
 */
class BiquadFilter {
public:
    enum class Type {
        LOWPASS, HIGHPASS, BANDPASS, PEAKING, LOW_SHELF, HIGH_SHELF,
        ALLPASS_180, ALLPASS_360
    };

    BiquadFilter() = default;

    /**
     * @brief Configure filter coefficients based on design parameters
     * @note NOT RT-SAFE: Uses exp, sin, cos - call during initialization only
     */
    void setCoefficients(Type type, double sampleRate, double frequency,
                        double Q = 0.707, double gainDB = 0.0) noexcept;

    // Implementation note: setCoefficients is declared here but implemented
    // in biquad_filter.cpp to avoid header bloat

    inline float processSample(float input) noexcept {
        const float output = b0_ * input + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = input;
        y2_ = y1_; y1_ = output;
        if (std::abs(y1_) < 1e-20f) y1_ = 0.0f;
        if (std::abs(y2_) < 1e-20f) y2_ = 0.0f;
        return output;
    }

    /**
     * @brief Process block of audio samples through filter (optimized)
     *
     * Processes multiple samples in sequence for better cache performance.
     * More efficient than calling processSample() in a loop.
     *
     * Can process in-place (input == output).
     *
     * @param input Pointer to input samples
     * @param output Pointer to output samples (can be same as input for in-place)
     * @param samples Number of samples to process
     *
     * @note RT-SAFE: No allocations, no branches (except denormal checks)
     * @note Supports in-place processing: processBlock(buffer, buffer, count)
     */
    inline void processBlock(const float* input, float* output, int samples) noexcept {
        for (int i = 0; i < samples; ++i) {
            output[i] = processSample(input[i]);
        }
    }

    void reset() noexcept {
        x1_ = x2_ = y1_ = y2_ = 0.0f;
    }

private:
    float b0_{1.0f}, b1_{0.0f}, b2_{0.0f};
    float a1_{0.0f}, a2_{0.0f};
    float x1_{0.0f}, x2_{0.0f}, y1_{0.0f}, y2_{0.0f};
};

#endif // BIQUAD_FILTER_H