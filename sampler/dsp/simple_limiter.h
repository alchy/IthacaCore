/**
 * @file simple_limiter.h
 * @brief Simple peak limiter for preventing output saturation
 *
 * RT-safe peak limiter with envelope follower for smooth gain reduction.
 * Designed as final safety stage in audio output chain.
 *
 * Features:
 * - Fast attack (1ms) for peak detection
 * - Smooth release (100ms) to prevent pumping
 * - Brick-wall limiting at threshold
 * - No lookahead (zero latency)
 * - Stereo-linked operation (uses max of both channels)
 *
 * @author IthacaCore Audio Team
 * @version 1.0.0
 * @date 2025
 */

#ifndef SIMPLE_LIMITER_H
#define SIMPLE_LIMITER_H

#include <cmath>
#include <algorithm>

/**
 * @class SimpleLimiter
 * @brief RT-safe peak limiter for output protection
 */
class SimpleLimiter {
public:
    /**
     * @brief Constructor with default settings
     */
    SimpleLimiter() = default;

    /**
     * @brief Initialize limiter for given sample rate
     * @param sampleRate Sample rate in Hz (44100, 48000, etc.)
     */
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;

        // Attack: 1ms (fast reaction to peaks)
        attackCoeff_ = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.001)));

        // Release: 100ms (smooth return to normal)
        releaseCoeff_ = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.1)));

        reset();
    }

    /**
     * @brief Set limiter threshold
     * @param thresholdDB Threshold in dBFS (e.g., -0.3 dBFS)
     * @note RT-SAFE
     */
    void setThreshold(float thresholdDB) noexcept {
        // Convert dBFS to linear (e.g., -0.3 dBFS = 0.966)
        threshold_ = std::pow(10.0f, thresholdDB / 20.0f);
    }

    /**
     * @brief Process stereo block (stereo-linked)
     * @param left Left channel buffer (modified in-place)
     * @param right Right channel buffer (modified in-place)
     * @param samples Number of samples
     * @note RT-SAFE: No allocations, optimized for performance
     */
    void processBlock(float* left, float* right, int samples) noexcept {
        for (int i = 0; i < samples; ++i) {
            // Detect peak from both channels (stereo-linked)
            const float peakLeft = std::abs(left[i]);
            const float peakRight = std::abs(right[i]);
            const float peak = std::max(peakLeft, peakRight);

            // Envelope follower with attack/release
            if (peak > envelope_) {
                // Attack: fast response to peaks
                envelope_ += (peak - envelope_) * (1.0f - attackCoeff_);
            } else {
                // Release: slow return to normal
                envelope_ += (peak - envelope_) * (1.0f - releaseCoeff_);
            }

            // Calculate gain reduction
            float gainReduction = 1.0f;
            if (envelope_ > threshold_) {
                // Brick-wall limiting: gain = threshold / envelope
                gainReduction = threshold_ / envelope_;
            }

            // Apply gain reduction to both channels (stereo-linked)
            left[i] *= gainReduction;
            right[i] *= gainReduction;
        }
    }

    /**
     * @brief Reset internal state
     * @note RT-SAFE
     */
    void reset() noexcept {
        envelope_ = 0.0f;
    }

    /**
     * @brief Get current gain reduction amount
     * @return Gain reduction in linear (1.0 = no reduction, 0.5 = -6dB reduction)
     */
    float getCurrentGainReduction() const noexcept {
        if (envelope_ > threshold_) {
            return threshold_ / envelope_;
        }
        return 1.0f;
    }

    /**
     * @brief Get current gain reduction in dB
     * @return Gain reduction in dB (0.0 = no reduction, negative = reduction)
     */
    float getCurrentGainReductionDB() const noexcept {
        float gr = getCurrentGainReduction();
        if (gr < 1.0f) {
            return 20.0f * std::log10(gr);
        }
        return 0.0f;
    }

private:
    double sampleRate_{48000.0};      ///< Current sample rate (Hz)
    float threshold_{0.966f};         ///< Threshold in linear (-0.3 dBFS)
    float envelope_{0.0f};            ///< Current envelope level
    float attackCoeff_{0.99f};        ///< Attack time coefficient (1ms)
    float releaseCoeff_{0.999f};      ///< Release time coefficient (100ms)
};

#endif // SIMPLE_LIMITER_H
