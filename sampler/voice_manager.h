#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include "voice.h"
#include "instrument_loader.h"  // Pro InstrumentLoader a getInstrumentNote
#include <vector>               // Pro std::vector<Voice>

/**
 * @class VoiceManager
 * @brief Správce fixního poolu 128 voice pro polyfonní přehrávání (jeden hlas na MIDI notu 0-127).
 * 
 * Každý hlas je pevně vázán na MIDI notu (index = midiNote). Žádný dynamický přiřazování nebo stealing –
 * přímý přístup pro setNoteState a mixdown všech aktivních hlasů v processBlock.
 * 
 * Klíčové vlastnosti:
 * - Fixní velikost: 128 hlasů (MIDI_NOTE_MAX + 1).
 * - setNoteState: Přímý přístup podle midiNote (O(1)).
 * - processBlock: Prochází všechny 128 hlasů, součítá výstupy aktivních s obálkou.
 * - Žádná krádež hlasů: Limit je 128 současných not.
 * 
 * PŘÍKLAD POUŽITÍ:
 * VoiceManager manager(44100, logger);  // Automaticky 128 hlasů
 * manager.initializeAll(loader);
 * manager.setNoteState(60, true, 100);  // Note-on pro C4
 * manager.processBlock(output, 512);    // Mixdown do bufferu
 */
class VoiceManager {
public:
    /**
     * @brief Konstruktor: Vytvoří fixní pool 128 voice, uloží sampleRate.
     * Každý hlas má midiNote_ = index (0-127).
     * @param sampleRate Frekvence vzorkování (Hz) - propagováno do Voice.
     * @param logger Reference na Logger.
     */
    VoiceManager(int sampleRate, Logger& logger);

    /**
     * @brief Inicializuje všechny 128 voices s instrumenty z Loaderu.
     * Pro každou voice (index = midiNote): Nastaví instrument z loader.getInstrumentNote(index).
     * @param loader Reference na InstrumentLoader pro instrumenty.
     */
    void initializeAll(InstrumentLoader& loader);

    /**
     * @brief Nastaví stav note pro danou MIDI notu (přímý přístup k voice[midiNote]).
     * @param midiNote MIDI nota (0-127) - slouží jako index.
     * @param isOn True pro start (note-on), false pro stop (note-off).
     * @param velocity Velocity (0-127, mapováno na layer 0-7).
     */
    void setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity = 0);

    /**
     * @brief Procesuje audio blok: Prochází všechny 128 hlasů, součítá výstupy aktivních do bufferu.
     * Placeholder pro mixdown (součet left/right s obálkou).
     * @param outputBuffer Simulovaný buffer pro výstup (stereo, placeholder).
     * @param numSamples Počet samples v bloku.
     * @return True, pokud je alespoň jedna aktivní voice.
     */
    bool processBlock(/* AudioBuffer& outputBuffer, int numSamples */);

    // Gettery
    int getMaxVoices() const { return 128; }  // Fixní
    int getActiveVoicesCount() const;

private:
    std::vector<Voice> voices_;     // Fixní pole 128 voice (index = midiNote)
    Logger& logger_;                // Reference na Logger
    int sampleRate_;                // Uložený sample rate pro propagaci do Voice
};

#endif // VOICE_MANAGER_H