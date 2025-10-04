/**
 * @file dsp_effect.h
 * @brief Base interface pro všechny DSP efekty v ithaca-core
 *
 * Tento interface definuje základní API pro všechny DSP efekty.
 * Všechny metody jsou RT-safe a mohou být volány z audio threadu.
 *
 * DESIGN PRINCIPLES:
 * - Platform-agnostic (žádné JUCE závislosti)
 * - RT-safe (lock-free, bounded execution time)
 * - In-place processing (modifikuje buffery přímo)
 * - MIDI interface (0-127) pro konzistentní ovládání
 */

#pragma once

#include <cstdint>

/**
 * @class DspEffect
 * @brief Pure virtual interface pro DSP efekty
 *
 * Každý efekt implementuje tento interface a poskytuje:
 * - Lifecycle metody (prepare, reset)
 * - RT-safe processing
 * - Enable/disable funkcionalitu
 * - MIDI parametry (0-127) pro platform-agnostic ovládání
 */
class DspEffect {
public:
    virtual ~DspEffect() = default;

    // ========================================================================
    // Lifecycle (ne RT-safe - volat pouze při inicializaci)
    // ========================================================================

    /**
     * @brief Připraví efekt pro zpracování audio
     * @param sampleRate Sample rate v Hz (44100, 48000, atd.)
     * @param maxBlockSize Maximální počet samplů v jednom bloku
     *
     * @note NENÍ RT-safe - volat pouze před začátkem audio processingu
     */
    virtual void prepare(int sampleRate, int maxBlockSize) = 0;

    /**
     * @brief Resetuje interní stav efektu
     *
     * @note RT-safe - lze volat z audio threadu
     */
    virtual void reset() noexcept = 0;

    // ========================================================================
    // Processing (RT-safe - volat z audio threadu)
    // ========================================================================

    /**
     * @brief Zpracuje audio blok (in-place)
     * @param leftBuffer Levý kanál (bude modifikován)
     * @param rightBuffer Pravý kanál (bude modifikován)
     * @param numSamples Počet samplů k zpracování
     *
     * @note RT-safe - garantovaný bounded execution time
     * @note Processing je in-place (buffery jsou modifikovány přímo)
     */
    virtual void process(float* leftBuffer, float* rightBuffer, int numSamples) noexcept = 0;

    // ========================================================================
    // State Management (RT-safe)
    // ========================================================================

    /**
     * @brief Zapne nebo vypne efekt
     * @param enabled true = zapnuto, false = vypnuto (bypass)
     *
     * @note RT-safe - lze volat z audio threadu
     * @note Když disabled, process() by měl být no-op nebo rychlý bypass
     */
    virtual void setEnabled(bool enabled) noexcept = 0;

    /**
     * @brief Zjistí, zda je efekt zapnutý
     * @return true pokud zapnuto, false pokud vypnuto
     *
     * @note RT-safe
     */
    virtual bool isEnabled() const noexcept = 0;

    // ========================================================================
    // Info
    // ========================================================================

    /**
     * @brief Vrací název efektu
     * @return C-string s názvem (např. "Limiter", "Reverb")
     *
     * @note Pointer je platný po celou dobu života objektu
     */
    virtual const char* getName() const noexcept = 0;
};
