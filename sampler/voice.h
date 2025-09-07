#ifndef VOICE_H
#define VOICE_H

#include <cstdint>      // Pro uint8_t
#include "instrument_loader.h"  // Pro Instrument
#include "core_logger.h"        // Pro Logger
#include <chrono>               // Pro měření času obálky (attack/release)

// Simulace JUCE AudioBuffer pro stereo výstup (bez závislosti na JUCE)
struct AudioBuffer {
    float* leftChannel;   // Pointer na levý kanál [L0, L1, L2...]
    float* rightChannel;  // Pointer na pravý kanál [R0, R1, R2...]
    int numSamples;       // Počet samples v bufferu

    // Konstruktor pro alokaci (příklad: stereo buffer o velikosti numSamples)
    AudioBuffer(int numSamples) : numSamples(numSamples) {
        leftChannel = new float[numSamples]();  // Inicializace na 0
        rightChannel = new float[numSamples]();
    }

    // Destruktor pro uvolnění paměti
    ~AudioBuffer() {
        delete[] leftChannel;
        delete[] rightChannel;
    }

    // Zakázané kopírování (RAII)
    AudioBuffer(const AudioBuffer&) = delete;
    AudioBuffer& operator=(const AudioBuffer&) = delete;
};

// Simulace JUCE AudioData pro jeden stereo sample (float left, float right)
struct AudioData {
    float left;
    float right;

    AudioData() : left(0.0f), right(0.0f) {}
    AudioData(float l, float r) : left(l), right(r) {}
};

/**
 * @enum VoiceState
 * @brief Stav hlasové jednotky (voice) pro řízení lifecycle.
 */
enum class VoiceState {
    idle,      // Neaktivní, čeká na note-on
    attacking, // Attack fáze obálky (gain stoupá)
    sustaining,// Sustain (plný gain, přehrává se)
    releasing  // Release fáze (gain klesá po note-off)
};

/**
 * @class Voice
 * @brief Reprezentuje jednu hlasovou jednotku (voice) pro přehrávání sample s obálkou.
 * 
 * Spravuje stav, pozici v bufferu, obálku (jednoduchá time-based gain) a výstup do AudioBuffer.
 * Inicializuje se s MIDI notou a instrumentem. Podporuje polyfonii přes VoiceManager.
 * 
 * DŮLEŽITÉ: Vstupní data z Instrument jsou stereo interleaved [L,R,L,R...].
 * Obálka: Attack (okamžitý), Sustain (plný), Release (200 ms lineární klesání).
 * Thread-safety: Neimplementováno (single-threaded design).
 * Logger: Není uložen jako člen – předává se jako parametr do metod (dependency injection).
 */
class Voice {
public:
    static constexpr int RELEASE_TIME_MS = 200;  // Konstanta pro délku release (ms)
    static int targetSampleRate_;                // Statický target sample rate (inicializován v .cpp)

    /**
     * @brief Default konstruktor pro pool v VoiceManager.
     * Inicializuje idle stav, MIDI note na 0, instrument na nullptr.
     */
    Voice() : midiNote_(0), instrument_(nullptr), currentState_(VoiceState::idle), currentPosition_(0), 
              isGateOn_(false), activeVelocityLayer_(0), noteOffTime_(std::chrono::steady_clock::now()) {
        // Prázdný – čeká na initialize
    }

    /**
     * @brief Konstruktor pro VoiceManager: Nastaví MIDI notu.
     * @param midiNote MIDI nota (0-127).
     * Logger se nepředává zde – předává se do metod.
     */
    explicit Voice(uint8_t midiNote) : midiNote_(midiNote), instrument_(nullptr), currentState_(VoiceState::idle), 
                                       currentPosition_(0), isGateOn_(false), activeVelocityLayer_(0), 
                                       noteOffTime_(std::chrono::steady_clock::now()) {
        // Inicializace s midiNote
    }

    /**
     * @brief Inicializuje voice s instrumentem (pro pool v VoiceManager).
     * @param instrument Reference na Instrument.
     * @param logger Reference na logger pro logování této operace.
     * Resetuje pozici a gain na 0.
     */
    void initialize(const Instrument& instrument, Logger& logger);

    /**
     * @brief Reinicializuje s novým instrumentem (pro dynamickou změnu).
     * @param instrument Nový Instrument.
     * @param logger Reference na logger pro logování této operace.
     */
    void reinitialize(const Instrument& instrument, Logger& logger);

    /**
     * @brief Resetuje voice na idle stav (pro vrácení do poolu).
     * @param logger Reference na logger pro logování této operace.
     */
    void cleanup(Logger& logger);

    /**
     * @brief Nastaví stav noty (note-on/note-off).
     * @param isOn True pro start (note-on), false pro stop (note-off).
     * @param velocity Velocity (0-127) pro výběr vrstvy.
     * @param logger Reference na logger pro logování této operace.
     */
    void setNoteState(bool isOn, uint8_t velocity, Logger& logger);

    /**
     * @brief Posune pozici v sample o 1 frame (pro non-real-time).
     * Žádný log – čistá metoda.
     */
    void advancePosition();

    /**
     * @brief Získává aktuální stereo audio data na pozici.
     * @return AudioData s aplikovaným gainem.
     * Žádný log – const metoda.
     */
    AudioData getCurrentAudioData() const;

    /**
     * @brief Zpracuje audio blok (simulace JUCE processBlock).
     * @param outputBuffer Reference na výstupní AudioBuffer (stereo).
     * @param numSamples Počet samples k zpracování.
     * @param logger Reference na logger pro logování této operace.
     * @return True, pokud voice je aktivní.
     */
    bool processBlock(AudioBuffer& outputBuffer, int numSamples, Logger& logger);

    /**
     * @brief Getter pro MIDI notu.
     * @return MIDI nota (0-127).
     */
    uint8_t getMidiNote() const { return midiNote_; }

    /**
     * @brief Kontroluje, zda je voice aktivní.
     * @return True, pokud gate on nebo releasing.
     */
    bool isActive() const {
        return currentState_ != VoiceState::idle;
    }

private:
    uint8_t midiNote_;                          // MIDI nota (0-127)
    const Instrument* instrument_;              // Pointer na instrument buffery (nullptr pokud není nastaveno)
    VoiceState currentState_;                   // Aktuální stav
    sf_count_t currentPosition_;                // Pozice v sample bufferu (frames)
    bool isGateOn_;                             // Flag pro gate (note on/off)
    uint8_t activeVelocityLayer_;               // Aktivní velocity vrstva (0-7)
    std::chrono::steady_clock::time_point noteOffTime_;  // Čas note-off pro release

    // Pomocné metody (privátní)
    void startNote(uint8_t velocity, Logger& logger);  // Note-on logika
    void stopNote(Logger& logger);                     // Note-off logika
    float getEnvelopeGain() const;                     // Výpočet gainu z obálky
    uint8_t mapVelocityToLayer(uint8_t midiVelocity) const;  // Mapování velocity na layer (0-7)
};

#endif // VOICE_H