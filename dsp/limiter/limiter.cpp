/**
 * @file limiter.cpp
 * @brief Implementace soft limiteru
 */

#include "limiter.h"
#include <cmath>
#include <algorithm>

// ============================================================================
// Constructor
// ============================================================================

Limiter::Limiter()
    : thresholdLinear_(1.0f),          // 0 dB = no limiting
      releaseCoeff_(0.999f),           // Cca 50 ms @ 48kHz
      enabled_(true),
      thresholdMIDI_(127),             // 0 dB
      releaseMIDI_(64),                // Cca 50 ms
      envelope_(1.0f),                 // No gain reduction
      sampleRate_(48000)
{
}

// ============================================================================
// DspEffect Interface Implementation
// ============================================================================

void Limiter::prepare(int sampleRate, int maxBlockSize)
{
    (void)maxBlockSize;  // Unused - limiter doesn't need buffer allocation

    sampleRate_ = sampleRate;

    // Přepočítej release coefficient pro nový sample rate
    uint8_t releaseMidi = releaseMIDI_.load(std::memory_order_relaxed);
    float releaseMs = midiToReleaseMs(releaseMidi);
    releaseCoeff_.store(calculateReleaseCoeff(releaseMs, sampleRate_), std::memory_order_relaxed);

    reset();
}

void Limiter::reset() noexcept
{
    envelope_ = 1.0f;  // Reset envelope to no reduction
}

void Limiter::process(float* leftBuffer, float* rightBuffer, int numSamples) noexcept
{
    if (!enabled_.load(std::memory_order_relaxed)) {
        return;  // Bypass when disabled
    }

    const float threshold = thresholdLinear_.load(std::memory_order_relaxed);
    const float releaseCoeff = releaseCoeff_.load(std::memory_order_relaxed);

    // Process each sample (attack is instant, no coefficient needed)
    for (int i = 0; i < numSamples; ++i) {
        // Find peak of stereo signal
        const float leftAbs = std::abs(leftBuffer[i]);
        const float rightAbs = std::abs(rightBuffer[i]);
        const float peak = std::max(leftAbs, rightAbs);

        // Calculate target gain
        float targetGain = 1.0f;
        if (peak > threshold) {
            targetGain = threshold / peak;  // Reduce gain to keep at threshold
        }

        // Smooth envelope follower
        if (targetGain < envelope_) {
            // Attack (instant)
            envelope_ = targetGain;
        } else {
            // Release (smooth)
            envelope_ = targetGain + releaseCoeff * (envelope_ - targetGain);
        }

        // Apply gain reduction
        leftBuffer[i] *= envelope_;
        rightBuffer[i] *= envelope_;
    }
}

void Limiter::setEnabled(bool enabled) noexcept
{
    enabled_.store(enabled, std::memory_order_relaxed);
}

bool Limiter::isEnabled() const noexcept
{
    return enabled_.load(std::memory_order_relaxed);
}

// ============================================================================
// MIDI API Implementation
// ============================================================================

void Limiter::setThresholdMIDI(uint8_t midiValue) noexcept
{
    thresholdMIDI_.store(midiValue, std::memory_order_relaxed);

    float thresholdDb = midiToThresholdDb(midiValue);
    setThreshold(thresholdDb);
}

void Limiter::setReleaseMIDI(uint8_t midiValue) noexcept
{
    releaseMIDI_.store(midiValue, std::memory_order_relaxed);

    float releaseMs = midiToReleaseMs(midiValue);
    setRelease(releaseMs);
}

uint8_t Limiter::getThresholdMIDI() const noexcept
{
    return thresholdMIDI_.load(std::memory_order_relaxed);
}

uint8_t Limiter::getReleaseMIDI() const noexcept
{
    return releaseMIDI_.load(std::memory_order_relaxed);
}

uint8_t Limiter::getGainReductionMIDI() const noexcept
{
    // Převést envelope (1.0 = no reduction) na MIDI (127 = no reduction)
    float gr = getCurrentGainReduction();
    return static_cast<uint8_t>(gr * 127.0f);
}

// ============================================================================
// Internal API Implementation
// ============================================================================

void Limiter::setThreshold(float thresholdDb) noexcept
{
    // Clamp to valid range
    thresholdDb = std::clamp(thresholdDb, -20.0f, 0.0f);

    // Convert to linear and store
    float linear = dbToLinear(thresholdDb);
    thresholdLinear_.store(linear, std::memory_order_relaxed);
}

void Limiter::setRelease(float releaseMs) noexcept
{
    // Clamp to valid range
    releaseMs = std::clamp(releaseMs, 1.0f, 1000.0f);

    // Calculate and store coefficient
    float coeff = calculateReleaseCoeff(releaseMs, sampleRate_);
    releaseCoeff_.store(coeff, std::memory_order_relaxed);
}

float Limiter::getThreshold() const noexcept
{
    float linear = thresholdLinear_.load(std::memory_order_relaxed);
    return linearToDb(linear);
}

float Limiter::getRelease() const noexcept
{
    uint8_t midiValue = releaseMIDI_.load(std::memory_order_relaxed);
    return midiToReleaseMs(midiValue);
}

float Limiter::getCurrentGainReduction() const noexcept
{
    return envelope_;  // 1.0 = no reduction, 0.0 = max reduction
}

// ============================================================================
// Conversion Helpers (Static)
// ============================================================================

float Limiter::midiToThresholdDb(uint8_t midiValue) noexcept
{
    // 0 → -20 dB, 127 → 0 dB (lineární mapování)
    float normalized = midiValue / 127.0f;
    return -20.0f + (normalized * 20.0f);
}

float Limiter::midiToReleaseMs(uint8_t midiValue) noexcept
{
    // 0 → 1 ms, 127 → 1000 ms (exponenciální mapování)
    // release_ms = 1.0 * exp((midiValue / 127.0) * ln(1000))
    float normalized = midiValue / 127.0f;
    return 1.0f * std::exp(normalized * std::log(1000.0f));
}

uint8_t Limiter::thresholdDbToMidi(float thresholdDb) noexcept
{
    // Clamp input
    thresholdDb = std::clamp(thresholdDb, -20.0f, 0.0f);

    // -20..0 dB → 0..1 → 0..127
    float normalized = (thresholdDb + 20.0f) / 20.0f;
    return static_cast<uint8_t>(normalized * 127.0f);
}

uint8_t Limiter::releaseMsToMidi(float releaseMs) noexcept
{
    // Clamp input
    releaseMs = std::clamp(releaseMs, 1.0f, 1000.0f);

    // Inverse exponential: ln(releaseMs / 1.0) / ln(1000) → 0..1 → 0..127
    float normalized = std::log(releaseMs) / std::log(1000.0f);
    return static_cast<uint8_t>(normalized * 127.0f);
}

float Limiter::dbToLinear(float db) noexcept
{
    // dB to linear: 10^(dB/20)
    return std::pow(10.0f, db / 20.0f);
}

float Limiter::linearToDb(float linear) noexcept
{
    // Linear to dB: 20 * log10(linear)
    // Clamp to avoid log(0)
    linear = std::max(linear, 0.00001f);
    return 20.0f * std::log10(linear);
}

float Limiter::calculateReleaseCoeff(float releaseMs, int sampleRate) noexcept
{
    // Release coefficient for exponential decay
    // Time constant: tau = releaseMs / 1000.0 seconds
    // Coefficient: exp(-1 / (tau * sampleRate))

    float tau = releaseMs / 1000.0f;  // Convert to seconds
    float samples = tau * static_cast<float>(sampleRate);

    // Coefficient for exponential decay
    return std::exp(-1.0f / samples);
}
