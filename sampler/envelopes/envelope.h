#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <functional>
#include "core_logger.h"

/**
 * @brief Třída pro správu ADSR obálek pro audio syntézu
 * 
 * Envelope třída poskytuje RT-safe operace pro získávání hodnot attack a release obálek.
 * Obálky jsou předpočítané při inicializaci pro vzorkovací frekvence 44100 Hz a 48000 Hz
 * pro všechny MIDI hodnoty (0-127).
 * 
 * Integrace s Voice třídou:
 * - Voice::initialize přijímá referenci na Envelope
 * - Voice::calculateBlockGains volá getAttackGains/getReleaseGains
 * - Voice používá getSustainLevel pro sustain fázi
 * - Voice používá getAttackLength/getReleaseLength pro informativní účely
 */
class Envelope {
public:
    /**
     * @brief Prázdný konstruktor
     * Inicializuje třídu v neplatném stavu - je nutné volat initialize()
     */
    Envelope();

    /**
     * @brief Inicializuje předpočítané obálky pro obě vzorkovací frekvence
     * NON-RT SAFE: Alokuje paměť a generuje obálky
     * 
     * @param logger Reference na Logger pro logování
     * @return true při úspěchu, false při chybě
     */
    bool initialize(Logger& logger);

    /**
     * @brief Nastaví vzorkovací frekvenci pro následné operace
     * NON-RT SAFE: Loguje změnu frekvence
     * 
     * @param sampleRate Vzorkovací frekvence (44100 nebo 48000 Hz)
     * @param logger Reference na Logger pro logování
     * @return true při úspěchu, false při neplatné frekvenci
     */
    bool setEnvelopeFrequency(int sampleRate, Logger& logger);

    /**
     * @brief RT-SAFE: Získá hodnoty attack obálky
     * 
     * @param gain_buffer Ukazatel na buffer pro výstupní hodnoty
     * @param num_samples Počet požadovaných vzorků
     * @param envelope_attack_position Pozice v obálce (offset)
     * @return true pokud obálka pokračuje, false při dosažení konce
     */
    bool getAttackGains(float* gain_buffer, int num_samples, int envelope_attack_position) const;

    /**
     * @brief RT-SAFE: Získá hodnoty release obálky
     * 
     * @param gain_buffer Ukazatel na buffer pro výstupní hodnoty
     * @param num_samples Počet požadovaných vzorků
     * @param envelope_release_position Pozice v obálce (offset)
     * @return true pokud obálka pokračuje, false při dosažení konce
     */
    bool getReleaseGains(float* gain_buffer, int num_samples, int envelope_release_position) const;

    /**
     * @brief RT-SAFE: Nastaví MIDI hodnotu pro attack obálku
     * 
     * @param midi_value MIDI hodnota (0-127)
     */
    void setAttackMIDI(uint8_t midi_value);

    /**
     * @brief RT-SAFE: Nastaví MIDI hodnotu pro release obálku
     * 
     * @param midi_value MIDI hodnota (0-127)
     */
    void setReleaseMIDI(uint8_t midi_value);

    /**
     * @brief RT-SAFE: Nastaví sustain úroveň na základě MIDI hodnoty
     * 
     * @param midi_value MIDI hodnota (0-127) → lineárně mapováno na (0.0f-1.0f)
     */
    void setSustainLevelMIDI(uint8_t midi_value);

    /**
     * @brief RT-SAFE: Vrátí aktuální sustain úroveň
     * 
     * @return float Sustain úroveň (0.0f-1.0f)
     */
    float getSustainLevel() const;

    /**
     * @brief RT-SAFE: Vrátí délku attack obálky v milisekundách
     * 
     * @return float Délka v ms na základě aktuální vzorkovací frekvence a MIDI hodnoty
     */
    float getAttackLength() const;

    /**
     * @brief RT-SAFE: Vrátí délku release obálky v milisekundách
     * 
     * @return float Délka v ms na základě aktuální vzorkovací frekvence a MIDI hodnoty
     */
    float getReleaseLength() const;

    /**
     * @brief Static metoda pro nastavení RT módu (podobně jako Voice::rtMode_)
     * 
     * @param enabled true pro RT mód (bez logování), false pro non-RT mód
     */
    static void setRTMode(bool enabled);

    /*
     * @brief methods for error logging (in RT)
     */
    using ErrorCallback = std::function<void(const std::string&, const std::string&, const std::string&)>;
    void  setErrorCallback(ErrorCallback callback);

private:
    // Konstanty podle Python skriptu
    static constexpr float TOTAL_DURATION = 12.0f;     // sekund
    static constexpr float TAU_DIVISOR = 5.0f;         // pro ~99% konvergenci
    static constexpr float CONVERGENCE_THRESHOLD = 0.01f; // 1% threshold
    static constexpr int SAMPLE_RATES[2] = {44100, 48000};
    static constexpr int NUM_SAMPLE_RATES = 2;
    static constexpr int MAX_MIDI = 127;
    static constexpr int SAMPLE_RATE_INDEX_INVALID = -1;
    static constexpr int SAMPLE_RATE_INDEX_44100 = 0;
    static constexpr int SAMPLE_RATE_INDEX_48000 = 1;

    // Struktura pro indexování obálky
    struct EnvelopeIndex {
        const float* data;   // Ukazatel na začátek dat obálky
        int length;          // Počet vzorků v obálce
    };

    // Souvislé buffery pro všechna envelope data (lepší cache lokalita)
    // Index 0 = 44100 Hz, Index 1 = 48000 Hz
    std::vector<float> attack_buffer_[NUM_SAMPLE_RATES];    // Všechna attack data pro každou frekvenci
    std::vector<float> release_buffer_[NUM_SAMPLE_RATES];   // Všechna release data pro každou frekvenci
    
    // Indexy pro rychlý přístup k datům (128 MIDI hodnot × 2 vzorkovací frekvence)
    // První index = vzorkovací frekvence (0=44100, 1=48000), druhý index = MIDI hodnota (0-127)
    EnvelopeIndex attack_index_[NUM_SAMPLE_RATES][128];     // Indexy attack obálek
    EnvelopeIndex release_index_[NUM_SAMPLE_RATES][128];    // Indexy release obálek
    
    // Aktuální stav
    int sample_rate_index_;          // -1 = nenastaveno, 0 = 44100Hz, 1 = 48000Hz
    int current_sample_rate_;        // Aktuální vzorkovací frekvence pro výpočty délky
    uint8_t attack_midi_index_;      // Aktuální MIDI index pro attack (0-127)
    uint8_t release_midi_index_;     // Aktuální MIDI index pro release (0-127)
    float sustain_level_;            // Sustain úroveň (0.0f-1.0f)
    
    // RT mode flag
    static std::atomic<bool> rtMode_;

    /**
     * @brief Vypočítá tau hodnotu na základě MIDI hodnoty
     * 
     * @param midi MIDI hodnota (0-127)
     * @return float Tau hodnota v sekundách
     */
    float calculateTau(uint8_t midi) const;

    /**
     * @brief Generuje obálky pro zadanou vzorkovací frekvenci
     * NON-RT SAFE: Alokuje paměť
     * 
     * @param sampleRate Vzorkovací frekvence (44100 nebo 48000)
     * @param logger Reference na Logger
     */
    void generateEnvelopeForSampleRate(int sampleRate, Logger& logger);

    /**
     * @brief Generuje jednu obálku (attack nebo release) pro dané MIDI a frekvenci
     * 
     * @param midi MIDI hodnota (0-127)
     * @param sampleRate Vzorkovací frekvence
     * @param envelope_type "attack" nebo "release"
     * @return std::pair<std::vector<float>, int> Data a počet vzorků
     */
    std::pair<std::vector<float>, int> generateSingleEnvelope(uint8_t midi, int sampleRate, 
                                                              const std::string& envelope_type) const;

    /**
     * @brief NON-RT SAFE: Loguje data obálky podle specifikace
     * 
     * @param data Data obálky
     * @param type Typ obálky ("attack" nebo "release")
     * @param sampleRate Vzorkovací frekvence
     * @param midi_value MIDI hodnota
     * @param logger Reference na Logger
     */
    void logEnvelopeData(const std::vector<float>& data, const std::string& type, 
                        int sampleRate, uint8_t midi_value, Logger& logger) const;

    /**
     * @brief NON-RT SAFE: Loguje vrácené hodnoty gainů během runtime
     * 
     * @param buffer Buffer s daty
     * @param num_samples Počet vzorků
     * @param type Typ obálky ("Attack" nebo "Release")
     * @param position Pozice v obálce
     * @param sampleRate Vzorkovací frekvence
     * @param logger Reference na Logger
     */
    void logGainsData(const float* buffer, int num_samples, const std::string& type, 
                     int position, int sampleRate, Logger& logger) const;

    /**
     * @brief RT-SAFE wrapper pro logování - kontroluje RT mód
     * 
     * @param component Komponenta
     * @param severity Úroveň závažnosti
     * @param message Zpráva
     * @param logger Reference na Logger
     */
    void logSafe(const std::string& component, const std::string& severity, 
                const std::string& message, Logger& logger) const;

    /**
     * @brief Pomocná funkce pro získání indexu vzorkovací frekvence
     * 
     * @param sampleRate Vzorkovací frekvence
     * @return int Index vzorkovací frekvence nebo SAMPLE_RATE_INDEX_INVALID
     */
    int getSampleRateIndex(int sampleRate) const;

    /**
     * @brief Pomocná funkce pro validaci indexu vzorkovací frekvence
     * 
     * @param index Index vzorkovací frekvence
     * @return bool true pokud je index platný
     */
    bool isValidSampleRateIndex(int index) const;

    /*
     * @brief methods for error logging (in RT)
     */
    ErrorCallback errorCallback_;
    void reportError(const std::string& component, const std::string& severity, const std::string& message) const;
    void exitOnError(const std::string& component, const std::string& severity, const std::string& message) const;
};