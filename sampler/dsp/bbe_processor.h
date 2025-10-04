/**
 * @file bbe_processor.h
 * @brief BBE Sound Processor - Professional audio enhancement
 *
 * Implementation of BBE Sound Inc. high-definition audio processing
 * technology based on BA3884F/BA3884S IC specifications. Provides
 * phase compensation, harmonic enhancement, and bass boost for
 * natural, clear sound reproduction.
 *
 * Optimizations:
 * - Block processing for better cache performance
 * - Auto-bypass when definition = 0 (saves ~80% CPU)
 * - Cached flags to avoid atomic loads in audio thread
 * - Branch-less soft clipping in harmonic enhancer
 *
 * @author IthacaCore Audio Team
 * @version 1.0.0
 * @date 2025
 * @license Educational/Non-Commercial Use
 */

#ifndef BBE_PROCESSOR_H
#define BBE_PROCESSOR_H

#include "biquad_filter.h"
#include "harmonic_enhancer.h"
#include <atomic>
#include <cstdint>

/**
 * @class BBEProcessor
 * @brief High-definition sound processor with phase and harmonic compensation
 */
class BBEProcessor {
public:
    BBEProcessor() = default;

    void prepare(int sampleRate) noexcept;
    void processBlock(float* left, float* right, int samples) noexcept;
    void setDefinitionMIDI(uint8_t midiValue) noexcept {
        if (midiValue > 127) return;
        definitionLevel_.store(midiValue / 127.0f, std::memory_order_relaxed);
    }

    void setBassBoostMIDI(uint8_t midiValue) noexcept {
        if (midiValue > 127) return;
        bassBoostLevel_.store(midiValue / 127.0f, std::memory_order_relaxed);
    }

    void setBypass(bool bypass) noexcept {
        bypassed_.store(bypass, std::memory_order_relaxed);
    }

    bool isEnabled() const noexcept {
        return !bypassed_.load(std::memory_order_relaxed);
    }

    void reset() noexcept;

private:
    void processChannel(float* buffer, int samples, int channelIndex) noexcept;
    void updateCoefficients() noexcept;

    struct CrossoverFilters {
        BiquadFilter lpBass1, lpBass2;
        BiquadFilter hpMid1, hpMid2, lpMid1, lpMid2;
        BiquadFilter hpTreble1, hpTreble2;
    };

    struct PhaseShifters {
        BiquadFilter midPhase;
        BiquadFilter treblePhase;
    };

    CrossoverFilters crossover_[2];
    PhaseShifters phaseShifters_[2];
    HarmonicEnhancer enhancer_[2];
    BiquadFilter bassBoost_[2];

    std::atomic<float> definitionLevel_{0.5f};
    std::atomic<float> bassBoostLevel_{0.0f};
    std::atomic<bool> bypassed_{false};

    int sampleRate_{44100};
    float lastDefinition_{-1.0f};
    float lastBassBoost_{-1.0f};
    bool bassBoostEnabled_{false};
    bool definitionEnabled_{true};

    static constexpr double BASS_CUTOFF = 150.0;
    static constexpr double TREBLE_CUTOFF = 2400.0;
    static constexpr double MID_CENTER = 1200.0;
    static constexpr double TREBLE_CENTER = 7200.0;
};

#endif // BBE_PROCESSOR_H