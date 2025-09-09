/*
 * envelope.cpp - Implementace s runtime generováním a optimalizovanou alokací paměti
 * Generuje identická data jako Python script, ale za běhu při startu
 * OPTIMALIZACE: Alokuje pouze potřebnou paměť pro každé MIDI a sample rate
 */

#include "envelope.h"
#include <algorithm>
#include <cstring>
#include <chrono>

/**
 * @brief Konstruktor: Generuje všechna envelope data za běhu s optimalizovanou alokací
 */
Envelope::Envelope(Logger& logger) : logger_(logger) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    logger_.log("Envelope/constructor", "info", 
                "Starting runtime envelope data generation with optimized memory allocation...");
    
    // Alokace prostoru pro všechna MIDI data
    attack_data_.resize(128);
    release_data_.resize(128);
    
    // Generuj všechna envelope data
    generateAllEnvelopes();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Spočítej celkovou velikost v paměti (skutečná použitá paměť)
    size_t total_bytes = 0;
    size_t max_possible_bytes = 0;  // Pro porovnání s fixed alokací
    
    for (int midi = 0; midi < 128; ++midi) {
        // Skutečně použitá paměť
        total_bytes += attack_data_[midi].len_44100 * sizeof(float);
        total_bytes += attack_data_[midi].len_48000 * sizeof(float);
        total_bytes += release_data_[midi].len_44100 * sizeof(float);
        total_bytes += release_data_[midi].len_48000 * sizeof(float);
        
        // Paměť při fixed alokaci (MAX_LEN pro každý)
        max_possible_bytes += MAX_LEN_44100 * sizeof(float);  // attack 44100
        max_possible_bytes += MAX_LEN_48000 * sizeof(float);  // attack 48000  
        max_possible_bytes += MAX_LEN_44100 * sizeof(float);  // release 44100
        max_possible_bytes += MAX_LEN_48000 * sizeof(float);  // release 48000
    }
    
    float memory_efficiency = (1.0f - (float)total_bytes / max_possible_bytes) * 100.0f;
    
    logger_.log("Envelope/constructor", "info", 
                "Envelope data generated in " + std::to_string(duration.count()) + 
                " ms. Optimized memory usage: " + std::to_string(total_bytes / 1024 / 1024) + 
                " MB (vs " + std::to_string(max_possible_bytes / 1024 / 1024) + 
                " MB fixed allocation = " + std::to_string(static_cast<int>(memory_efficiency)) + "% savings)");
    
    // Validace: Kontrola MIDI 127
    if (attack_data_[127].len_44100 <= 0 || release_data_[127].len_44100 <= 0) {
        logger_.log("Envelope/constructor", "error", "Invalid generated data for MIDI 127");
        std::exit(1);
    }
    
    logger_.log("Envelope/constructor", "info", "Data validation OK - ready for setEnvelopeFrequency");
}

/**
 * @brief Vypočítá tau konstantu pro dané MIDI (stejně jako Python)
 */
float Envelope::calculateTau(uint8_t midi) const {
    if (midi == 0) return 0.0f;
    return (midi / 127.0f) * (TOTAL_DURATION / TAU_DIVISOR);
}

/**
 * @brief Generuje envelope data pro jedno MIDI a sample rate (stejný algoritmus jako Python)
 */
void Envelope::generateEnvelopeData(uint8_t midi, int sample_rate, bool is_attack, 
                                  std::vector<float>& output, int& num_samples) const {
    float tau = calculateTau(midi);
    float target_val = is_attack ? 1.0f : 0.0f;
    
    if (midi == 0) {
        // Speciální případ MIDI 0: immediate response, single sample at target
        output = {target_val};
        num_samples = 1;
        return;
    }
    
    // Výpočet stable time (stejně jako Python)
    float log_threshold = -std::log(THRESHOLD);  // ≈4.605 for threshold=0.01
    float t_stable = tau * log_threshold;
    t_stable = std::min(t_stable, TOTAL_DURATION);
    
    // Počet vzorků: sample_rate * t_stable, rounded up for inclusion, with safety limits
    int max_len = (sample_rate == 44100) ? MAX_LEN_44100 : MAX_LEN_48000;
    num_samples = std::max(1, std::min(static_cast<int>(sample_rate * t_stable) + 1, max_len));
    
    // Generuj data
    output.resize(num_samples);
    for (int i = 0; i < num_samples; ++i) {
        float t = (i * t_stable) / (num_samples - 1);  // Time array up to t_stable
        
        if (is_attack) {
            output[i] = 1.0f - std::exp(-t / tau);
        } else {
            output[i] = std::exp(-t / tau);
        }
        
        // Normalize to 0-1, clamp
        output[i] = std::max(0.0f, std::min(1.0f, output[i]));
    }
    
    // Truncate to where it meets threshold - OPRAVENO (stejně jako v Python)
    int converge_idx = -1;
    for (int i = 0; i < num_samples; ++i) {
        if (is_attack) {
            if (output[i] >= (1.0f - THRESHOLD)) {  // >= 0.99
                converge_idx = i;
                break;
            }
        } else {
            if (output[i] <= THRESHOLD) {  // <= 0.01
                converge_idx = i;
                break;
            }
        }
    }
    
    if (converge_idx > 0) {
        num_samples = converge_idx + 1;
        output.resize(num_samples);  // Ořízni na skutečnou délku
    }
    
    // ODSTRANENO: Násilné nastavení posledního vzorku (jako v Python verzi)
    // Ponecháváme přirozenou konvergenci exponenciální křivky
}

/**
 * @brief Generuje všechna envelope data s optimalizovanou alokací (volané v konstruktoru)
 */
void Envelope::generateAllEnvelopes() {
    for (int midi = 0; midi < 128; ++midi) {
        std::vector<float> temp_data;
        
        // Attack envelopes pro oba sample rates
        generateEnvelopeData(midi, 44100, true, temp_data, attack_data_[midi].len_44100);
        attack_data_[midi].data_44100 = std::make_unique<float[]>(attack_data_[midi].len_44100);
        std::copy(temp_data.begin(), temp_data.end(), attack_data_[midi].data_44100.get());
        
        generateEnvelopeData(midi, 48000, true, temp_data, attack_data_[midi].len_48000);
        attack_data_[midi].data_48000 = std::make_unique<float[]>(attack_data_[midi].len_48000);
        std::copy(temp_data.begin(), temp_data.end(), attack_data_[midi].data_48000.get());
        
        // Release envelopes pro oba sample rates
        generateEnvelopeData(midi, 44100, false, temp_data, release_data_[midi].len_44100);
        release_data_[midi].data_44100 = std::make_unique<float[]>(release_data_[midi].len_44100);
        std::copy(temp_data.begin(), temp_data.end(), release_data_[midi].data_44100.get());
        
        generateEnvelopeData(midi, 48000, false, temp_data, release_data_[midi].len_48000);
        release_data_[midi].data_48000 = std::make_unique<float[]>(release_data_[midi].len_48000);
        std::copy(temp_data.begin(), temp_data.end(), release_data_[midi].data_48000.get());
    }
}

/**
 * @brief Nastaví frekvenci (stejně jako původní implementace)
 */
void Envelope::setEnvelopeFrequency(int freq, Logger& logger) {
    bitrate_ = freq;  // Uložení bitrate pro rozhodování a logování
    
    if (freq == 44100) {
        sample_rate_index_ = 0;
        logger.log("Envelope/setEnvelopeFrequency", "info", 
                   "Bitrate changed to 44100 Hz (index=0)");
    } else if (freq == 48000) {
        sample_rate_index_ = 1;
        logger.log("Envelope/setEnvelopeFrequency", "info", 
                   "Bitrate changed to 48000 Hz (index=1)");
    } else {
        logger.log("Envelope/setEnvelopeFrequency", "error", 
                   "Invalid frequency " + std::to_string(freq) + " Hz - only 44100/48000 supported. Terminating.");
        std::exit(1);
    }
}

/**
 * @brief Vyplní gainBuffer pro attack fázi s optimalizovaným přístupem k datům
 */
void Envelope::getGainBufferAttack(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed) const noexcept {
    if (sample_rate_index_ == -1 || midi > 127 || numSamples <= 0 || !gainBuffer) {
        // Fallback: Vyplň 0.0f
        std::fill(gainBuffer, gainBuffer + numSamples, 0.0f);
        return;
    }
    
    const auto& env = attack_data_[midi];
    fillBufferFromData(env, gainBuffer, numSamples, start_elapsed, true);  // true = isAttack
}

/**
 * @brief Vyplní gainBuffer pro release fázi s optimalizovaným přístupem k datům
 */
void Envelope::getGainBufferRelease(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed) const noexcept {
    if (sample_rate_index_ == -1 || midi > 127 || numSamples <= 0 || !gainBuffer) {
        // Fallback: Vyplň 0.0f
        std::fill(gainBuffer, gainBuffer + numSamples, 0.0f);
        return;
    }
    
    const auto& env = release_data_[midi];
    fillBufferFromData(env, gainBuffer, numSamples, start_elapsed, false);  // false = isAttack
}

/**
 * @brief Pomocná: Vyplní buffer s optimalizovaným přístupem k dynamicky alokovaným datům
 * RT-safe: Jednoduchý loop s array access (bez memcpy pro univerzálnost s clampem).
 * @param env Envelope data pro dané MIDI s optimalizovanou alokací.
 * @param gainBuffer Buffer k vyplnění.
 * @param numSamples Počet samples.
 * @param start_elapsed Absolutní start samples.
 * @param isAttack True pro attack (konstantní 1.0f při len=1), false pro release (0.0f).
 */
void Envelope::fillBufferFromData(const EnvelopeData& env, float* gainBuffer, int numSamples, sf_count_t start_elapsed, bool isAttack) const noexcept {
    // Vyber správná data a délku podle sample rate
    const float* src_data;
    int len;
    
    if (sample_rate_index_ == 0) {
        src_data = env.data_44100.get();
        len = env.len_44100;
    } else {
        src_data = env.data_48000.get();
        len = env.len_48000;
    }
    
    if (len <= 1) {
        // Speciální případ pro MIDI 0 (len=1): Rozliš podle typu
        float constant_value = isAttack ? 1.0f : 0.0f;
        std::fill(gainBuffer, gainBuffer + numSamples, constant_value);
        return;
    }
    
    // RT-safe vyplnění s clampem (stejné jako původní)
    for (int i = 0; i < numSamples; ++i) {
        sf_count_t idx = start_elapsed + static_cast<sf_count_t>(i);
        // Clamp idx na [0, len-1]
        idx = std::max(static_cast<sf_count_t>(0), std::min(static_cast<sf_count_t>(len - 1), idx));
        gainBuffer[i] = src_data[static_cast<int>(idx)];
    }
}