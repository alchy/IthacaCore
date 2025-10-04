/**
 * @file harmonic_enhancer.h
 * @brief Dynamic harmonic enhancement processor (BBE-style treble VCA)
 * 
 * Simulates the BA3884F IC's treble VCA behavior for dynamic harmonic enhancement.
 * Optimalizace:
 * - Omezení maximálního zesílení na 2.5x pro prevenci clippingu
 * - Vyhlazení změn definitionLevel pro eliminaci kliků
 * 
 * @author IthacaCore Audio Team
 * @version 1.1.0
 * @date 2025
 */

#ifndef HARMONIC_ENHANCER_H
#define HARMONIC_ENHANCER_H

#include <cmath>
#include <algorithm>

/**
 * @class HarmonicEnhancer
 * @brief Dynamic treble enhancement with envelope follower
 */
class HarmonicEnhancer {
public:
    /**
     * @brief Default constructor
     */
    HarmonicEnhancer() = default;

    /**
     * @brief Initialize enhancer for given sample rate
     * @param sampleRate Sample rate in Hz (44100, 48000, etc.)
     */
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        attackCoeff_ = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.01)));
        releaseCoeff_ = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.1)));
        gainSmoothCoeff_ = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.001)));
        reset();
    }

    /**
     * @brief Set enhancement intensity with smoothing
     * @param level Enhancement level (0.0 to 1.0)
     * @note RT-SAFE: Smooths changes to prevent clicks
     */
    void setDefinitionLevel(float level) noexcept {
        // Vyhlazení změn pro eliminaci kliků
        const float targetLevel = std::max(0.0f, std::min(1.0f, level));
        definitionLevel_ += (targetLevel - definitionLevel_) * 0.1f; // 10% za blok
    }

    /**
     * @brief Process block of audio samples
     * @param buffer Input/output buffer (modified in-place)
     * @param samples Number of samples
     * @note RT-SAFE: No allocations, optimized for performance
     */
    void processBlock(float* buffer, int samples) noexcept {
        for (int i = 0; i < samples; ++i) {
            const float input = buffer[i];
            const float inputAbs = std::abs(input);

            // Detekce obálky
            if (inputAbs > envelope_) {
                envelope_ += (inputAbs - envelope_) * (1.0f - attackCoeff_);
            } else {
                envelope_ += (inputAbs - envelope_) * (1.0f - releaseCoeff_);
            }

            // Dynamické zesílení (omezeno na 2.5x)
            const float dynamicFactor = 1.0f - std::min(1.0f, envelope_ * 2.0f);
            const float targetGain = 1.0f + (definitionLevel_ * 1.5f * dynamicFactor); // Omezeno z 2.0 na 1.5
            currentGain_ += (targetGain - currentGain_) * (1.0f - gainSmoothCoeff_);

            // Aplikace zesílení a soft clipping
            buffer[i] = softClip(input * currentGain_);
        }
    }

    /**
     * @brief Reset internal state
     * @note RT-SAFE
     */
    void reset() noexcept {
        envelope_ = 0.0f;
        currentGain_ = 1.0f;
        definitionLevel_ = 0.5f;
    }

private:
    /**
     * @brief Soft clipping function
     * @param x Input sample
     * @return Clipped output
     */
    inline float softClip(float x) const noexcept {
        x = std::max(-1.5f, std::min(1.5f, x));
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // Konfigurace
    double sampleRate_{44100.0};      ///< Current sample rate (Hz)
    float definitionLevel_{0.5f};     ///< Enhancement intensity (0.0-1.0)
    // Stav detektoru obálky
    float envelope_{0.0f};            ///< Current envelope level (0.0-1.0)
    float attackCoeff_{0.99f};        ///< Attack time coefficient (10ms)
    float releaseCoeff_{0.999f};      ///< Release time coefficient (100ms)
    // Stav vyhlazování zesílení
    float currentGain_{1.0f};         ///< Current smoothed gain
    float gainSmoothCoeff_{0.99f};    ///< Gain smoothing coefficient (1ms)
};

#endif // HARMONIC_ENHANCER_H