/*
 * envelope.cpp - ADSR envelope s fixními parametry
 * REFAKTOROVÁNO: Odstraněn per-MIDI kód, nahrazen fixními ADSR parametry
 */

#include "envelope.h"
#include <algorithm>
#include <cstring>
#include <chrono>

/**
 * @brief Prázdný konstruktor - vytvoří neinicializovaný Envelope
 */
Envelope::Envelope() : attack_time_ms_(0.0f), decay_time_ms_(0.0f), 
                       sustain_level_(0.0f), release_time_ms_(0.0f),
                       sample_rate_index_(-1), bitrate_(0) {
    // Data se vygenerují až při volání initialize()
}

/**
 * @brief Konstruktor s ADSR parametry
 */
Envelope::Envelope(float attack_ms, float decay_ms, float sustain_level, float release_ms)
    : attack_time_ms_(attack_ms), decay_time_ms_(decay_ms), 
      sustain_level_(sustain_level), release_time_ms_(release_ms),
      sample_rate_index_(-1), bitrate_(0) {
    
    // Validace parametrů
    attack_time_ms_ = std::max(1.0f, attack_time_ms_);     // Min 1ms
    decay_time_ms_ = std::max(1.0f, decay_time_ms_);       // Min 1ms
    sustain_level_ = std::max(0.0f, std::min(1.0f, sustain_level_)); // 0.0-1.0
    release_time_ms_ = std::max(1.0f, release_time_ms_);    // Min 1ms
    
    // Data se vygenerují až při volání initialize() nebo setEnvelopeFrequency()
}

/**
 * @brief Inicializace envelope dat s ADSR parametry
 */
void Envelope::initialize(Logger& logger, float attack_ms, float decay_ms, 
                         float sustain_level, float release_ms) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    logger.log("Envelope/initialize", "info", 
              "Starting ADSR envelope initialization with fixed parameters...");
    
    // Nastavení parametrů s validací
    attack_time_ms_ = std::max(1.0f, attack_ms);
    decay_time_ms_ = std::max(1.0f, decay_ms);
    sustain_level_ = std::max(0.0f, std::min(1.0f, sustain_level));
    release_time_ms_ = std::max(1.0f, release_ms);
    
    logger.log("Envelope/initialize", "info", 
              "ADSR Parameters - Attack: " + std::to_string(attack_time_ms_) + "ms, " +
              "Decay: " + std::to_string(decay_time_ms_) + "ms, " +
              "Sustain: " + std::to_string(sustain_level_) + ", " +
              "Release: " + std::to_string(release_time_ms_) + "ms");
    
    // Generuj envelope data pro oba sample rates
    generateAttackEnvelope();
    generateReleaseEnvelope();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    logger.log("Envelope/initialize", "info", 
              "ADSR envelope initialization completed in " + std::to_string(duration_ms) + "ms");
}

/**
 * @brief Nastaví frekvenci vzorkování
 */
void Envelope::setEnvelopeFrequency(int freq, Logger& logger) {
    bitrate_ = freq;
    
    if (freq == 44100) {
        sample_rate_index_ = 0;
        logger.log("Envelope/setEnvelopeFrequency", "info", 
                   "Envelope frequency set to 44100 Hz (index=0)");
    } else if (freq == 48000) {
        sample_rate_index_ = 1;
        logger.log("Envelope/setEnvelopeFrequency", "info", 
                   "Envelope frequency set to 48000 Hz (index=1)");
    } else {
        logger.log("Envelope/setEnvelopeFrequency", "error", 
                   "Invalid frequency " + std::to_string(freq) + " Hz - only 44100/48000 supported. Terminating.");
        std::exit(1);
    }
}

/**
 * @brief Získá attack envelope data od zadané pozice
 */
bool Envelope::getAttackGains(float* gainBuffer, int numSamples, sf_count_t position) const noexcept {
    if (sample_rate_index_ == -1 || !gainBuffer || numSamples <= 0) {
        // Fallback: vyplň 0.0f
        std::fill(gainBuffer, gainBuffer + numSamples, 0.0f);
        return false;
    }
    
    fillBufferFromFixedData(attack_envelope_, gainBuffer, numSamples, position, true);
    
    // Zkontroluj, zda envelope pokračuje
    sf_count_t envelopeLength = getAttackLength();
    return (position + numSamples) < envelopeLength;
}

/**
 * @brief Získá release envelope data od zadané pozice
 */
bool Envelope::getReleaseGains(float* gainBuffer, int numSamples, sf_count_t position) const noexcept {
    if (sample_rate_index_ == -1 || !gainBuffer || numSamples <= 0) {
        // Fallback: vyplň 0.0f
        std::fill(gainBuffer, gainBuffer + numSamples, 0.0f);
        return false;
    }
    
    fillBufferFromFixedData(release_envelope_, gainBuffer, numSamples, position, false);
    
    // Zkontroluj, zda envelope pokračuje
    sf_count_t envelopeLength = getReleaseLength();
    return (position + numSamples) < envelopeLength;
}

/**
 * @brief Getter pro délku attack envelope
 */
sf_count_t Envelope::getAttackLength() const noexcept {
    if (sample_rate_index_ == 0) {
        return attack_envelope_.len_44100;
    } else if (sample_rate_index_ == 1) {
        return attack_envelope_.len_48000;
    }
    return 0;
}

/**
 * @brief Getter pro délku release envelope
 */
sf_count_t Envelope::getReleaseLength() const noexcept {
    if (sample_rate_index_ == 0) {
        return release_envelope_.len_44100;
    } else if (sample_rate_index_ == 1) {
        return release_envelope_.len_48000;
    }
    return 0;
}

/**
 * @brief Generuje attack envelope pro oba sample rates
 */
void Envelope::generateAttackEnvelope() {
    std::vector<float> temp_data;
    
    // Generuj pro 44100 Hz
    generateEnvelopeForSampleRate(44100, true, temp_data, attack_envelope_.len_44100);
    attack_envelope_.data_44100 = std::make_unique<float[]>(attack_envelope_.len_44100);
    std::copy(temp_data.begin(), temp_data.end(), attack_envelope_.data_44100.get());
    
    // Generuj pro 48000 Hz
    generateEnvelopeForSampleRate(48000, true, temp_data, attack_envelope_.len_48000);
    attack_envelope_.data_48000 = std::make_unique<float[]>(attack_envelope_.len_48000);
    std::copy(temp_data.begin(), temp_data.end(), attack_envelope_.data_48000.get());
}

/**
 * @brief Generuje release envelope pro oba sample rates
 */
void Envelope::generateReleaseEnvelope() {
    std::vector<float> temp_data;
    
    // Generuj pro 44100 Hz
    generateEnvelopeForSampleRate(44100, false, temp_data, release_envelope_.len_44100);
    release_envelope_.data_44100 = std::make_unique<float[]>(release_envelope_.len_44100);
    std::copy(temp_data.begin(), temp_data.end(), release_envelope_.data_44100.get());
    
    // Generuj pro 48000 Hz
    generateEnvelopeForSampleRate(48000, false, temp_data, release_envelope_.len_48000);
    release_envelope_.data_48000 = std::make_unique<float[]>(release_envelope_.len_48000);
    std::copy(temp_data.begin(), temp_data.end(), release_envelope_.data_48000.get());
}

/**
 * @brief Generuje envelope data pro daný sample rate
 */
void Envelope::generateEnvelopeForSampleRate(int sampleRate, bool isAttack, 
                                           std::vector<float>& output, int& numSamples) const {
    float time_ms = isAttack ? attack_time_ms_ : release_time_ms_;
    float time_seconds = time_ms / 1000.0f;
    
    // Počet samples pro daný čas
    numSamples = static_cast<int>(sampleRate * time_seconds);
    numSamples = std::max(1, numSamples);  // Min 1 sample
    
    output.resize(numSamples);
    
    // Exponenciální křivka s tau = time_seconds / 3 (cca 95% za daný čas)
    float tau = time_seconds / 3.0f;
    
    for (int i = 0; i < numSamples; ++i) {
        float t = (static_cast<float>(i) / static_cast<float>(numSamples - 1)) * time_seconds;
        
        if (isAttack) {
            // Attack: 0 → 1 exponenciálně
            output[i] = 1.0f - std::exp(-t / tau);
        } else {
            // Release: 1 → 0 exponenciálně
            output[i] = std::exp(-t / tau);
        }
        
        // Clamp na [0, 1]
        output[i] = std::max(0.0f, std::min(1.0f, output[i]));
    }
    
    // Truncate na threshold
    int converge_idx = -1;
    for (int i = 0; i < numSamples; ++i) {
        if (isAttack) {
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
        numSamples = converge_idx + 1;
        output.resize(numSamples);
    }
}

/**
 * @brief Helper: Vyplní buffer s envelope daty
 */
void Envelope::fillBufferFromFixedData(const FixedEnvelopeData& env, float* gainBuffer, 
                                      int numSamples, sf_count_t position, bool isAttack) const noexcept {
    // Vyber správná data podle sample rate
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
        // Speciální případ pro velmi krátké envelope
        float constant_value = isAttack ? 1.0f : 0.0f;
        std::fill(gainBuffer, gainBuffer + numSamples, constant_value);
        return;
    }
    
    // RT-safe vyplnění s clampem
    for (int i = 0; i < numSamples; ++i) {
        sf_count_t idx = position + static_cast<sf_count_t>(i);
        
        // Clamp idx na [0, len-1]
        if (idx >= len) {
            // Po konci envelope: konstantní target hodnota
            gainBuffer[i] = isAttack ? 1.0f : 0.0f;
        } else {
            gainBuffer[i] = src_data[static_cast<int>(idx)];
        }
    }
}