/**
 * @file biquad_filter.h
 * @brief Professional biquad IIR filter implementation for audio DSP
 * 
 * Implementation of industry-standard biquad (2-pole, 2-zero) IIR filter
 * using Direct Form I topology. Supports multiple filter types commonly
 * used in audio processing and BBE sound enhancement.
 * 
 * Optimalizace:
 * - SIMD (SSE) podpora pro processBlock pro vyšší výkon
 * - Denormal protection pro numerickou stabilitu
 * - RT-safe processing bez alokací
 * 
 * @author IthacaCore Audio Team
 * @version 1.2.0
 * @date 2025
 */

#ifndef BIQUAD_FILTER_H
#define BIQUAD_FILTER_H

#include <cmath>
#include <algorithm>
#ifdef __SSE__
#include <immintrin.h> // Pro SSE instrukce
#endif

// Definice M_PI pro kompatibilitu s MSVC
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
     */
    inline void setCoefficients(Type type, double sampleRate, double frequency,
                                double Q = 0.707, double gainDB = 0.0) noexcept {
        // Parameter validation
        frequency = std::max(10.0, std::min(frequency, sampleRate * 0.49));
        Q = std::max(0.1, std::min(Q, 20.0));

        // Pre-calculate common terms
        const double omega = 2.0 * M_PI * frequency / sampleRate;
        const double sinOmega = std::sin(omega);
        const double cosOmega = std::cos(omega);
        const double alpha = sinOmega / (2.0 * Q);
        const double A = std::pow(10.0, gainDB / 40.0);

        double b0, b1, b2, a0, a1, a2;
        switch (type) {
            case Type::LOWPASS:
                b0 = (1.0 - cosOmega) / 2.0;
                b1 = 1.0 - cosOmega;
                b2 = (1.0 - cosOmega) / 2.0;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosOmega;
                a2 = 1.0 - alpha;
                break;
            case Type::HIGHPASS:
                b0 = (1.0 + cosOmega) / 2.0;
                b1 = -(1.0 + cosOmega);
                b2 = (1.0 + cosOmega) / 2.0;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosOmega;
                a2 = 1.0 - alpha;
                break;
            case Type::BANDPASS:
                b0 = alpha;
                b1 = 0.0;
                b2 = -alpha;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosOmega;
                a2 = 1.0 - alpha;
                break;
            case Type::PEAKING:
                b0 = 1.0 + alpha * A;
                b1 = -2.0 * cosOmega;
                b2 = 1.0 - alpha * A;
                a0 = 1.0 + alpha / A;
                a1 = -2.0 * cosOmega;
                a2 = 1.0 - alpha / A;
                break;
            case Type::LOW_SHELF:
                {
                    const double beta = std::sqrt(A) / Q;
                    b0 = A * ((A + 1.0) - (A - 1.0) * cosOmega + beta * sinOmega);
                    b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosOmega);
                    b2 = A * ((A + 1.0) - (A - 1.0) * cosOmega - beta * sinOmega);
                    a0 = (A + 1.0) + (A - 1.0) * cosOmega + beta * sinOmega;
                    a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosOmega);
                    a2 = (A + 1.0) + (A - 1.0) * cosOmega - beta * sinOmega;
                }
                break;
            case Type::HIGH_SHELF:
                {
                    const double beta = std::sqrt(A) / Q;
                    b0 = A * ((A + 1.0) + (A - 1.0) * cosOmega + beta * sinOmega);
                    b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosOmega);
                    b2 = A * ((A + 1.0) + (A - 1.0) * cosOmega - beta * sinOmega);
                    a0 = (A + 1.0) - (A - 1.0) * cosOmega + beta * sinOmega;
                    a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosOmega);
                    a2 = (A + 1.0) - (A - 1.0) * cosOmega - beta * sinOmega;
                }
                break;
            case Type::ALLPASS_180:
                {
                    const double tan_omega = std::tan(omega / 2.0);
                    b0 = (tan_omega - 1.0) / (tan_omega + 1.0);
                    b1 = 1.0;
                    b2 = 0.0;
                    a0 = 1.0;
                    a1 = b0;
                    a2 = 0.0;
                }
                break;
            case Type::ALLPASS_360:
                b0 = 1.0 - alpha;
                b1 = -2.0 * cosOmega;
                b2 = 1.0 + alpha;
                a0 = 1.0 + alpha;
                a1 = -2.0 * cosOmega;
                a2 = 1.0 - alpha;
                break;
            default:
                b0 = 1.0; b1 = 0.0; b2 = 0.0;
                a0 = 1.0; a1 = 0.0; a2 = 0.0;
                break;
        }

        if (std::abs(a0) < 1e-10) {
            b0_ = 1.0f; b1_ = 0.0f; b2_ = 0.0f;
            a1_ = 0.0f; a2_ = 0.0f;
            return;
        }

        b0_ = static_cast<float>(b0 / a0);
        b1_ = static_cast<float>(b1 / a0);
        b2_ = static_cast<float>(b2 / a0);
        a1_ = static_cast<float>(a1 / a0);
        a2_ = static_cast<float>(a2 / a0);
    }

    inline float processSample(float input) noexcept {
        const float output = b0_ * input + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = input;
        y2_ = y1_; y1_ = output;
        if (std::abs(y1_) < 1e-20f) y1_ = 0.0f;
        if (std::abs(y2_) < 1e-20f) y2_ = 0.0f;
        return output;
    }

    void processBlock(const float* input, float* output, int samples) noexcept {
#ifdef __SSE__
        // SIMD zpracování pro 4 vzorky najednou
        int i = 0;
        __m128 x1 = _mm_set1_ps(x1_);
        __m128 x2 = _mm_set1_ps(x2_);
        __m128 y1 = _mm_set1_ps(y1_);
        __m128 y2 = _mm_set1_ps(y2_);
        __m128 b0 = _mm_set1_ps(b0_);
        __m128 b1 = _mm_set1_ps(b1_);
        __m128 b2 = _mm_set1_ps(b2_);
        __m128 a1 = _mm_set1_ps(a1_);
        __m128 a2 = _mm_set1_ps(a2_);
        __m128 denormThreshold = _mm_set1_ps(1e-20f);
        __m128 zero = _mm_setzero_ps();

        for (; i <= samples - 4; i += 4) {
            __m128 x = _mm_loadu_ps(&input[i]);
            __m128 y = _mm_add_ps(_mm_mul_ps(b0, x),
                                 _mm_add_ps(_mm_mul_ps(b1, x1),
                                           _mm_add_ps(_mm_mul_ps(b2, x2),
                                                     _mm_sub_ps(_mm_mul_ps(a1, y1),
                                                               _mm_mul_ps(a2, y2))));
            _mm_storeu_ps(&output[i], y);
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;

            __m128 absY1 = _mm_andnot_ps(_mm_set1_ps(-0.0f), y1);
            __m128 absY2 = _mm_andnot_ps(_mm_set1_ps(-0.0f), y2);
            y1 = _mm_blendv_ps(y1, zero, _mm_cmplt_ps(absY1, denormThreshold));
            y2 = _mm_blendv_ps(y2, zero, _mm_cmplt_ps(absY2, denormThreshold));
        }

        float temp[4];
        _mm_storeu_ps(temp, x1); x1_ = temp[3];
        _mm_storeu_ps(temp, x2); x2_ = temp[3];
        _mm_storeu_ps(temp, y1); y1_ = temp[3];
        _mm_storeu_ps(temp, y2); y2_ = temp[3];

        for (; i < samples; ++i) {
            output[i] = processSample(input[i]);
        }
#else
        for (int i = 0; i < samples; ++i) {
            output[i] = processSample(input[i]);
        }
#endif
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