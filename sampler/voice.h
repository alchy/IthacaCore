#ifndef VOICE_H
#define VOICE_H

#include <cstdint>          // Pro uint8_t
#include <chrono>           // Pro časové výpočty (std::chrono)
#include <algorithm>        // Pro std::min, std::max
#include <limits>           // Pro numerické limity
#include "instrument_loader.h"  // Pro Instrument a Logger
#include "core_logger.h"    // Pro Logger

// Enum pro stavy voice (idle, attacking, sustaining, releasing)
enum class VoiceState {
    idle,       // Voice neaktivní
    attacking,  // Attack fáze (obálka stoupá)
    sustaining, // Sustain fáze (drží)
    releasing   // Release fáze (obálka klesá)
};

// Jednoduchá simulace JUCE AudioBuffer pro stereo výstup (bez JUCE závislosti)
struct AudioBuffer {
    float* leftChannel;     // Pointer na levý kanál (numSamples float hodnot)
    float* rightChannel;    // Pointer na pravý kanál (numSamples float hodnot)
    int numSamples;         // Počet samples v bufferu

    // Konstruktor pro alokaci (příklad použití: AudioBuffer buf(numSamples);)
    AudioBuffer(int samples) : numSamples(samples), leftChannel(new float[samples]), rightChannel(new float[samples]) {
        // Inicializace nulami (volitelně)
        std::fill(leftChannel, leftChannel + samples, 0.0f);
        std::fill(rightChannel, rightChannel + samples, 0.0f);
    }

    // Destruktor pro uvolnění paměti
    ~AudioBuffer() {
        delete[] leftChannel;
        delete[] rightChannel;
    }

    // Zakázané kopírování (pro jednoduchost)
    AudioBuffer(const AudioBuffer&) = delete;
    AudioBuffer& operator=(const AudioBuffer&) = delete;
};

// Jednoduchá struktura pro audio data jednoho sample (stereo)
struct AudioData {
    float left;   // Levý kanál
    float right;  // Pravý kanál
};

/**
 * @class Voice
 * @brief Jedna hlasová jednotka (voice) pro sampler engine.
 * 
 * Spravuje stav MIDI noty: start/stop, obálku (envelope) a čtení z sample bufferu.
 * Používá stereo interleaved float buffery z InstrumentLoader.
 * Thread-safe: Žádný sdílený stav mezi voices – každý Voice je nezávislý.
 * 
 * Klíčové vlastnosti:
 * - Mapování velocity 0-127 na vrstvy 0-7.
 * - Jednoduchá ADSR obálka (zde jen attack/release, bez full ADSR pro jednoduchost).
 * - Pozice v sample: `currentPosition_` v framech (stereo párech).
 * - Gate: `isGateOn_` pro detekci note-on/off.
 * 
 * PŘEDPOKLAD: InstrumentLoader poskytuje stereo buffery [L,R,L,R...].
 * 
 * Sdílené (statické) členy:
 * - targetSampleRate: Sdílená frekvence (např. 44100 Hz).
 * - sharedLogger: Sdílený logger pro logování (nyní public pro přístup z VoiceManager).
 * 
 * Opravy: Přidán getter pro midiNote_ (pro bezpečný přístup zvenčí). sharedLogger_ udělán public.
 */
class Voice {
public:
    /**
     * @brief Default konstruktor: Inicializuje na idle stav.
     * Používá se pro VoiceManager před inicializací.
     */
    Voice();

    /**
     * @brief Konstruktor s 2 argumenty (pro VoiceManager: midiNote, logger).
     * Instrument se nastaví později přes initialize.
     * @param midiNote MIDI nota (0-127) pro tuto voice.
     * @param logger Reference na sdílený Logger.
     */
    Voice(uint8_t midiNote, Logger& logger);

    /**
     * @brief Plný konstruktor.
     * Inicializuje stav na idle, pozici na 0, velocity na 0.
     * @param midiNote MIDI nota (0-127) pro tuto voice.
     * @param instrument Reference na načtený Instrument z InstrumentLoader.
     * @param logger Reference na sdílený Logger.
     */
    Voice(uint8_t midiNote, const Instrument& instrument, Logger& logger);

    /**
     * @brief Destruktor: Zaloguje ukončení voice.
     */
    ~Voice();

    /**
     * @brief Inicializuje voice s instrumentem (pro VoiceManager).
     * @param instrument Reference na Instrument.
     */
    void initialize(const Instrument& instrument);

    /**
     * @brief Vyčistí voice (reset na idle).
     */
    void cleanup();

    /**
     * @brief Reinicializuje voice s novým instrumentem.
     * @param instrument Reference na nový Instrument.
     */
    void reinitialize(const Instrument& instrument);

    /**
     * @brief Nastaví stav noty (start/stop).
     * @param isOn true pro start, false pro stop.
     * @param velocity MIDI velocity (0-127) pro start.
     */
    void setNoteState(bool isOn, uint8_t velocity = 0);

    /**
     * @brief Posune pozici v sample (advance pro jeden frame).
     */
    void advancePosition();

    /**
     * @brief Získává aktuální audio data (stereo sample na aktuální pozici).
     * @return AudioData struktura (left, right) s aplikovanou obálkou.
     */
    AudioData getCurrentAudioData() const;

    /**
     * @brief Spustí notu (note-on event).
     * Nastaví gate on, vybere velocity layer, resetuje pozici a stav na attacking.
     * @param velocity MIDI velocity (0-127).
     * Non-const: Mění stav (isGateOn_, currentState_, activeVelocityLayer_).
     */
    void startNote(uint8_t velocity);

    /**
     * @brief Zastaví notu (note-off event).
     * Nastaví čas note-off pro release fázi.
     * Non-const: Mění stav (isGateOn_, currentState_, noteOffTime_).
     */
    void stopNote();

    /**
     * @brief Hlavní processing metoda (jako JUCE processBlock).
     * Čte z sample bufferu podle aktuální pozice, aplikuje obálku (gain), zapisuje do výstupního bufferu.
     * Pokud je idle nebo end of sample, vrací false (deaktivovat voice).
     * @param outputBuffer Výstupní stereo buffer (simulace JUCE: AudioBuffer, 2 kanály).
     * @param numSamples Počet samples k zpracování v tomto bloku.
     * @return true, pokud voice stále aktivní; false pro deaktivaci (end of sample nebo released).
     * Non-const: Mění stav (currentState_, currentPosition_, isGateOn_).
     * Používá std::min pro omezení pozice – přetypováno na sf_count_t pro kompatibilitu.
     */
    bool processBlock(AudioBuffer& outputBuffer, int numSamples);

    /**
     * @brief Získává aktuální gain obálky (time-based).
     * @return Gain faktor (0.0f - 1.0f) pro multiplikaci sample dat.
     * Const: Pouze čte stav, ne mění.
     */
    float getEnvelopeGain() const;

    /**
     * @brief Kontroluje, zda je voice aktivní.
     * @return true, pokud isGateOn_ nebo v release fázi.
     * Const: Bezpečná pro query.
     */
    bool isActive() const { return isGateOn_ || (currentState_ == VoiceState::releasing); }

    /**
     * @brief Getter pro MIDI notu (pro přístup z VoiceManager).
     * @return MIDI nota (0-127).
     */
    uint8_t getMidiNote() const { return midiNote_; }

    // Sdílené (statické) členy – nyní public pro přístup z VoiceManager
    static int targetSampleRate_;                   // Sdílená target sample rate
    static Logger* sharedLogger_;                   // Sdílený logger (public pro jednoduchý přístup)

private:
    // Instance-specific
    uint8_t midiNote_;                          // Tato MIDI nota (0-127)
    bool isGateOn_ = false;                     // Gate stav (non-const, mění se v process)
    uint8_t activeVelocityLayer_ = 0;           // Aktivní velocity vrstva (0-7)
    VoiceState currentState_ = VoiceState::idle; // Současný stav (non-const)
    sf_count_t currentPosition_ = 0;            // Aktuální pozice v framech (stereo)

    // Obálka parametry (sdílené, konstanty)
    static constexpr float RELEASE_TIME_MS = 200.0f; // Doba release v ms
    std::chrono::steady_clock::time_point noteOffTime_; // Čas note-off pro release

    // Reference na instrument (načtené buffery) – nastavuje se v initialize
    const Instrument* instrument_ = nullptr;    // Pointer na stereo buffery (pro flexibilitu)

    // Pomocné metody (interní)
    uint8_t mapVelocityToLayer(uint8_t midiVelocity) const;  // Mapování 0-127 → 0-7
};

#endif // VOICE_H