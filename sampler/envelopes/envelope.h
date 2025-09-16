#pragma once
#include <cstdint>
#include "envelope_static_data.h"

/**
 * @brief Per-voice envelope state manager (refaktorovaná třída)
 * 
 * Tato třída nyní slouží pouze jako per-voice wrapper pro statická envelope data.
 * Všechna těžká data (předpočítané křivky) jsou ve třídě EnvelopeStaticData,
 * což dramaticky snižuje paměťovou spotřebu při více instancích VoiceManager.
 * 
 * Zachovává původní API pro kompatibilitu s Voice třídou, ale interně
 * deleguje všechny operace na EnvelopeStaticData.
 */
class Envelope {
public:
    /**
     * @brief Prázdný konstruktor
     * Inicializuje per-voice state s výchozími hodnotami
     */
    Envelope();

    /**
     * @brief RT-SAFE: Nastaví MIDI hodnotu pro attack obálku
     * 
     * @param midi_value MIDI hodnota (0-127)
     */
    void setAttackMIDI(uint8_t midi_value) noexcept;

    /**
     * @brief RT-SAFE: Nastaví MIDI hodnotu pro release obálku
     * 
     * @param midi_value MIDI hodnota (0-127)
     */
    void setReleaseMIDI(uint8_t midi_value) noexcept;

    /**
     * @brief RT-SAFE: Nastaví sustain úroveň na základě MIDI hodnoty
     * 
     * @param midi_value MIDI hodnota (0-127) → lineárně mapováno na (0.0f-1.0f)
     */
    void setSustainLevelMIDI(uint8_t midi_value) noexcept;

    /**
     * @brief RT-SAFE: Vrátí aktuální sustain úroveň
     * 
     * @return float Sustain úroveň (0.0f-1.0f)
     */
    float getSustainLevel() const noexcept;

    /**
     * @brief RT-SAFE: Získá hodnoty attack obálky (deleguje na static data)
     * 
     * @param gain_buffer Ukazatel na buffer pro výstupní hodnoty
     * @param num_samples Počet požadovaných vzorků
     * @param envelope_attack_position Pozice v obálce (offset)
     * @param sample_rate Vzorkovací frekvence
     * @return true pokud obálka pokračuje, false při dosažení konce
     */
    bool getAttackGains(float* gain_buffer, int num_samples, 
                       int envelope_attack_position, int sample_rate) const noexcept;

    /**
     * @brief RT-SAFE: Získá hodnoty release obálky (deleguje na static data)
     * 
     * @param gain_buffer Ukazatel na buffer pro výstupní hodnoty
     * @param num_samples Počet požadovaných vzorků
     * @param envelope_release_position Pozice v obálce (offset)
     * @param sample_rate Vzorkovací frekvence
     * @return true pokud obálka pokračuje, false při dosažení konce
     */
    bool getReleaseGains(float* gain_buffer, int num_samples, 
                        int envelope_release_position, int sample_rate) const noexcept;

    /**
     * @brief RT-SAFE: Vrátí délku attack obálky v milisekundách (deleguje na static data)
     * 
     * @param sample_rate Vzorkovací frekvence
     * @return float Délka v ms na základě aktuální MIDI hodnoty
     */
    float getAttackLength(int sample_rate) const noexcept;

    /**
     * @brief RT-SAFE: Vrátí délku release obálky v milisekundách (deleguje na static data)
     * 
     * @param sample_rate Vzorkovací frekvence
     * @return float Délka v ms na základě aktuální MIDI hodnoty
     */
    float getReleaseLength(int sample_rate) const noexcept;

private:
    // Per-voice state (minimální paměťová spotřeba)
    uint8_t attack_midi_index_;      // Aktuální MIDI index pro attack (0-127)
    uint8_t release_midi_index_;     // Aktuální MIDI index pro release (0-127)
    float sustain_level_;            // Sustain úroveň (0.0f-1.0f)
};