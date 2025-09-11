/*
 * envelope.h - RT-safe ADSR envelope s fixními parametry
 * REFAKTOROVÁNO: Místo per-MIDI křivek používá fixní ADSR parametry
 * Jednoduchá konfigurace: attack_ms, decay_ms, sustain_level, release_ms
 * Position tracking přesunut do Voice třídy pro lepší kontrolu
 */

#ifndef ENVELOPE_H
#define ENVELOPE_H

#include <cstdint>  // Pro uint8_t
#include "core_logger.h"  // Pro Logger
#include <sndfile.h>      // Pro sf_count_t (absolutní samples)
#include <vector>
#include <memory>
#include <cmath>

// Konstanty pro ADSR envelope
static constexpr float DEFAULT_ATTACK_MS = 500.0f;     // Default 500ms attack
static constexpr float DEFAULT_DECAY_MS = 300.0f;      // Default 300ms decay  
static constexpr float DEFAULT_SUSTAIN_LEVEL = 0.7f;   // Default 70% sustain
static constexpr float DEFAULT_RELEASE_MS = 1000.0f;   // Default 1000ms release
static constexpr float THRESHOLD = 0.01f;              // 99% convergence

/**
 * @class Envelope
 * @brief ADSR envelope s fixními parametry pro všechny MIDI noty
 * 
 * NOVÝ DESIGN:
 * - Fixní ADSR parametry v milisekundách
 * - Generuje pouze 2 křivky (attack + release) místo 256 (128×2)
 * - Position tracking v Voice, ne v Envelope
 * - Konfigurovatelné parametry pro různé zvuky
 * - RT-safe metody bez MIDI parametru
 */
class Envelope {
public:
    /**
     * @brief Konstruktor s fixními ADSR parametry
     * @param attack_ms Attack time v milisekundách
     * @param decay_ms Decay time v milisekundách  
     * @param sustain_level Sustain level (0.0-1.0)
     * @param release_ms Release time v milisekundách
     */
    Envelope(float attack_ms, float decay_ms, float sustain_level, float release_ms);
             
    /**
     * @brief Prázdný konstruktor pro delayed initialization
     * Envelope se vytvoří v neinicializovaném stavu, musí se zavolat initialize()
     */
    Envelope();

    /**
     * @brief Inicializace envelope dat s ADSR parametry
     * @param logger Reference na Logger pro logování
     * @param attack_ms Attack time v milisekundách (default 500ms)
     * @param decay_ms Decay time v milisekundách (default 300ms)
     * @param sustain_level Sustain level 0.0-1.0 (default 0.7)
     * @param release_ms Release time v milisekundách (default 1000ms)
     */
    void initialize(Logger& logger, float attack_ms = DEFAULT_ATTACK_MS, 
                   float decay_ms = DEFAULT_DECAY_MS, float sustain_level = DEFAULT_SUSTAIN_LEVEL, 
                   float release_ms = DEFAULT_RELEASE_MS);

    /**
     * @brief Nastaví frekvenci vzorkování (44100 nebo 48000 Hz)
     * @param freq Frekvence v Hz
     * @param logger Reference na Logger
     */
    void setEnvelopeFrequency(int freq, Logger& logger);

    /**
     * @brief Získá attack envelope data od zadané pozice
     * @param gainBuffer Buffer pro výstup
     * @param numSamples Počet samples
     * @param position Pozice v envelope (spravuje Voice)
     * @return true pokud envelope pokračuje
     */
    bool getAttackGains(float* gainBuffer, int numSamples, sf_count_t position) const noexcept;

    /**
     * @brief Získá release envelope data od zadané pozice
     * @param gainBuffer Buffer pro výstup
     * @param numSamples Počet samples
     * @param position Pozice v envelope (spravuje Voice)
     * @return true pokud envelope pokračuje
     */
    bool getReleaseGains(float* gainBuffer, int numSamples, sf_count_t position) const noexcept;

    /**
     * @brief Getter pro sustain level
     * @return Sustain level (0.0-1.0)
     */
    float getSustainLevel() const noexcept { return sustain_level_; }

    /**
     * @brief Gettery pro délky envelope v samples při aktuální frekvenci
     */
    sf_count_t getAttackLength() const noexcept;
    sf_count_t getReleaseLength() const noexcept;

private:
    // Fixní ADSR parametry v milisekundách
    float attack_time_ms_;
    float decay_time_ms_;
    float sustain_level_;      // 0.0-1.0
    float release_time_ms_;
    
    // Frekvence management
    int sample_rate_index_ = -1;        // 0=44100, 1=48000, -1=nevalidní
    int bitrate_ = 0;                   // Aktuální bitrate (Hz)
    
    // Simplified envelope data struktura
    struct FixedEnvelopeData {
        std::unique_ptr<float[]> data_44100;
        std::unique_ptr<float[]> data_48000;
        int len_44100;
        int len_48000;
        
        FixedEnvelopeData() : len_44100(0), len_48000(0) {}
        
        // Move konstruktor a assignment pro std::unique_ptr
        FixedEnvelopeData(FixedEnvelopeData&& other) noexcept 
            : data_44100(std::move(other.data_44100))
            , data_48000(std::move(other.data_48000))
            , len_44100(other.len_44100)
            , len_48000(other.len_48000) {}
        
        FixedEnvelopeData& operator=(FixedEnvelopeData&& other) noexcept {
            if (this != &other) {
                data_44100 = std::move(other.data_44100);
                data_48000 = std::move(other.data_48000);
                len_44100 = other.len_44100;
                len_48000 = other.len_48000;
            }
            return *this;
        }
        
        // Zakázání copy konstruktoru/assignment
        FixedEnvelopeData(const FixedEnvelopeData&) = delete;
        FixedEnvelopeData& operator=(const FixedEnvelopeData&) = delete;
    };
    
    FixedEnvelopeData attack_envelope_;
    FixedEnvelopeData release_envelope_;
    
    // Generovací metody
    void generateAttackEnvelope();
    void generateReleaseEnvelope();
    void generateEnvelopeForSampleRate(int sampleRate, bool isAttack, 
                                      std::vector<float>& output, int& numSamples) const;
    
    // Helper metoda pro vyplnění bufferu
    void fillBufferFromFixedData(const FixedEnvelopeData& env, float* gainBuffer, 
                                int numSamples, sf_count_t position, bool isAttack) const noexcept;
};

#endif  // ENVELOPE_H