#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include "voice.h"
#include "envelopes/envelope.h"
#include "envelopes/envelope_static_data.h"
#include "instrument_loader.h"
#include "sampler.h"

#include <vector>
#include <string>
#include <atomic>
#include <memory>

/**
 * @class VoiceManager
 * @brief Hlavní třída pro řízení polyfonního audio systému s refaktorovanými sdílenými envelope daty
 * 
 * REFAKTOROVÁNO: Odstraněny všechny testovací metody pro čistou produkční implementaci.
 * Všechny testy jsou nyní v separátní třídě VoiceManagerTester.
 * 
 * NOVÝ SYSTÉM: Používá sdílená statická envelope data z EnvelopeStaticData,
 * což dramaticky snižuje paměťovou spotřebu při více instancích (úspora ~3.1 GB při 16 kanálech).
 * 
 * Klíčové vlastnosti:
 * - 128 simultánních voices (jeden pro každou MIDI notu)
 * - Dynamic sample rate management (44100/48000 Hz)
 * - Stack allocated komponenty (SamplerIO, InstrumentLoader)
 * - JUCE integration ready (prepareToPlay pattern)
 * - RT-safe processBlock s dynamic gain scaling
 * - Initialization pipeline pro modulární setup
 * - Sdílená envelope data mezi všemi instancemi
 */
class VoiceManager {
public:
    /**
     * @brief Konstruktor: Vytvoří VoiceManager s cestou k samples
     * @param sampleDir Cesta k sample adresáři
     * @param logger Reference na Logger
     * 
     * DŮLEŽITÉ: EnvelopeStaticData musí být inicializována PŘED vytvořením VoiceManager!
     * Volej EnvelopeStaticData::initialize(logger) v main() před vytvořením VoiceManagerů.
     */
    VoiceManager(const std::string& sampleDir, Logger& logger);
    
    // ===== INITIALIZATION PIPELINE START =====
    
    /**
     * @brief FÁZE 1: Jednorazová inicializace - skenování adresáře (BEZ envelope init)
     * @param logger Reference na Logger
     * 
     * POZNÁMKA: Envelope inicializace je nyní globální v EnvelopeStaticData::initialize()
     */
    void initializeSystem(Logger& logger);

    /**
     * @brief FÁZE 2: Načtení dat pro konkrétní sample rate
     * @param sampleRate Target sample rate (44100/48000)
     * @param logger Reference na Logger
     * 
     * POZNÁMKA: EnvelopeStaticData už obsahuje data pro oba sample rate!
     */
    void loadForSampleRate(int sampleRate, Logger& logger);

    // =====  SAMPLE RATE MANAGEMENT =====
    
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

    // ===== JUCE INTEGRATION =====
    
    /**
     * @brief Prepare all voices for new buffer size (JUCE integration)
     * @param maxBlockSize Maximum block size from DAW
     */
    void prepareToPlay(int maxBlockSize) noexcept;

    // ===== CORE AUDIO API =====

    /**
     * @brief Nastavení stavu MIDI noty (note-on/note-off)
     * @param midiNote MIDI nota (0-127)
     * @param isOn true = note-on, false = note-off
     * @param velocity MIDI velocity (0-127), nyní správně ovlivňuje hlasitost
     */
    void setNoteStateMIDI(uint8_t midiNote, bool isOn, uint8_t velocity) noexcept;
    void setNoteStateMIDI(uint8_t midiNote, bool isOn) noexcept;

    // ===== GLOBAL VOICE CONTROL =====

    /**
     * @brief Nastavení master gain pro všechny voices
     * @param midi_gain Master gain jako MIDI hodnota (0-127)
     * @param logger Reference na Logger
     */
    void setAllVoicesMasterGainMIDI(uint8_t midi_gain, Logger& logger);

    /**
     * @brief Nastavení pan pro všechny voices
     * @param pan Pan hodnota jako MIDI hodnota (0-127, kde 64 je stred)
     */
    void setAllVoicesPanMIDI(uint8_t midi_pan) noexcept;

    /**
     * @brief RT-SAFE: Process audio block - INTERLEAVED format
     * @param outputBuffer Interleaved stereo buffer (AudioData array)
     * @param samplesPerBlock Number of samples to process
     * @return true if any voices are still active
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
     * @return Počet aktivních voices (sustaining + releasing)
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
    Voice& getVoiceMIDI(uint8_t midiNote) noexcept;
    
    /**
     * @brief Const přístup k jednotlivé voice podle MIDI noty
     * @param midiNote MIDI nota (0-127)
     * @return Const reference na Voice
     */
    const Voice& getVoiceMIDI(uint8_t midiNote) const noexcept;

    // ===== RT-SAFE MODE CONTROL =====
    
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

    // ===== SYSTEM DIAGNOSTICS =====
    
    /**
     * @brief Loguje detailní statistiky celého systému
     * @param logger Reference na Logger
     */
    void logSystemStatistics(Logger& logger);

private:
    // ===== ENCAPSULATED COMPONENTS =====
    
    SamplerIO samplerIO_;               // Stack allocated SamplerIO instance
    InstrumentLoader instrumentLoader_; // Stack allocated InstrumentLoader instance
    Envelope envelope_;                 // Per-instance envelope state manager (pouze per-voice state)

    // ODSTRANĚNO: EnvelopeStaticData& envelopeData_ - používáme globální statická data

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

    // ===== VOICE POOL MANAGEMENT =====
    
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