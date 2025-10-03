/**
 * @file biquad_filter.cpp
 * @brief Implementation of biquad filter coefficient calculation
 * 
 * This file implements the coefficient calculation for various filter types
 * using the bilinear transform method. All calculations use double precision
 * for accuracy, then cast to float for runtime processing.
 * 
 * Bilinear Transform:
 * Maps continuous-time (analog) filter designs to discrete-time (digital)
 * using the substitution: s = 2/T * (1 - z^-1) / (1 + z^-1)
 * where T = sample period = 1/sampleRate
 * 
 * Pre-warping:
 * Compensates for frequency warping in bilinear transform using:
 * omega = 2 * pi * frequency / sampleRate
 * 
 * References:
 * - "Cookbook formulae for audio EQ biquad filter coefficients"
 *   by Robert Bristow-Johnson
 * - "Digital Signal Processing" by Oppenheim & Schafer
 * 
 * @author IthacaCore Audio Team
 * @version 1.0.0
 */

#include "biquad_filter.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void BiquadFilter::setCoefficients(Type type, double sampleRate, double frequency,
                                   double Q, double gainDB) noexcept {
    // ===== PARAMETER VALIDATION =====
    // Clamp frequency to valid range [10 Hz, Nyquist - 1%]
    // Upper limit at 49% of Nyquist prevents aliasing and numerical issues
    frequency = std::max(10.0, std::min(frequency, sampleRate * 0.49));
    
    // Clamp Q to reasonable range
    // Too low Q can cause numerical instability
    // Too high Q creates extremely narrow filters
    Q = std::max(0.1, std::min(Q, 20.0));
    
    // ===== PRE-CALCULATE COMMON TERMS =====
    // Normalized angular frequency (pre-warped for bilinear transform)
    const double omega = 2.0 * M_PI * frequency / sampleRate;
    const double sinOmega = std::sin(omega);
    const double cosOmega = std::cos(omega);
    
    // Alpha term (bandwidth control)
    // alpha = sin(omega) / (2*Q)
    // Determines filter bandwidth - smaller Q = wider bandwidth
    const double alpha = sinOmega / (2.0 * Q);
    
    // Amplitude term for shelving/peaking filters
    // A = 10^(gainDB/40) converts dB to linear amplitude
    // Division by 40 (not 20) because we're dealing with voltage gain
    const double A = std::pow(10.0, gainDB / 40.0);
    
    // Temporary coefficient storage (double precision for accuracy)
    double b0, b1, b2, a0, a1, a2;
    
    // ===== FILTER TYPE SPECIFIC COEFFICIENT CALCULATION =====
    
    switch (type) {
        case Type::LOWPASS: {
            // 2nd order Butterworth lowpass
            // Passes frequencies below cutoff, attenuates above at -12dB/octave
            // At cutoff: -3dB, phase shift = -90°
            b0 = (1.0 - cosOmega) / 2.0;
            b1 = 1.0 - cosOmega;
            b2 = (1.0 - cosOmega) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha;
            break;
        }
        
        case Type::HIGHPASS: {
            // 2nd order Butterworth highpass
            // Passes frequencies above cutoff, attenuates below at -12dB/octave
            // At cutoff: -3dB, phase shift = +90°
            b0 = (1.0 + cosOmega) / 2.0;
            b1 = -(1.0 + cosOmega);
            b2 = (1.0 + cosOmega) / 2.0;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha;
            break;
        }
        
        case Type::BANDPASS: {
            // Bandpass filter (constant skirt gain, peak gain = Q)
            // Passes frequencies around center frequency
            // Bandwidth determined by Q: BW ≈ frequency / Q
            // 0 dB peak gain, attenuates -6dB/octave on both sides
            b0 = alpha;
            b1 = 0.0;
            b2 = -alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha;
            break;
        }
        
        case Type::PEAKING: {
            // Parametric EQ bell filter
            // Boosts or cuts around center frequency
            // Q controls bandwidth: narrow Q = sharp peak, wide Q = gentle curve
            // Used for surgical EQ adjustments
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cosOmega;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha / A;
            break;
        }
        
        case Type::LOW_SHELF: {
            // Low frequency shelving filter
            // Boosts or cuts all frequencies below transition frequency
            // Smooth transition centered at 'frequency'
            // Used for bass boost/cut in BBE processor
            const double beta = std::sqrt(A) / Q;
            b0 = A * ((A + 1.0) - (A - 1.0) * cosOmega + beta * sinOmega);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosOmega);
            b2 = A * ((A + 1.0) - (A - 1.0) * cosOmega - beta * sinOmega);
            a0 = (A + 1.0) + (A - 1.0) * cosOmega + beta * sinOmega;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosOmega);
            a2 = (A + 1.0) + (A - 1.0) * cosOmega - beta * sinOmega;
            break;
        }
        
        case Type::HIGH_SHELF: {
            // High frequency shelving filter
            // Boosts or cuts all frequencies above transition frequency
            // Smooth transition centered at 'frequency'
            // Used for treble boost/cut in EQ applications
            const double beta = std::sqrt(A) / Q;
            b0 = A * ((A + 1.0) + (A - 1.0) * cosOmega + beta * sinOmega);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosOmega);
            b2 = A * ((A + 1.0) + (A - 1.0) * cosOmega - beta * sinOmega);
            a0 = (A + 1.0) - (A - 1.0) * cosOmega + beta * sinOmega;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosOmega);
            a2 = (A + 1.0) - (A - 1.0) * cosOmega - beta * sinOmega;
            break;
        }
        
        case Type::ALLPASS_180: {
            // 1st order all-pass filter for -180° phase shift
            // Passes all frequencies unchanged in magnitude
            // Only affects phase: -180° at center frequency
            // Used in BBE processor for mid band phase compensation
            // Formula: H(z) = (tan(ω/2) - 1) / (tan(ω/2) + 1)
            const double tan_omega = std::tan(omega / 2.0);
            b0 = (tan_omega - 1.0) / (tan_omega + 1.0);
            b1 = 1.0;
            b2 = 0.0;
            a0 = 1.0;
            a1 = b0;
            a2 = 0.0;
            break;
        }
        
        case Type::ALLPASS_360: {
            // 2nd order all-pass filter for -360° phase shift
            // Passes all frequencies unchanged in magnitude
            // Only affects phase: -360° at center frequency
            // Used in BBE processor for treble band phase compensation
            // Creates full cycle phase rotation
            b0 = 1.0 - alpha;
            b1 = -2.0 * cosOmega;
            b2 = 1.0 + alpha;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha;
            break;
        }

        default: {
            // Fallback to passthrough (identity filter)
            // Should never happen, but silences compiler warning
            b0 = 1.0;
            b1 = 0.0;
            b2 = 0.0;
            a0 = 1.0;
            a1 = 0.0;
            a2 = 0.0;
            break;
        }
    }
    
    // ===== NORMALIZE AND STORE COEFFICIENTS =====
    // Normalize all coefficients by a0 to get standard form: y[n] = b*x - a*y
    // Division by a0 ensures a0 = 1.0 in the difference equation
    // Cast to float for runtime efficiency (double precision not needed in processing)
    b0_ = static_cast<float>(b0 / a0);
    b1_ = static_cast<float>(b1 / a0);
    b2_ = static_cast<float>(b2 / a0);
    a1_ = static_cast<float>(a1 / a0);
    a2_ = static_cast<float>(a2 / a0);
    
    // Note: a0 itself is not stored as it's always 1.0 after normalization
}