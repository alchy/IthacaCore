#ifndef ENVELOPE_H
#define ENVELOPE_H

#include <cstdint>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

// Definice sf_count_t pokud není definovaná jinde
#ifndef sf_count_t
typedef long long sf_count_t;
#endif

// Forward declaration pro Logger
class Logger;

class Envelope {
public:
    // Konstruktor: Inicializuje parametry pro generování obálek
    explicit Envelope(Logger& logger);
    
    // Nastaví frekvenci a odpovídající index (0=44100Hz, 1=48000Hz)
    void setEnvelopeFrequency(int freq, Logger& logger);
    
    // Vyplní gainBuffer gainy pro attack fázi s debug logováním
    void getGainBufferAttack(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed, Logger& logger) const noexcept;
    
    // Vyplní gainBuffer gainy pro release fázi s debug logováním
    void getGainBufferRelease(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed, Logger& logger) const noexcept;

private:
    // Konstanty z Python kódu
    static constexpr double TOTAL_DURATION = 12.0;  // seconds
    static constexpr double TAU_DIVISOR = 5.0;      // For ~99% change at max length
    static constexpr int MIDI_MIN = 0;
    static constexpr int MIDI_MAX = 127;
    static constexpr double CONVERGENCE_THRESHOLD = 0.01;  // 99% convergence
    
    // Podporované sample rates
    static constexpr int SAMPLE_RATES[2] = {44100, 48000};
    
    // Stav instance
    int bitrate_;
    int sample_rate_index_;
    
    // Pomocné metody pro generování dat
    double calculateTau(uint8_t midi) const noexcept;
    int calculateEnvelopeLength(uint8_t midi, int sample_rate, bool is_attack) const noexcept;
    void generateEnvelopeData(uint8_t midi, int sample_rate, bool is_attack, 
                            float* buffer, int buffer_size, int& actual_length) const noexcept;
    
    // Pomocné metody pro debug logování
    std::string formatValuesForLog(const float* values, int count) const noexcept;
    void logEnvelopeData(const std::string& envelope_type, int sample_rate, uint8_t midi, 
                        const float* data, int length, Logger& logger) const noexcept;
    void logRuntimeBuffer(const std::string& envelope_type, uint8_t midi, 
                         const float* buffer, int numSamples, sf_count_t start_elapsed, 
                         Logger& logger) const noexcept;
    
    // Inline metoda pro rychlé generování jednotlivého vzorku
    inline float calculateEnvelopeSample(double t, double tau, bool is_attack) const noexcept {
        if (tau == 0.0) {
            return is_attack ? 1.0f : 0.0f;
        }
        
        if (is_attack) {
            return static_cast<float>(1.0 - std::exp(-t / tau));
        } else {
            return static_cast<float>(std::exp(-t / tau));
        }
    }
};

#endif // ENVELOPE_H