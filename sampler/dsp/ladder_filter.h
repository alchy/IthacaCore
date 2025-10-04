/**
 * @file ladder_filter.h
 * @brief Moog-style 4-pole ladder filter implementation
 *
 * Accurate simulation of the classic Moog ladder filter topology with:
 * - 4 cascaded one-pole lowpass filters
 * - Global feedback loop for resonance
 * - Per-stage tanh saturation for analog warmth
 * - Highpass mode via subtraction
 *
 * Key Features:
 * - True Moog ladder topology (not bilinear approximation)
 * - One-pole coefficient calculation (analog-matched)
 * - RT-safe processing (no allocations, noexcept)
 * - Stereo processing support
 *
 * @author IthacaCore Audio Team
 * @version 1.0.0
 * @date 2025
 */

#ifndef LADDER_FILTER_H
#define LADDER_FILTER_H

#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @class LadderFilter
 * @brief Moog-style 4-pole ladder filter with resonance
 *
 * True ladder topology: 4 one-pole filters with global feedback.
 * Each stage includes tanh saturation for analog-like warmth.
 */
class LadderFilter {
public:
    enum class Type {
        LOWPASS,  ///< 4-pole lowpass (24 dB/octave)
        HIGHPASS  ///< 4-pole highpass (24 dB/octave)
    };

    LadderFilter() = default;

    /**
     * @brief Initialize filter for given sample rate
     * @param sampleRate Sample rate in Hz
     */
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        reset();
    }

    /**
     * @brief Set filter parameters
     * @param frequency Cutoff frequency in Hz (10 Hz to Nyquist-10%)
     * @param resonance Resonance (0.0 to 1.0)
     * @param type Filter type (LOWPASS or HIGHPASS)
     * @note RT-SAFE
     */
    void setParameters(float frequency, float resonance, Type type) noexcept {
        type_ = type;

        // Clamp frequency to safe range
        frequency = std::max(10.0f, std::min(frequency, static_cast<float>(sampleRate_ * 0.45f)));
        resonance = std::max(0.0f, std::min(resonance, 1.0f));

        // One-pole coefficient: 1 - exp(-2π * fc / fs)
        // More accurate for ladder topology than bilinear transform
        const float omega = static_cast<float>(2.0 * M_PI * frequency / sampleRate_);
        coeff_ = 1.0f - std::exp(-omega);

        // Resonance: 0-4 range (4 = self-oscillation)
        resonance_ = resonance * 4.0f;
    }

    /**
     * @brief Process single sample through ladder filter
     * @param input Input sample
     * @return Filtered output
     * @note RT-SAFE
     */
    inline float processSample(float input) noexcept {
        // Store original input for highpass mode
        const float original = input;

        // Global feedback from output (Moog ladder topology)
        const float feedback = state_[3] * resonance_;
        input = input - feedback;

        // 4 cascaded one-pole filters with per-stage saturation
        // state[n] += coeff * (tanh(input[n]) - state[n])
        state_[0] += coeff_ * (std::tanh(input) - state_[0]);
        state_[1] += coeff_ * (std::tanh(state_[0]) - state_[1]);
        state_[2] += coeff_ * (std::tanh(state_[1]) - state_[2]);
        state_[3] += coeff_ * (std::tanh(state_[2]) - state_[3]);

        // Output based on filter type
        if (type_ == Type::HIGHPASS) {
            // Highpass = original - lowpass
            return original - state_[3];
        }

        return state_[3];  // Lowpass output
    }

    /**
     * @brief Process block of samples
     * @param input Input buffer
     * @param output Output buffer (can be same as input)
     * @param samples Number of samples
     * @note RT-SAFE
     */
    inline void processBlock(const float* input, float* output, int samples) noexcept {
        for (int i = 0; i < samples; ++i) {
            output[i] = processSample(input[i]);
        }
    }

    /**
     * @brief Reset filter state
     * @note RT-SAFE
     */
    void reset() noexcept {
        for (int i = 0; i < 4; ++i) {
            state_[i] = 0.0f;
        }
    }

private:
    double sampleRate_{48000.0};  ///< Current sample rate
    float coeff_{0.0f};           ///< One-pole filter coefficient
    float resonance_{0.0f};       ///< Feedback amount (0-4)
    float state_[4]{0.0f};        ///< 4-pole filter states
    Type type_{Type::LOWPASS};    ///< Filter type
};

/**
 * @class StereoLadderFilter
 * @brief Stereo wrapper for LadderFilter
 */
class StereoLadderFilter {
public:
    StereoLadderFilter() = default;

    void prepare(double sampleRate) noexcept {
        left_.prepare(sampleRate);
        right_.prepare(sampleRate);
    }

    void setParameters(float frequency, float resonance, LadderFilter::Type type) noexcept {
        left_.setParameters(frequency, resonance, type);
        right_.setParameters(frequency, resonance, type);
    }

    void processBlock(float* left, float* right, int samples) noexcept {
        left_.processBlock(left, left, samples);
        right_.processBlock(right, right, samples);
    }

    void reset() noexcept {
        left_.reset();
        right_.reset();
    }

private:
    LadderFilter left_;
    LadderFilter right_;
};

#endif // LADDER_FILTER_H
