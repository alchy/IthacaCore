#ifndef VOICE_H
#define VOICE_H

#include <cstdint>      // Pro uint8_t
#include <atomic>       // Pro RT-safe logging flag
#include <cstring>      // Pro memset
#include "instrument_loader.h"  // Pro Instrument a Logger
#include "core_logger.h"        // Pro Logger

// Simulace JUCE AudioBuffer pro stereo výstup (bez závislosti na JUCE)
struct AudioData {
    float left;
    float right;
    AudioData() : left(0.0f), right(0.0f) {}
    AudioData(float l, float r) : left(l), right(r) {}
};

/**
 * @enum VoiceState
 * @brief Stavy voice pro řízení lifecycle (idle, attacking, sustaining, releasing).
 * Používá se pro aplikaci obálky (gain) a rozhodnutí o ukončení voice.
 */
enum class VoiceState {
    Idle = 0,       
    Attacking = 1,    
    Sustaining = 2, 
    Releasing = 3   
};

/**
 * @class Voice
 * @brief Jedna hlasová jednotka pro přehrávání sample s obálkou a stavy.
 * 
 * REFAKTOROVÁNO PRO RT-SAFETY:
 * - Eliminovány redundantní memory access patterns
 * - Batch processing místo sample-by-sample
 * - RT-safe logging s compile-time/runtime flags
 * - Optimalizované envelope processing
 * - Cache-friendly memory access
 * - Žádné memory allocations v processBlock
 * 
 * Logger se předává jako reference pouze do non-RT metod.
 * ProcessBlock je 100% RT-safe bez loggingu a allocací.
 */
class Voice {
public:
    /**
     * @brief Default konstruktor pro pool v VoiceManager.
     */
    Voice();

    /**
     * @brief Konstruktor pro VoiceManager: Nastaví MIDI notu.
     * @param midiNote MIDI nota (0-127).
     */
    Voice(uint8_t midiNote);

    /**
     * @brief Inicializuje voice s instrumentem a sample rate.
     * NON-RT SAFE: Může logovat inicializaci.
     * @param instrument Reference na Instrument.
     * @param sampleRate Frekvence vzorkování (např. 44100 Hz).
     * @param logger Reference na Logger.
     */
    void initialize(const Instrument& instrument, int sampleRate, Logger& logger);

    /**
     * @brief Cleanup: Reset na idle stav.
     * NON-RT SAFE: Může logovat cleanup.
     * @param logger Reference na Logger.
     */
    void cleanup(Logger& logger);

    /**
     * @brief Reinicializuje s novým instrumentem a sample rate.
     * NON-RT SAFE: Může logovat reinicializaci.
     * @param instrument Nový Instrument.
     * @param sampleRate Nový sample rate.
     * @param logger Reference na Logger.
     */
    void reinitialize(const Instrument& instrument, int sampleRate, Logger& logger);

    /**
     * @brief Nastaví stav note: true = startNote, false = stopNote.
     * RT-SAFE: Žádné alokace, pouze state changes.
     * @param isOn True pro start, false pro stop.
     * @param velocity Velocity (0-127, mapováno na layer 0-7).
     */
    void setNoteState(bool isOn, uint8_t velocity) noexcept;

    /**
     * @brief Posune pozici ve sample o 1 frame.
     * RT-SAFE: Optimalizováno pro single-sample advance bez loggingu.
     */
    void advancePosition() noexcept;

    /**
     * @brief Získá aktuální stereo audio data s aplikovanou obálkou.
     * RT-SAFE: Inline gain application, žádné function call overhead.
     * @param data Reference na AudioData pro výstup.
     * @return True, pokud data jsou platná.
     */
    bool getCurrentAudioData(AudioData& data) const noexcept;

    /**
     * @brief HLAVNÍ RT-SAFE METODA: Zpracuje audio blok s explicitními stereo buffery.
     * 
     * OPTIMALIZOVÁNO PRO RT:
     * - Batch processing místo sample-by-sample loops
     * - Eliminovány redundantní kontroly v každé iteraci
     * - Cache-friendly memory access patterns
     * - Specialized processing paths pro sustain/release
     * - Žádné string operations ani alokace
     * - Žádné logging v RT contextu
     * 
     * @param outputLeft Pointer na levý kanál výstupního bufferu.
     * @param outputRight Pointer na pravý kanál výstupního bufferu.
     * @param numSamples Počet samples k zpracování.
     * @return True, pokud voice je stále aktivní.
     */
    bool processBlock(float* outputLeft, float* outputRight, int numSamples) noexcept;

    /**
     * @brief RT-SAFE: Zpracuje audio blok s AudioData strukturou.
     * Alternativní interface pro compatibility.
     * @param outputBuffer Pointer na pole AudioData struktur.
     * @param numSamples Počet samples k zpracování.
     * @return True, pokud voice je stále aktivní.
     */
    bool processBlock(AudioData* outputBuffer, int numSamples) noexcept;

    // RT-SAFE Gettery (bez loggeru - jen čtení hodnot)
    uint8_t getMidiNote() const noexcept { return midiNote_; }
    bool isActive() const noexcept { return state_ != VoiceState::Idle; }
    VoiceState getState() const noexcept { return state_; }
    sf_count_t getPosition() const noexcept { return position_; }
    float getCurrentGain() const noexcept { return gain_; }
    uint8_t getCurrentVelocityLayer() const noexcept { return currentVelocityLayer_; }

    // RT-Safe mode control (pro debugging/profiling)
    static void setRealTimeMode(bool enabled) noexcept { rtMode_.store(enabled); }
    static bool isRealTimeMode() noexcept { return rtMode_.load(); }

private:
    uint8_t midiNote_;              // MIDI nota (0-127)
    const Instrument* instrument_;  // Pointer na Instrument (nevolnit, patří Loader)
    int sampleRate_;                // Frekvence vzorkování pro obálku (Hz)
    VoiceState state_;              // Aktuální stav
    sf_count_t position_;           // Pozice ve sample (frames)
    uint8_t currentVelocityLayer_;  // Aktuální velocity layer (0-7)
    float gain_;                    // Aktuální gain obálky (0.0-1.0)
    sf_count_t releaseStartPosition_; // Pozice startu release pro lineární útlum
    sf_count_t releaseSamples_;     // Délka release v samplech (např. 0.2 * sampleRate)

    // RT-safe mode flag (shared across all instances)
    static std::atomic<bool> rtMode_;

    // OPTIMALIZOVANÉ PRIVATE METODY PRO RT-PROCESSING:

    /**
     * @brief RT-SAFE: Vypočítá releaseSamples na základě sampleRate_ (200 ms release).
     */
    void calculateReleaseSamples() noexcept;

    /**
     * @brief RT-SAFE: Update release gain pro single sample advance.
     */
    void updateReleaseGain() noexcept;

    /**
     * @brief RT-SAFE: Specialized processing pro konstantní gain (sustain).
     * Cache-friendly, vectorizable loop.
     * @param stereoBuffer Source audio buffer (interleaved [L,R,L,R...])
     * @param outputLeft Left channel output buffer
     * @param outputRight Right channel output buffer  
     * @param numSamples Number of samples to process
     */
    void processConstantGain(const float* stereoBuffer, 
                           float* outputLeft, float* outputRight, 
                           int numSamples) noexcept;

    /**
     * @brief RT-SAFE: Specialized processing pro release s lineární fade.
     * Optimalizované gain interpolation.
     * @param stereoBuffer Source audio buffer
     * @param outputLeft Left channel output buffer
     * @param outputRight Right channel output buffer
     * @param numSamples Number of samples to process
     */
    void processRelease(const float* stereoBuffer, 
                       float* outputLeft, float* outputRight, 
                       int numSamples) noexcept;

    /**
     * @brief NON-RT: Safe logging wrapper pro non-critical operations.
     * Používá se pouze v initialize/cleanup metodách.
     */
    void logSafe(const std::string& component, const std::string& severity, 
                const std::string& message, Logger& logger) const;
};

#endif // VOICE_H