/**
 * @file dsp_chain.h
 * @brief Container pro řetězení DSP efektů
 *
 * DspChain drží kolekci DSP efektů a zpracovává je sériově
 * (jeden po druhém) v pořadí, ve kterém byly přidány.
 *
 * THREAD SAFETY:
 * - addEffect() - NE RT-safe (volat pouze při inicializaci)
 * - prepare(), reset(), process() - RT-safe
 * - getEffect(), getEffectCount() - RT-safe (read-only)
 */

#pragma once

#include "dsp_effect.h"
#include <vector>
#include <memory>
#include <cstddef>

/**
 * @class DspChain
 * @brief Container pro sériové zpracování DSP efektů
 *
 * Použití:
 * 1. Vytvoř chain
 * 2. Přidej efekty pomocí addEffect() (při inicializaci)
 * 3. Zavolej prepare() s audio parametry
 * 4. Volej process() z audio threadu
 *
 * Efekty jsou zpracovány v pořadí přidání:
 * Input → Effect[0] → Effect[1] → ... → Effect[N] → Output
 */
class DspChain {
public:
    /**
     * @brief Konstruktor - vytvoří prázdný chain
     */
    DspChain();

    /**
     * @brief Destruktor
     */
    ~DspChain();

    // ========================================================================
    // Lifecycle (ne RT-safe)
    // ========================================================================

    /**
     * @brief Připraví všechny efekty pro audio processing
     * @param sampleRate Sample rate v Hz
     * @param maxBlockSize Maximální velikost audio bloku
     *
     * @note NENÍ RT-safe - volat před začátkem audio processingu
     */
    void prepare(int sampleRate, int maxBlockSize);

    /**
     * @brief Resetuje všechny efekty
     *
     * @note RT-safe - lze volat z audio threadu
     */
    void reset() noexcept;

    // ========================================================================
    // Processing (RT-safe)
    // ========================================================================

    /**
     * @brief Zpracuje audio blok přes všechny efekty (in-place)
     * @param leftBuffer Levý kanál (bude modifikován)
     * @param rightBuffer Pravý kanál (bude modifikován)
     * @param numSamples Počet samplů
     *
     * @note RT-safe - volat z audio threadu
     * @note Efekty jsou aplikovány sériově v pořadí přidání
     * @note Pokud efekt je disabled, přeskočí se
     */
    void process(float* leftBuffer, float* rightBuffer, int numSamples) noexcept;

    // ========================================================================
    // Effect Management
    // ========================================================================

    /**
     * @brief Přidá efekt do chainu
     * @param effect Unique pointer na efekt (převezme ownership)
     *
     * @note NENÍ RT-safe - volat pouze při inicializaci
     * @note Efekt je přidán na konec chainu
     */
    void addEffect(std::unique_ptr<DspEffect> effect);

    /**
     * @brief Získá pointer na efekt podle indexu
     * @param index Index efektu (0 = první)
     * @return Pointer na efekt nebo nullptr pokud index je mimo rozsah
     *
     * @note RT-safe (read-only)
     * @note Použij pro přístup k setterům/getterům specifického efektu
     */
    DspEffect* getEffect(size_t index) const;

    /**
     * @brief Vrací počet efektů v chainu
     * @return Počet efektů
     *
     * @note RT-safe (read-only)
     */
    size_t getEffectCount() const noexcept;

private:
    std::vector<std::unique_ptr<DspEffect>> effects_;  // Kolekce efektů
    bool isPrepared_;                                   // Příznak prepare() volání

    // Non-copyable
    DspChain(const DspChain&) = delete;
    DspChain& operator=(const DspChain&) = delete;
};
