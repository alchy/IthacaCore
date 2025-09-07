#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include <vector>           // Pro std::vector<Voice>
#include "voice.h"          // Pro Voice
#include "instrument_loader.h"  // Pro Instrument
#include "core_logger.h"    // Pro Logger

/**
 * @class VoiceManager
 * @brief Správce více Voice instancí (polyfonie).
 * 
 * Inicializuje pool voice (např. 128 pro polyfonii), přiřazuje noty k volným voices.
 * Používá se pro koordinaci v sampleru (např. v runSampler).
 * 
 * Opravy: Používá opravené Voice API (konstruktor s 2 arg, metody initialize atd.).
 */
class VoiceManager {
public:
    /**
     * @brief Konstruktor: Inicializuje pool voice (default 128).
     * @param maxVoices Maximální počet voice (polyfonie).
     * @param logger Reference na Logger.
     */
    VoiceManager(int maxVoices = 128, Logger& logger = *Voice::sharedLogger_);

    /**
     * @brief Destruktor: Vyčistí voices.
     */
    ~VoiceManager();

    /**
     * @brief Inicializuje všechny voices s instrumentem.
     * @param instrument Reference na InstrumentLoader.
     */
    void initializeAll(const InstrumentLoader& loader);

    /**
     * @brief Nastaví stav noty pro danou MIDI notu.
     * @param midiNote MIDI nota (0-127).
     * @param isOn true pro note-on, false pro note-off.
     * @param velocity Velocity (0-127).
     */
    void setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity = 0);

    /**
     * @brief Procesuje audio blok (pro všechny aktivní voices).
     * @param outputBuffer Výstupní buffer.
     * @param numSamples Počet samples.
     * @return true, pokud jsou aktivní voices.
     */
    bool processBlock(AudioBuffer& outputBuffer, int numSamples);

private:
    std::vector<Voice> voices_;  // Pool voice
    Logger* logger_;             // Reference na logger
};

#endif // VOICE_MANAGER_H