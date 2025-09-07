#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include "voice.h"          // Pro Voice
#include "instrument_loader.h"  // Pro Instrument
#include "core_logger.h"    // Pro Logger
#include <vector>           // Pro pool voice

/**
 * @class VoiceManager
 * @brief Spravuje pool voice pro polyfonní přehrávání.
 * 
 * Inicializuje fixed pool (např. 128 voice), přiřazuje MIDI noty k volným voices.
 * Podporuje note-on/off a processBlock pro celý systém (mixing do výstupního bufferu).
 * 
 * DŮLEŽITÉ: Voice pool je fixed-size (max polyfonie). Při plném poolu ignoruje nové noty (round-robin nebo steal).
 * Mixing: Additive – všechny aktivní voices přidávají do výstupního bufferu.
 * Logger: Není uložen jako člen – předává se jako parametr do metod (dependency injection).
 */
class VoiceManager {
public:
    /**
     * @brief Konstruktor: Vytvoří pool o velikosti maxVoices.
     * @param maxVoices Maximální polyfonie (např. 128).
     * Logger se nepředává zde – předává se do metod.
     */
    explicit VoiceManager(int maxVoices) : maxVoices_(maxVoices) {
        voices_.reserve(maxVoices_);
        for (int i = 0; i < maxVoices_; ++i) {
            voices_.emplace_back(static_cast<uint8_t>(i % 128));  // Inicializace s MIDI notou
        }
        // Žádné logování zde – předá se do metod
    }

    /**
     * @brief Inicializuje všechny voices s instrumenty z loaderu.
     * @param loader Reference na InstrumentLoader.
     * @param logger Reference na logger pro logování této operace.
     * Pro každou voice přiřadí instrument pro její MIDI notu.
     */
    void initializeAll(const InstrumentLoader& loader, Logger& logger);

    /**
     * @brief Nastaví stav noty pro danou MIDI notu.
     * @param midiNote MIDI nota (0-127).
     * @param isOn True pro note-on, false pro note-off.
     * @param velocity Velocity (0-127).
     * @param logger Reference na logger pro logování této operace.
     * Najde voice pro tuto notu, zavolá setNoteState.
     */
    void setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity, Logger& logger);

    /**
     * @brief Zpracuje audio blok pro všechny voices (polyfonní mixing).
     * @param outputBuffer Výstupní stereo buffer.
     * @param numSamples Počet samples v bloku.
     * @param logger Reference na logger pro logování této operace.
     * @return True, pokud alespoň jedna voice je aktivní.
     * Prochází všechny voices, volá processBlock a mixuje do outputu.
     */
    bool processBlock(AudioBuffer& outputBuffer, int numSamples, Logger& logger);

private:
    // Pomocná: Najde voice pro MIDI notu (jednoduchá – vrátí první volnou)
    Voice* findVoiceForNote(uint8_t midiNote);

    // Pomocná: Počítá aktivní voices
    int countActiveVoices() const;

    std::vector<Voice> voices_;     // Pool voice
    int maxVoices_;                 // Maximální velikost poolu
    // Žádný logger_ člen – předává se jako parametr do metod
};

#endif // VOICE_MANAGER_H