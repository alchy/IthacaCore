#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include "voice.h"
#include "envelopes/envelope.h"              // Refaktorovaná Envelope třída
#include "envelopes/envelope_static_data.h"  // Pro validace
#include "instrument_loader.h"
#include "sampler.h"
#include <vector>
#include <string>
#include <atomic>
#include <memory>

/**
 * @class VoiceManager
 * @brief Hlavní třída pro řízení polyfonního audio systému s REFAKTOROVANÝM envelope systémem
 * 
 * REFAKTOROVÁNO: Používá sdílená statická envelope data přes EnvelopeStaticData,
 * což dramaticky snižuje paměťovou spotřebu při více instancích VoiceManager.
 * 
 * DŮLEŽITÉ: EnvelopeStaticData musí být inicializována PŘED vytvořením VoiceManager!
 */
class VoiceManager {
public:
    /**
     * @brief Konstruktor s validací statických envelope dat
     * @param sampleDir Cesta k sample adresáři
     * @param logger Reference na Logger
     * 
     * PŘEDPOKLAD: EnvelopeStaticData::initialize() už bylo voláno!
     */
    VoiceManager(const std::string& sampleDir, Logger& logger);
    
    // ===== INITIALIZATION PIPELINE =====
    
    /**
     * @brief FÁZE 1: Jednorazová inicializace - skenování adresáře
     * @param logger Reference na Logger
     */
    void initializeSystem(Logger& logger);

    /**
     * @brief FÁZE 2: Načtení dat pro konkrétní sample rate
     * @param sampleRate Target sample rate (44100/48000)
     * @param logger Reference na Logger
     */
    void loadForSampleRate(int sampleRate, Logger& logger);

    /**
     * @brief Nastavení sample rate a trigger reinicializace
     * @param newSampleRate Nový sample rate (44100 nebo 48000)
     * @param logger Reference na Logger
     */
    void changeSampleRate(int newSampleRate, Logger& logger);
    
    /**
     * @brief Getter pro aktuální sample rate
     * @return Aktuální sample rate v Hz (0 = neinicializováno)
     */
    int getCurrentSampleRate() const noexcept { return currentSampleRate_; }

    /**
     * @brief FÁZE 3: Prepare all voices for new buffer size (JUCE integration)
     * @param maxBlockSize Maximum block size from DAW
     */
    void prepareToPlay(int maxBlockSize) noexcept;
    
    // ===== CORE AUDIO API =====

    /**
     * @brief Nastavení stavu MIDI noty (note-on/note-off)
     * @param midiNote MIDI nota (0-127)
     * @param isOn true = note-on, false = note-off
     * @param velocity MIDI velocity (0-127)
     */
    void setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity) noexcept;
    
    /**
     * @brief RT-SAFE: Process audio block - INTERLEAVED format
     * @param outputBuffer AudioData buffer pro stereo výstup
     * @param samplesPerBlock Počet samples k zpracování
     * @return true pokud jsou nějaké voices aktivní
     */
    bool processBlockInterleaved(AudioData* outputBuffer, int samplesPerBlock) noexcept;
    
    /**
     * @brief RT-SAFE: Process audio block - UNINTERLEAVED format (JUCE style)
     * @param outputLeft Left channel buffer (float array)
     * @param outputRight Right channel buffer (float array) 
     * @param samplesPerBlock Number of samples to process
     * @return true if any voices are still active
     */
    bool processBlockUninterleaved(float* outputLeft, float* outputRight, int samplesPerBlock) noexcept;
    
    /**
     * @brief Zastaví všechny aktivní voices (send note-off)
     */
    void stopAllVoices() noexcept;
    
    /**
     * @brief Reset všech voices na idle stav
     * @param logger Reference na Logger
     */
    void resetAllVoices(Logger& logger);

    // ===== STATISTICS AND MONITORING =====
    
    /**
     * @brief Getter pro maximální počet voices
     * @return Vždy 128 (fixní pool)
     */
    int getMaxVoices() const noexcept { return 128; }
    
    /**
     * @brief Getter pro počet aktivních voices
     * @return Počet aktivních voices
     */
    int getActiveVoicesCount() const noexcept { return activeVoicesCount_.load(); }
    
    /**
     * @brief Getter pro počet sustaining voices
     * @return Počet voices ve stavu sustaining
     */
    int getSustainingVoicesCount() const noexcept;
    
    /**
     * @brief Getter pro počet releasing voices
     * @return Počet voices ve stavu releasing
     */
    int getReleasingVoicesCount() const noexcept;

    /**
     * @brief Přístup k jednotlivé voice podle MIDI noty
     * @param midiNote MIDI nota (0-127)
     * @return Reference na Voice
     */
    Voice& getVoice(uint8_t midiNote) noexcept;
    
    /**
     * @brief Const přístup k jednotlivé voice podle MIDI noty
     * @param midiNote MIDI nota (0-127)
     * @return Const reference na Voice
     */
    const Voice& getVoice(uint8_t midiNote) const noexcept;

    /**
     * @brief Nastavení real-time mode (pro debugging/profiling)
     * @param enabled true = RT mode (no logging), false = normal mode
     */
    void setRealTimeMode(bool enabled) noexcept;
    
    /**
     * @brief Getter pro RT mode stav
     * @return true pokud je RT mode zapnutý
     */
    bool isRealTimeMode() const noexcept { return rtMode_.load(); }

    /**
     * @brief Loguje detailní statistiky celého systému
     * @param logger Reference na Logger
     */
    void logSystemStatistics(Logger& logger);

private:
    // ===== ENCAPSULATED COMPONENTS =====
    
    SamplerIO samplerIO_;               // Stack allocated SamplerIO instance
    InstrumentLoader instrumentLoader_; // Stack allocated InstrumentLoader instance
    Envelope envelope_;                 // Per-instance envelope state manager (refaktorováno)

    // ===== SAMPLE RATE MANAGEMENT =====
    
    int currentSampleRate_;            // Aktuální sample rate (0 = neinicializováno)
    std::string sampleDir_;            // Cesta k sample adresáři
    bool systemInitialized_;           // Flag pro initialization state
    

    // ===== VOICE MANAGEMENT =====
    
    std::vector<Voice> voices_;        // Fixní pool 128 voices
    std::vector<Voice*> activeVoices_; // Pointery na aktivní voices
    std::vector<Voice*> voicesToRemove_; // Cleanup buffer
    
    mutable std::atomic<int> activeVoicesCount_{0}; // Thread-safe counter
    std::atomic<bool> rtMode_{false};               // RT-safe mode flag

    // ===== PRIVATE HELPER METHODS =====
    
    /**
     * @brief Reinicializace pokud je potřeba změna sample rate
     * @param targetSampleRate Cílový sample rate
     * @param logger Reference na Logger
     */
    void reinitializeIfNeeded(int targetSampleRate, Logger& logger);
    
    /**
     * @brief Kontrola, zda je potřeba reinicializace
     * @param targetSampleRate Cílový sample rate
     * @return true pokud je potřeba reinicializace
     */
    bool needsReinitialization(int targetSampleRate) const noexcept;
    
    /**
     * @brief Inicializace všech voices s načtenými instrumenty
     * @param logger Reference na Logger
     */
    void initializeVoicesWithInstruments(Logger& logger);

    /**
     * @brief Přidá voice do active pool
     * @param voice Pointer na voice
     */
    void addActiveVoice(Voice* voice) noexcept;
    
    /**
     * @brief Odebere voice z active pool
     * @param voice Pointer na voice
     */
    void removeActiveVoice(Voice* voice) noexcept;
    
    /**
     * @brief Vyčistí neaktivní voices z pool
     */
    void cleanupInactiveVoices() noexcept;
    
    /**
     * @brief Safe logging wrapper pro non-RT operace
     * @param component Název komponenty
     * @param severity Závažnost
     * @param message Zpráva
     * @param logger Reference na Logger
     */
    void logSafe(const std::string& component, const std::string& severity, 
                const std::string& message, Logger& logger) const;
    
    /**
     * @brief Validace MIDI note rozsahu
     * @param midiNote MIDI nota k validaci
     * @return true pokud je platná (0-127)
     */
    bool isValidMidiNote(uint8_t midiNote) const noexcept {
        return midiNote <= 127;
    }
};

#endif // VOICE_MANAGER_H