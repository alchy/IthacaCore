/*
 * envelope.h - RT-safe envelope lookup z runtime-generated dat pro attack a release.
 * ZMĚNA: Místo načítání z envelopes.h generuje identická data za běhu při startu.
 * OPTIMALIZACE: Dynamická alokace podle skutečné délky pro každý sample rate.
 * Dvě separátní metody: getGainBufferAttack a getGainBufferRelease.
 * Podpora dynamické změny sample rate přes setEnvelopeFrequency.
 * getGainBuffer* bere absolutní start samples a vyplní buffer pro blok (okno).
 * Logování: V konstruktoru (generovaná data) a setEnvelopeFrequency (změna bitrate).
 * Velikost bináru: ~0 MB místo 497 MB, paměť optimalizována podle skutečných délek.
 */

#ifndef ENVELOPE_H
#define ENVELOPE_H

#include <cstdint>  // Pro uint8_t
#include "core_logger.h"  // Pro Logger
#include <sndfile.h>      // Pro sf_count_t (absolutní samples)
#include <vector>
#include <memory>
#include <cmath>

// Parametry z Python kódu pro runtime generování
static constexpr float TOTAL_DURATION = 12.0f;         // seconds  
static constexpr float TAU_DIVISOR = 5.0f;             // For ~99% change
static constexpr float THRESHOLD = 0.01f;              // 99% convergence
static constexpr int MAX_LEN_44100 = int(44100 * 12.0) + 1;  // Max for 44100 Hz
static constexpr int MAX_LEN_48000 = int(48000 * 12.0) + 1;  // Max for 48000 Hz

// Runtime envelope data structure - OPTIMALIZOVANÁ s dynamickou alokací
struct EnvelopeData {
    std::unique_ptr<float[]> data_44100;  // Dynamicky alokováno podle skutečné délky
    std::unique_ptr<float[]> data_48000;  // Dynamicky alokováno podle skutečné délky
    int len_44100;                        // Skutečný počet vzorků pro 44100 Hz
    int len_48000;                        // Skutečný počet vzorků pro 48000 Hz
    
    // Konstruktor pro inicializaci
    EnvelopeData() : len_44100(0), len_48000(0) {}
    
    // Move konstruktor a assignment pro std::unique_ptr
    EnvelopeData(EnvelopeData&& other) noexcept 
        : data_44100(std::move(other.data_44100))
        , data_48000(std::move(other.data_48000))
        , len_44100(other.len_44100)
        , len_48000(other.len_48000) {}
    
    EnvelopeData& operator=(EnvelopeData&& other) noexcept {
        if (this != &other) {
            data_44100 = std::move(other.data_44100);
            data_48000 = std::move(other.data_48000);
            len_44100 = other.len_44100;
            len_48000 = other.len_48000;
        }
        return *this;
    }
    
    // Zakázání copy konstruktoru/assignment (kvůli unique_ptr)
    EnvelopeData(const EnvelopeData&) = delete;
    EnvelopeData& operator=(const EnvelopeData&) = delete;
};

/**
 * @class Envelope
 * @brief Univerzální třída pro RT-safe gain buffer vyplnění pro attack/release.
 * 
 * ZMĚNA: Generuje data za běhu v konstruktoru místo načítání z envelopes.h (ušetří 497 MB v bináru).
 * OPTIMALIZACE: Alokuje pouze potřebnou paměť pro každé MIDI a sample rate.
 * Nastav index pro sample rate separátně přes setEnvelopeFrequency (s logováním změny bitrate).
 * getGainBufferAttack/getGainBufferRelease vyplní gainBuffer pro numSamples počínaje start_elapsed 
 * (absolutní indexace do pole pro dané MIDI).
 * Použije přímý access data[idx] s clampem na 0..len-1 (nearest neighbor pro RT-safe).
 * sampleRateIndex: Interně 0=44100Hz, 1=48000Hz (nastaveno v setEnvelopeFrequency).
 * bitrate_: Private člen pro uchování aktuální frekvence (pro logování a rozhodování).
 * RT-safe: Přímý array access v loopu, žádné logování v runtime metodách.
 * 
 * Použití: V VoiceManager: Envelope envelope(logger); envelope.setEnvelopeFrequency(sample_rate, logger);
 * V calculateBlockGains (Releasing): envelope.getGainBufferRelease(midi_note_, gainBuffer, numSamples, releaseElapsed);
 */
class Envelope {
public:
    /**
     * @brief Konstruktor: Generuje attack a release data za běhu, loguje informace o datech.
     * ZMĚNA: Místo načítání z const arrays generuje identická data v paměti s optimalizovanou alokací.
     * Nenastavuje frekvenci (index = -1, bitrate = 0).
     * @param logger Reference pro logování (informace o generovaných datech a úspoře paměti).
     */
    explicit Envelope(Logger& logger);

    /**
     * @brief Nastaví frekvenci vzorkování (bitrate) a odpovídající index (0 pro 44100, 1 pro 48000).
     * Loguje změnu bitrate a index.
     * Voláno po konstruktoru nebo při změně sample rate v programu.
     * @param freq Frekvence v Hz (pouze 44100 nebo 48000).
     * @param logger Reference pro logování změny (nebo error při neplatné freq).
     */
    void setEnvelopeFrequency(int freq, Logger& logger);

    /**
     * @brief Vyplní gainBuffer gainy pro attack fázi počínaje absolutní pozicí v samplech (pro dané MIDI).
     * Přímá indexace: pro i=0..numSamples-1: gainBuffer[i] = data[start_elapsed + i] (s clampem).
     * Pro len=1 (MIDI 0): Vyplní 1.0f (okamžitý nárůst).
     * Použije aktuální sample_rate_index_; pokud není nastaven (-1), vyplní 0.0f.
     * @param midi MIDI hodnota (0-127).
     * @param gainBuffer Buffer k vyplnění (může být nullptr pro validaci).
     * @param numSamples Počet samples v bufferu (např. 512).
     * @param start_elapsed Absolutní start samples od začátku attack fáze.
     */
    void getGainBufferAttack(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed) const noexcept;

    /**
     * @brief Vyplní gainBuffer gainy pro release fázi počínaje absolutní pozicí v samplech (pro dané MIDI).
     * Přímá indexace: pro i=0..numSamples-1: gainBuffer[i] = data[start_elapsed + i] (s clampem).
     * Pro len=1 (MIDI 0): Vyplní 0.0f (okamžitý útlum).
     * Použije aktuální sample_rate_index_; pokud není nastaven (-1), vyplní 0.0f.
     * @param midi MIDI hodnota (0-127).
     * @param gainBuffer Buffer k vyplnění (může být nullptr pro validaci).
     * @param numSamples Počet samples v bufferu (např. 512).
     * @param start_elapsed Absolutní start samples od začátku release fáze (např. releaseElapsed).
     */
    void getGainBufferRelease(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed) const noexcept;

private:
    Logger& logger_;                    // Reference na logger pro logování
    int sample_rate_index_ = -1;        // Interní index: 0=44100, 1=48000, -1=nevalidní
    int bitrate_ = 0;                   // Aktuální bitrate (Hz) pro rozhodování a logování (nastavený v setteru)
    
    // Runtime generovaná data (optimalizovaná alokace podle skutečných délek)
    std::vector<EnvelopeData> attack_data_;   // [128] MIDI hodnot s dynamickou alokací
    std::vector<EnvelopeData> release_data_;  // [128] MIDI hodnot s dynamickou alokací
    
    // Generovací metody (volané v konstruktoru)
    float calculateTau(uint8_t midi) const;
    void generateEnvelopeData(uint8_t midi, int sample_rate, bool is_attack, 
                            std::vector<float>& output, int& num_samples) const;
    void generateAllEnvelopes();
    
    // Pomocná metoda pro vyplnění bufferu z envelope dat (společná pro attack/release)
    void fillBufferFromData(const EnvelopeData& env, float* gainBuffer, int numSamples, sf_count_t start_elapsed, bool isAttack) const noexcept;
};

#endif  // ENVELOPE_H