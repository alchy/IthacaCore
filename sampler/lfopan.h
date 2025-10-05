#ifndef LFOPAN_H
#define LFOPAN_H

#include <cstdint>

/**
 * @class LfoPanning
 * @brief Spravuje automatické LFO panning s předpočítanými lookup tabulkami
 * 
 * Poskytuje RT-safe výpočty LFO panningu pro efekty elektrického piana.
 * Používá předpočítané sinusové tabulky pro hladký pohyb panoramy mezi kanály.
 * Podporuje MIDI ovládání rychlosti a hloubky s kubickou interpolací.
 */
class LfoPanning {
public:
    /**
     * @brief Inicializuje lookup tabulky pro LFO panning
     * @note Voláno jednou během konstrukce VoiceManager
     */
    static void initializeLfoTables();

    /**
     * @brief Převede MIDI hodnotu rychlosti na frekvenci LFO v Hz
     * @param midi_speed MIDI hodnota (0-127)
     * @return Frekvence LFO (0.0-2.0 Hz)
     * @note RT-safe: lineární interpolace z předpočítané tabulky
     */
    static float getFrequencyFromMIDI(uint8_t midi_speed) noexcept;

    /**
     * @brief Převede MIDI hodnotu hloubky na multiplikátor amplitudy
     * @param midi_depth MIDI hodnota (0-127)
     * @return Multiplikátor amplitudy (0.0-1.0)
     * @note RT-safe: lineární interpolace z předpočítané tabulky
     */
    static float getDepthFromMIDI(uint8_t midi_depth) noexcept;

    /**
     * @brief Získá hodnotu sinusovky z předpočítané lookup tabulky
     * @param phase Fáze (0.0-2π)
     * @return Sinusová hodnota (-1.0 až +1.0)
     * @note RT-safe: používá předpočítané hodnoty s kubickou interpolací
     */
    static float getSineValue(float phase) noexcept;

    /**
     * @brief Vypočítá přírůstek fáze na vzorek pro danou frekvenci
     * @param frequency Frekvence LFO v Hz
     * @param sampleRate Vzorkovací frekvence v Hz
     * @return Přírůstek fáze na vzorek
     * @note RT-safe: přímý výpočet
     */
    static float calculatePhaseIncrement(float frequency, int sampleRate) noexcept;

    /**
     * @brief Ořízne fázi do platného rozsahu (0.0-2π)
     * @param phase Vstupní hodnota fáze
     * @return Ořezaná fáze (0.0-2π)
     * @note RT-safe: modulo operace s 2π
     */
    static float wrapPhase(float phase) noexcept;

    /**
     * @brief Kubická interpolace pro hladké přechody mezi hodnotami
     * @param y0 První hodnota
     * @param y1 Druhá hodnota
     * @param y2 Třetí hodnota
     * @param y3 Čtvrtá hodnota
     * @param mu Interpolace (0.0-1.0)
     * @return Interpolovaná hodnota
     * @note RT-safe: přímý výpočet
     */
    static float cubicInterpolate(float y0, float y1, float y2, float y3, float mu) noexcept;

    // Veřejná konstanta pro externí výpočty fází
    static constexpr float TWO_PI = 6.283185307179586f;

private:
    // Velikosti lookup tabulek
    static constexpr int MIDI_TABLE_SIZE = 128;
    static constexpr int SINE_TABLE_SIZE = 8192; // Zvýšeno pro vyšší přesnost
    
    // Předpočítané lookup tabulky
    static float frequency_table[MIDI_TABLE_SIZE];
    static float depth_table[MIDI_TABLE_SIZE];
    static float sine_table[SINE_TABLE_SIZE];
    static bool tables_initialized;
};

#endif // LFOPAN_H