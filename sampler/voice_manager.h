#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include "voice.h"
#include "instrument_loader.h"  // Pro stack allocated member
#include "sampler.h"           // Pro SamplerIO stack allocated member
#include <vector>
#include <string>
#include <atomic>
#include <memory>

/**
 * @class VoiceManager
 * @brief Hlavní třída pro řízení polyfonnního audio systému
 * 
 * REFAKTOROVÁNO: Odstraněny všechny testovací metody pro čistou produkční implementaci.
 * Všechny testy jsou nyní v separátní třídě VoiceManagerTester.
 * 
 * Klíčové vlastnosti:
 * - 128 simultánních voices (jeden pro každou MIDI notu)
 * - Dynamic sample rate management (44100/48000 Hz)
 * - Stack allocated komponenty (SamplerIO, InstrumentLoader)
 * - JUCE integration ready (prepareToPlay pattern)
 * - RT-safe processBlock s dynamic gain scaling
 * - Initialization pipeline pro modulární setup
 */
class VoiceManager {
public:
    /**
     * @brief Konstruktor: Vytvoří VoiceManager s cestou k samples
     * @param sampleDir Cesta k sample adresáři
     * @param logger Reference na Logger
     * 
     * DŮLEŽITÉ: Konstruktor pouze vytvoří objekty s neinicializovaným sample rate!
     * Musí se explicitně volat: changeSampleRate() + initializeSystem() + loadAllInstruments()
     */
    VoiceManager(const std::string& sampleDir, Logger& logger);

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
     * @brief NOVÉ: Prepare all voices for new buffer size (JUCE integration)
     * @param maxBlockSize Maximum block size from DAW
     */
    void prepareToPlay(int maxBlockSize) noexcept;

    // ===== INITIALIZATION PIPELINE =====
    
    /**
     * @brief Inicializace systému - skenování sample adresáře
     * @param logger Reference na Logger
     */
    void initializeSystem(Logger& logger);
    
    /**
     * @brief Načtení všech instrumentů do paměti
     * @param logger Reference na Logger
     */
    void loadAllInstruments(Logger& logger);
    
    /**
     * @brief Validace integrity celého systému
     * @param logger Reference na Logger
     */
    void validateSystemIntegrity(Logger& logger);

    // ===== CORE AUDIO API =====
    
    /**
     * @brief Nastavení stavu MIDI noty (note-on/note-off)
     * @param midiNote MIDI nota (0-127)
     * @param isOn true = note-on, false = note-off
     * @param velocity MIDI velocity (0-127), nyní správně ovlivňuje hlasitost
     */
    void setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity) noexcept;
    
    /**
     * @brief HLAVNÍ RT-SAFE METODA: Zpracuje audio blok s dynamic voice scaling
     * @param outputLeft Pointer na levý kanál výstupního bufferu
     * @param outputRight Pointer na pravý kanál výstupního bufferu
     * @param numSamples Počet samples k zpracování
     * @return true pokud je nějaký audio výstup
     */
    bool processBlock(float* outputLeft, float* outputRight, int numSamples) noexcept;
    
    /**
     * @brief Alternativní processBlock s AudioData strukturou
     * @param outputBuffer Pointer na pole AudioData struktur
     * @param numSamples Počet samples k zpracování
     * @return true pokud je nějaký audio výstup
     */
    bool processBlock(AudioData* outputBuffer, int numSamples) noexcept;
    
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
    Voice& getVoice(uint8_t midiNote) noexcept;
    
    /**
     * @brief Const přístup k jednotlivé voice podle MIDI noty
     * @param midiNote MIDI nota (0-127)
     * @return Const reference na Voice
     */
    const Voice& getVoice(uint8_t midiNote) const noexcept;

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
    
    SamplerIO samplerIO_;              // Stack allocated SamplerIO instance
    InstrumentLoader instrumentLoader_; // Stack allocated InstrumentLoader instance

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