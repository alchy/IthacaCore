/**
 * @file bbe_processor.h
 * @brief BBE Sound Processor - Professional audio enhancement
 * 
 * Implementation of BBE Sound Inc. high-definition audio processing
 * technology based on BA3884F/BA3884S IC specifications. Provides
 * phase compensation, harmonic enhancement, and bass boost for
 * natural, clear sound reproduction.
 * 
 * Optimalizace:
 * - Omezení basového boostu na 9 dB pro prevenci clippingu
 * - Vyhlazení změn bassBoostLevel pro eliminaci kliků
 * - Soft clipping při rekombinaci pásem pro hladší výstup
 * 
 * @author IthacaCore Audio Team
 * @version 1.2.0
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

    /**
     * @brief Soft clipping function to prevent hard clipping
     * @param x Input sample
     * @return Clipped output
     * @note RT-SAFE, inline, SIMD-friendly
     */
    inline float softClip(float x) const noexcept {
        x = std::max(-1.5f, std::min(1.5f, x));
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

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