#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <atomic>
#include <functional>
#include "core_logger.h"

/**
 * @brief Statická třída pro sdílená envelope data mezi všemi VoiceManager instancemi
 * 
 * EnvelopeStaticData obsahuje všechna těžká předpočítaná data (attack/release křivky)
 * pro všechny MIDI hodnoty (0-127) a vzorkovací frekvence (44100/48000 Hz).
 * 
 * Tato data jsou inicializována jednou globálně před vytvořením VoiceManagerů
 * a poté sdílena mezi všemi Voice instancemi pro maximální úsporu paměti.
 */
class EnvelopeStaticData {
public:
    /**
     * @brief GLOBÁLNÍ INICIALIZACE: Musí se volat před vytvořením jakýchkoli VoiceManagerů
     * NON-RT SAFE: Alokuje paměť a generuje všechna envelope data
     * 
     * @param logger Reference na Logger pro logování
     * @return true při úspěchu, std::exit(1) při chybě
     */
    static bool initialize(Logger& logger);

    /**
     * @brief Cleanup: Uvolní všechna statická data
     * NON-RT SAFE: Volat na konci programu
     */
    static void cleanup() noexcept;

    /**
     * @brief RT-SAFE: Získá hodnoty attack obálky pro daný MIDI parametr
     * 
     * @param gainBuffer Ukazatel na buffer pro výstupní hodnoty
     * @param numSamples Počet požadovaných vzorků
     * @param position Pozice v obálce (offset)
     * @param midiValue MIDI hodnota pro attack (0-127)
     * @param sampleRate Vzorkovací frekvence (44100 nebo 48000)
     * @return true pokud obálka pokračuje, false při dosažení konce
     */
    static bool getAttackGains(float* gainBuffer, int numSamples, int position,
                              uint8_t midiValue, int sampleRate) noexcept;

    /**
     * @brief RT-SAFE: Získá hodnoty release obálky pro daný MIDI parametr
     * 
     * @param gainBuffer Ukazatel na buffer pro výstupní hodnoty
     * @param numSamples Počet požadovaných vzorků
     * @param position Pozice v obálce (offset)
     * @param midiValue MIDI hodnota pro release (0-127)
     * @param sampleRate Vzorkovací frekvence (44100 nebo 48000)
     * @return true pokud obálka pokračuje, false při dosažení konce
     */
    static bool getReleaseGains(float* gainBuffer, int numSamples, int position,
                               uint8_t midiValue, int sampleRate) noexcept;

    /**
     * @brief RT-SAFE: Vrátí délku attack obálky v milisekundách
     * 
     * @param midiValue MIDI hodnota (0-127)
     * @param sampleRate Vzorkovací frekvence (44100 nebo 48000)
     * @return float Délka v ms
     */
    static float getAttackLength(uint8_t midiValue, int sampleRate) noexcept;

    /**
     * @brief RT-SAFE: Vrátí délku release obálky v milisekundách
     * 
     * @param midiValue MIDI hodnota (0-127)
     * @param sampleRate Vzorkovací frekvence (44100 nebo 48000)
     * @return float Délka v ms
     */
    static float getReleaseLength(uint8_t midiValue, int sampleRate) noexcept;

    /**
     * @brief RT-SAFE: Kontrola, zda jsou data inicializována
     * 
     * @return true pokud jsou data připravena k použití
     */
    static bool isInitialized() noexcept { return initialized_.load(); }

    // Error callback type - MUSÍ BÝT PŘED setErrorCallback deklarací
    using ErrorCallback = std::function<void(const std::string&, const std::string&, const std::string&)>;

    /**
     * @brief Nastavení error callback pro RT funkce
     * 
     * @param callback Callback funkce pro hlášení chyb
     */
    static void setErrorCallback(ErrorCallback callback);

private:
    // Konstanty podle Python skriptu
    static constexpr float TOTAL_DURATION = 12.0f;
    static constexpr float TAU_DIVISOR = 5.0f;
    static constexpr float CONVERGENCE_THRESHOLD = 0.01f;
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
        
        EnvelopeIndex() : data(nullptr), length(0) {}
    };

    // STATICKÁ DATA - sdílená mezi všemi instance
    static std::vector<float> attack_buffer_[NUM_SAMPLE_RATES];
    static std::vector<float> release_buffer_[NUM_SAMPLE_RATES];
    static EnvelopeIndex attack_index_[NUM_SAMPLE_RATES][128];
    static EnvelopeIndex release_index_[NUM_SAMPLE_RATES][128];
    
    static std::atomic<bool> initialized_;
    static ErrorCallback errorCallback_;

    // PRIVATE HELPER METODY
    
    /**
     * @brief Výpočet tau hodnoty na základě MIDI hodnoty
     */
    static float calculateTau(uint8_t midi) noexcept;

    /**
     * @brief Generuje obálky pro zadanou vzorkovací frekvenci
     */
    static void generateEnvelopeForSampleRate(int sampleRate, Logger& logger);

    /**
     * @brief Generuje jednu obálku (attack nebo release)
     */
    static std::pair<std::vector<float>, int> generateSingleEnvelope(
        uint8_t midi, int sampleRate, const std::string& envelope_type);

    /**
     * @brief Pomocné funkce pro indexování
     */
    static int getSampleRateIndex(int sampleRate) noexcept;
    static bool isValidSampleRateIndex(int index) noexcept;
    static bool isValidMidiValue(uint8_t midi) noexcept;

    /**
     * @brief RT-safe error handling
     */
    static void reportError(const std::string& component, const std::string& severity, 
                           const std::string& message) noexcept;
    static void exitOnError(const std::string& component, const std::string& severity, 
                           const std::string& message) noexcept;

    /**
     * @brief Logging helpers
     */
    static void logEnvelopeData(const std::vector<float>& data, const std::string& type,
                               int sampleRate, uint8_t midi_value, Logger& logger);
};