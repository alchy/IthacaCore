/**
 * @file limiter.h
 * @brief Soft limiter s MIDI interface pro prevenci clippingu
 *
 * Tento limiter zajišťuje, že výstupní audio nepřekročí maximální
 * úroveň 1.0f. Používá smooth gain reduction envelope pro
 * transparentní limiting bez artifacts.
 *
 * FEATURES:
 * - Soft limiting s plynulým přechodem
 * - RT-safe processing
 * - MIDI interface (0-127) pro threshold a release
 * - Gain reduction metering pro GUI
 *
 * ALGORITHM:
 * - Peak detection s envelope follower
 * - Smooth attack (instant) a release (adjustable)
 * - Zero latency (no look-ahead)
 */

#pragma once

#include "../dsp_effect.h"
#include <atomic>
#include <cstdint>
#include <cmath>
#include <algorithm>

/**
 * @class Limiter
 * @brief Soft limiter s MIDI parametry
 *
 * MIDI Mapování:
 * - Threshold: 0 = -20 dB (hard), 127 = 0 dB (off/transparent)
 * - Release: 0 = 1 ms (fast), 127 = 1000 ms (slow)
 * - Enabled: 0 = off, 1-127 = on
 *
 * Gain Reduction Metering:
 * - 127 = no reduction (1.0 linear)
 * - 0 = maximum reduction
 */
class Limiter : public DspEffect {
public:
    /**
     * @brief Konstruktor - vytvoří limiter s defaultními hodnotami
     *
     * Default:
     * - Threshold: 0 dB (127 MIDI) - transparent
     * - Release: 50 ms (cca 64 MIDI)
     * - Enabled: true
     */
    Limiter();

    /**
     * @brief Destruktor
     */
    ~Limiter() override = default;

    // ========================================================================
    // DspEffect Interface Implementation
    // ========================================================================

    void prepare(int sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process(float* leftBuffer, float* rightBuffer, int numSamples) noexcept override;
    void setEnabled(bool enabled) noexcept override;
    bool isEnabled() const noexcept override;
    const char* getName() const noexcept override { return "Limiter"; }

    // ========================================================================
    // MIDI API (0-127) - RT-safe
    // ========================================================================

    /**
     * @brief Nastaví threshold pomocí MIDI hodnoty (0-127)
     * @param midiValue 0 = -20 dB (hard limiting), 127 = 0 dB (no limiting)
     *
     * @note RT-safe - lze volat z audio threadu
     */
    void setThresholdMIDI(uint8_t midiValue) noexcept;

    /**
     * @brief Nastaví release time pomocí MIDI hodnoty (0-127)
     * @param midiValue 0 = 1 ms (fast), 127 = 1000 ms (slow)
     *
     * @note RT-safe - lze volat z audio threadu
     * @note Exponenciální mapování pro plynulou kontrolu
     */
    void setReleaseMIDI(uint8_t midiValue) noexcept;

    /**
     * @brief Získá threshold jako MIDI hodnotu (0-127)
     * @return MIDI hodnota threshold
     *
     * @note RT-safe
     */
    uint8_t getThresholdMIDI() const noexcept;

    /**
     * @brief Získá release jako MIDI hodnotu (0-127)
     * @return MIDI hodnota release
     *
     * @note RT-safe
     */
    uint8_t getReleaseMIDI() const noexcept;

    /**
     * @brief Získá aktuální gain reduction pro metering (0-127)
     * @return 127 = no reduction, 0 = maximum reduction
     *
     * @note RT-safe
     * @note Použití v GUI pro vizualizaci limitingu
     */
    uint8_t getGainReductionMIDI() const noexcept;

    // ========================================================================
    // Internal API (pro přímé C++ použití) - RT-safe
    // ========================================================================

    /**
     * @brief Nastaví threshold v dB
     * @param thresholdDb Threshold v rozsahu -20.0 až 0.0 dB
     *
     * @note RT-safe - lze volat z audio threadu
     */
    void setThreshold(float thresholdDb) noexcept;

    /**
     * @brief Nastaví release time v milisekundách
     * @param releaseMs Release v rozsahu 1 až 1000 ms
     *
     * @note RT-safe - lze volat z audio threadu
     */
    void setRelease(float releaseMs) noexcept;

    /**
     * @brief Získá threshold v dB
     * @return Threshold v dB
     *
     * @note RT-safe
     */
    float getThreshold() const noexcept;

    /**
     * @brief Získá release v ms
     * @return Release v milisekundách
     *
     * @note RT-safe
     */
    float getRelease() const noexcept;

    /**
     * @brief Získá aktuální gain reduction (0.0 - 1.0)
     * @return 1.0 = no reduction, 0.0 = maximum reduction
     *
     * @note RT-safe
     */
    float getCurrentGainReduction() const noexcept;

private:
    // ========================================================================
    // Atomic Parameters (RT-safe parameter changes)
    // ========================================================================

    std::atomic<float> thresholdLinear_;    // Linear threshold (0.0 - 1.0)
    std::atomic<float> releaseCoeff_;       // Release coefficient (0.0 - 1.0)
    std::atomic<bool> enabled_;             // Enable/disable flag

    // MIDI cache pro gettery
    std::atomic<uint8_t> thresholdMIDI_;    // Cached MIDI value
    std::atomic<uint8_t> releaseMIDI_;      // Cached MIDI value

    // ========================================================================
    // Processing State (modifikováno pouze v process())
    // ========================================================================

    float envelope_;                        // Gain reduction envelope (1.0 = no reduction)
    int sampleRate_;                        // Current sample rate

    // ========================================================================
    // Conversion Helpers (static, pure functions)
    // ========================================================================

    /**
     * @brief Převede MIDI hodnotu na threshold v dB
     * @param midiValue MIDI hodnota (0-127)
     * @return Threshold v dB (-20.0 až 0.0)
     */
    static float midiToThresholdDb(uint8_t midiValue) noexcept;

    /**
     * @brief Převede MIDI hodnotu na release v ms
     * @param midiValue MIDI hodnota (0-127)
     * @return Release v ms (1 až 1000)
     */
    static float midiToReleaseMs(uint8_t midiValue) noexcept;

    /**
     * @brief Převede threshold v dB na MIDI hodnotu
     * @param thresholdDb Threshold v dB
     * @return MIDI hodnota (0-127)
     */
    static uint8_t thresholdDbToMidi(float thresholdDb) noexcept;

    /**
     * @brief Převede release v ms na MIDI hodnotu
     * @param releaseMs Release v ms
     * @return MIDI hodnota (0-127)
     */
    static uint8_t releaseMsToMidi(float releaseMs) noexcept;

    /**
     * @brief Převede dB na lineární hodnotu
     * @param db Hodnota v dB
     * @return Lineární hodnota
     */
    static float dbToLinear(float db) noexcept;

    /**
     * @brief Převede lineární hodnotu na dB
     * @param linear Lineární hodnota
     * @return Hodnota v dB
     */
    static float linearToDb(float linear) noexcept;

    /**
     * @brief Vypočítá release coefficient z release time
     * @param releaseMs Release time v ms
     * @param sampleRate Sample rate v Hz
     * @return Release coefficient (0.0 - 1.0)
     */
    static float calculateReleaseCoeff(float releaseMs, int sampleRate) noexcept;
};
