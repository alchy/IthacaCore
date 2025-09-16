#ifndef VOICE_H
#define VOICE_H

#include "IthacaConfig.h"  // PŘIDÁNO - nahrazuje duplicitní definice

#include <cstdint>
#include <atomic>
#include <cstring>
#include <vector>
#include "instrument_loader.h"
#include "core_logger.h"
#include "envelopes/envelope.h"

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
 * Logger se předává jako reference pouze do non-RT metod.
 * ProcessBlock je 100% RT-safe bez loggingu a alokací.
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
     * @param envelope Reference na Envelope.
     * @param logger Reference na Logger.
     */
    void initialize(const Instrument& instrument, int sampleRate, const Envelope& envelope, Logger& logger);

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
     * @param envelope Reference na Envelope.
     * @param logger Reference na Logger.
     */
    void reinitialize(const Instrument& instrument, int sampleRate, const Envelope& envelope, Logger& logger);
    
    /**
     * @brief NON-RT SAFE: Updates internal buffer size for DAW block size changes
     * Must be called from audio thread during buffer size changes, NOT during processing
     * @param maxBlockSize Maximum expected block size from DAW
     */
    void prepareToPlay(int maxBlockSize) noexcept;

    /**
     * @brief Nastaví stav note s SPRÁVNOU aplikací velocity.
     * RT-SAFE: Žádné alokace, pouze state changes + velocity gain update.
     * Existující dvě metody, jedna je s MIDI velocity a druhá ji nepožaduje.
     * V případě volání NOTE OFF se použije druhá metoda bez velocity parametru.
     * @param isOn True pro start, false pro stop.
     * @param velocity Velocity (0-127) - NYNÍ SPRÁVNĚ ovlivňuje hlasitost!
     */
    void setNoteState(bool isOn, uint8_t velocity) noexcept;
    void setNoteState(bool isOn) noexcept;

    /**
     * @brief Získá aktuální stereo audio data s aplikovanou kompletní gain chain.
     * RT-SAFE: Inline gain application s envelope * velocity * master.
     * @param data Reference na AudioData pro výstup.
     * @return True, pokud data jsou platná.
     */
    bool getCurrentAudioData(AudioData& data) const noexcept;

    /**
     * @brief RT-SAFE: Pre-calculates envelope gains for entire block
     * 
     * This method separates gain calculation from audio processing for better
     * modularity and testability. It handles all envelope states and returns
     * false when voice should be deactivated.
     * 
     * @param gainBuffer Pre-allocated buffer for per-sample gains (caller owns)
     * @param numSamples Number of samples to calculate gains for
     * @return true if voice remains active, false if should be deactivated
     */
    bool calculateBlockGains(float* gainBuffer, int numSamples) noexcept;

    /**
     * @brief HLAVNÍ RT-SAFE METODA: Zpracuje audio blok s OPRAVENOU centralizovanou aplikací gain.
     * 
     * OPRAVENO PRO KOMPLETNÍ GAIN CHAIN:
     * - Volá calculateBlockGains() pro výpočet envelope
     * - Aplikuje envelope_gain_ * velocity_gain_ 
     * - Řídí voice state transitions na základě výsledků gain calculation
     * - Provádí mixdown sčítání do sdílených output bufferů
     * 
     * @param outputLeft Pointer na levý kanál výstupního bufferu (sdílený, používá +=).
     * @param outputRight Pointer na pravý kanál výstupního bufferu (sdílený, používá +=).
     * @param numSamples Počet samples k zpracování.
     * @return True, pokud voice je stále aktivní.
     */
    bool processBlock(float* outputLeft, float* outputRight, int samplesPerBlock) noexcept;

    // RT-SAFE Gettery (bez loggeru - jen čtení hodnot)
    uint8_t getMidiNote() const noexcept { return midiNote_; }
    bool isActive() const noexcept { return state_ != VoiceState::Idle; }
    VoiceState getState() const noexcept { return state_; }
    int getPosition() const noexcept { return position_; }
    uint8_t getCurrentVelocityLayer() const noexcept { return currentVelocityLayer_; }
    
    // gain gettery
    float getCurrentEnvelopeGain() const noexcept { return envelope_gain_; }
    float getVelocityGain() const noexcept { return velocity_gain_; }
    float getMasterGain() const noexcept { return master_gain_; }

    // RT-Safe mode control (pro debugging/profiling)
    static void setRealTimeMode(bool enabled) noexcept { rtMode_.store(enabled); }
    static bool isRealTimeMode() noexcept { return rtMode_.load(); }

    // Metody pro runtime kontrolu gain

    /**
     * @brief NON-RT SAFE: Nastaví master gain pro voice (0.0-1.0)
     * @param gain Master gain (0.0-1.0, default 0.8)
     * @param logger Reference na Logger
     */
    void setMasterGain(float gain, Logger& logger);

    /**
     * @brief RT-SAFE: Nastaví master gain pro voice bez loggingu
     * @param gain Master gain (0.0-1.0)
     */
    void setMasterGainRTSafe(float gain) noexcept;

    /**
     * @brief RT-SAFE: Nastaví pan pro voice (L -1.0 <0> +1.0 R)
     * @param pan default 0
     */
    void setPan(float pan) noexcept;

    /**
     * @brief NON-RT SAFE: Získá debug informace o gain structure
     * @param logger Reference na Logger
     * @return String s detailními gain informacemi
     */
    std::string getGainDebugInfo(Logger& logger) const;

private:
    uint8_t             midiNote_;              // MIDI nota (0-127)
    const Instrument*   instrument_;            // Pointer na Instrument (nevolnit, patří Loader)
    int                 sampleRate_;            // Frekvence vzorkování pro obálku (Hz)
    VoiceState          state_;                 // Aktuální stav
    int                 position_;              // Pozice v samples (frames)
    uint8_t             currentVelocityLayer_;  // Aktuální velocity layer (0-7)
    
    float               master_gain_;           // Master volume kontrola (default 0.8f)
    float               velocity_gain_;         // MIDI velocity gain (0-127 → 0.0-1.0)
    float               envelope_gain_;         // Dynamická obálka (attack/sustain/release): 0.0-1.0
    float               pan_;                   // PAN left right (-1.0f až +1.0f) 0 je střed

    const Envelope*     envelope_;              // Pointer na Envelope (non-owning)

    int         envelope_attack_position_;      // Pozice v attack envelope
    int         envelope_release_position_;     // Pozice v release envelope

    float       release_start_gain_;            // Hodnota gain pri svem zacatku (nastavuje se v note on a pak ji nastavuje attack vzdy v miste, kde se meni na release / note-off)
    
    // RT-SAFE: Pre-allocated buffer pro gain calculations
    // Resized pouze během prepareToPlay() calls, nikdy během RT processing
    mutable std::vector<float> gainBuffer_;

    // RT-safe mode flag (shared across all instances)
    static std::atomic<bool> rtMode_;

    // RT-SAFE PRIVATE METODY:

    /**
     * @brief RT-SAFE: NOVÁ metoda pro aplikaci MIDI velocity na hlasitost
     * Mapuje velocity 0-127 na velocity_gain_ 0.0-1.0 s logaritmickou křivkou.
     * @param velocity MIDI velocity (0-127)
     */
    void updateVelocityGain(uint8_t velocity) noexcept;

    /**
     * @brief NON-RT: Safe logging wrapper pro non-critical operations.
     * Používá se pouze v initialize/cleanup metodách.
     */
    void logSafe(const std::string& component, const std::string& severity, 
                const std::string& message, Logger& logger) const;
};

#endif // VOICE_H