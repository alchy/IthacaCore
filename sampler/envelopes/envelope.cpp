#include "envelope.h"
#include <cstdlib>    // pro exit()
#include <iostream>   // pro error reporting
#include <cstring>    // pro memset
#include <sstream>    // pro stringstream
#include <iomanip>    // pro setprecision

// Definice sf_count_t pokud není definovaná jinde
#ifndef sf_count_t
typedef long long sf_count_t;
#endif

// Pomocná funkce pro konverzi čísla na string (pro starší C++ standardy)
template<typename T>
std::string to_string_helper(T value) {
    std::stringstream ss;
    ss << value;
    return ss.str();
}

// Forward declaration - Logger se správným API podle vašeho headeru
class Logger {
public:
    void log(const std::string& component, const std::string& severity, const std::string& message);
};

Envelope::Envelope(Logger& logger) 
    : bitrate_(0), sample_rate_index_(-1) {
    logger.log("Envelope/constructor", "info", "Envelope initialized. Ready to generate attack/release data for MIDI 0-127.");
    logger.log("Envelope/constructor", "info", "Supported sample rates: 44100 Hz (index=0), 48000 Hz (index=1).");
    logger.log("Envelope/constructor", "info", "Data will be generated dynamically - no pre-computed arrays needed.");
    logger.log("Envelope/constructor", "warn", "Frequency not set (bitrate=0, index=-1) - call setEnvelopeFrequency().");
}

void Envelope::setEnvelopeFrequency(int freq, Logger& logger) {
    if (freq == 44100) {
        bitrate_ = freq;
        sample_rate_index_ = 0;
        logger.log("Envelope/setEnvelopeFrequency", "info", "Bitrate changed to 44100 Hz (index=0)");
    } else if (freq == 48000) {
        bitrate_ = freq;
        sample_rate_index_ = 1;
        logger.log("Envelope/setEnvelopeFrequency", "info", "Bitrate changed to 48000 Hz (index=1)");
    } else {
        logger.log("Envelope/setEnvelopeFrequency", "error", "Unsupported sample rate: " + to_string_helper(freq) + " Hz");
        logger.log("Envelope/setEnvelopeFrequency", "error", "Only 44100 Hz and 48000 Hz are supported. Exiting...");
        std::exit(1);
    }
}

double Envelope::calculateTau(uint8_t midi) const noexcept {
    if (midi == 0) {
        return 0.0;
    }
    return (static_cast<double>(midi) / 127.0) * (TOTAL_DURATION / TAU_DIVISOR);
}

int Envelope::calculateEnvelopeLength(uint8_t midi, int sample_rate, bool is_attack) const noexcept {
    const double tau = calculateTau(midi);
    
    if (midi == 0) {
        return 1;  // Okamžitá změna
    }
    
    // Vypočítej stabilní čas kde křivka konverguje (t_stable = -tau * log(threshold))
    const double log_threshold = -std::log(CONVERGENCE_THRESHOLD);  // ≈4.605 pro threshold=0.01
    double t_stable = tau * log_threshold;
    
    // Omeď na TOTAL_DURATION
    t_stable = std::min(t_stable, TOTAL_DURATION);
    
    // Počet vzorků: sample_rate * t_stable, zaokrouhleno nahoru s bezpečnostní rezervou
    int num_samples = std::max(1, static_cast<int>(sample_rate * t_stable) + 1);
    
    // Bezpečnostní limit (odpovídá MAX_LEN z Pythonu)
    const int MAX_LEN = static_cast<int>(48000 * TOTAL_DURATION) + 1;
    return std::min(num_samples, MAX_LEN);
}

void Envelope::generateEnvelopeData(uint8_t midi, int sample_rate, bool is_attack, 
                                  float* buffer, int buffer_size, int& actual_length) const noexcept {
    if (!buffer || buffer_size <= 0) {
        actual_length = 0;
        return;
    }
    
    const double tau = calculateTau(midi);
    const double target_val = is_attack ? 1.0 : 0.0;
    
    // Pro MIDI 0: Okamžitá změna na cílovou hodnotu
    if (midi == 0) {
        buffer[0] = static_cast<float>(target_val);
        actual_length = 1;
        return;
    }
    
    // Vypočítej optimální délku obálky
    const int optimal_length = calculateEnvelopeLength(midi, sample_rate, is_attack);
    actual_length = std::min(optimal_length, buffer_size);
    
    // Generuj data
    for (int i = 0; i < actual_length; ++i) {
        const double t = (static_cast<double>(i) / sample_rate) * (TOTAL_DURATION * actual_length) / optimal_length;
        buffer[i] = calculateEnvelopeSample(t, tau, is_attack);
        
        // Clamp na 0-1 rozsah
        buffer[i] = std::max(0.0f, std::min(1.0f, buffer[i]));
    }
    
    // Zkontroluj konvergenci a případně zkrať
    if (is_attack) {
        // Pro attack: hledáme kde dosáhneme (1 - threshold) = 0.99
        for (int i = 0; i < actual_length; ++i) {
            if (buffer[i] >= (1.0f - CONVERGENCE_THRESHOLD)) {
                actual_length = i + 1;
                break;
            }
        }
    } else {
        // Pro release: hledáme kde klesne pod threshold = 0.01
        for (int i = 0; i < actual_length; ++i) {
            if (buffer[i] <= CONVERGENCE_THRESHOLD) {
                actual_length = i + 1;
                break;
            }
        }
    }
}

void Envelope::getGainBufferAttack(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed, Logger& logger) const noexcept {
    if (!gainBuffer || numSamples <= 0 || sample_rate_index_ < 0) {
        // Fallback při chybě
        if (gainBuffer && numSamples > 0) {
            std::memset(gainBuffer, 0, numSamples * sizeof(float));
        }
        return;
    }
    
    const int sample_rate = SAMPLE_RATES[sample_rate_index_];
    const double tau = calculateTau(midi);
    
    // Pro MIDI 0: Konstantní hodnota 1.0f
    if (midi == 0) {
        for (int i = 0; i < numSamples; ++i) {
            gainBuffer[i] = 1.0f;
        }
        // Debug logování
        logRuntimeBuffer("Attack", midi, gainBuffer, numSamples, start_elapsed, logger);
        return;
    }
    
    // Generuj data pro požadovaný úsek
    for (int i = 0; i < numSamples; ++i) {
        const sf_count_t sample_index = start_elapsed + i;
        const double t = static_cast<double>(sample_index) / sample_rate;
        
        // Zkontroluj, jestli jsme mimo platnou oblast obálky
        const double max_time = tau * (-std::log(CONVERGENCE_THRESHOLD));
        if (t >= std::min(max_time, TOTAL_DURATION)) {
            gainBuffer[i] = 1.0f;  // Attack dokončen
        } else {
            gainBuffer[i] = calculateEnvelopeSample(t, tau, true);
            gainBuffer[i] = std::max(0.0f, std::min(1.0f, gainBuffer[i]));
        }
    }
    
    // Debug logování
    logRuntimeBuffer("Attack", midi, gainBuffer, numSamples, start_elapsed, logger);
}

void Envelope::getGainBufferRelease(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed, Logger& logger) const noexcept {
    if (!gainBuffer || numSamples <= 0 || sample_rate_index_ < 0) {
        // Fallback při chybě
        if (gainBuffer && numSamples > 0) {
            std::memset(gainBuffer, 0, numSamples * sizeof(float));
        }
        return;
    }
    
    const int sample_rate = SAMPLE_RATES[sample_rate_index_];
    const double tau = calculateTau(midi);
    
    // Pro MIDI 0: Konstantní hodnota 0.0f
    if (midi == 0) {
        std::memset(gainBuffer, 0, numSamples * sizeof(float));
        // Debug logování
        logRuntimeBuffer("Release", midi, gainBuffer, numSamples, start_elapsed, logger);
        return;
    }
    
    // Generuj data pro požadovaný úsek
    for (int i = 0; i < numSamples; ++i) {
        const sf_count_t sample_index = start_elapsed + i;
        const double t = static_cast<double>(sample_index) / sample_rate;
        
        // Zkontroluj, jestli jsme mimo platnou oblast obálky
        const double max_time = tau * (-std::log(CONVERGENCE_THRESHOLD));
        if (t >= std::min(max_time, TOTAL_DURATION)) {
            gainBuffer[i] = 0.0f;  // Release dokončen
        } else {
            gainBuffer[i] = calculateEnvelopeSample(t, tau, false);
            gainBuffer[i] = std::max(0.0f, std::min(1.0f, gainBuffer[i]));
        }
    }
    
    // Debug logování
    logRuntimeBuffer("Release", midi, gainBuffer, numSamples, start_elapsed, logger);
}

// Pomocné metody pro debug logování
std::string Envelope::formatValuesForLog(const float* values, int count) const noexcept {
    if (!values || count <= 0) {
        return "[]";
    }
    
    std::stringstream ss;
    ss << std::fixed << std::setprecision(3);
    ss << "[";
    
    for (int i = 0; i < count; ++i) {
        if (i > 0) ss << ", ";
        ss << std::setw(5) << values[i];  // Zarovnání na 5 znaků pro konzistentní sloupce
    }
    
    ss << "]";
    return ss.str();
}

void Envelope::logEnvelopeData(const std::string& envelope_type, int sample_rate, uint8_t midi, 
                              const float* data, int length, Logger& logger) const noexcept {
    if (!data || length <= 0) {
        return;
    }
    
    const std::string rate_str = "(" + to_string_helper(sample_rate) + " Hz)";
    const std::string midi_str = "value[" + to_string_helper(static_cast<int>(midi)) + "]";
    
    // První 4 hodnoty
    const int first_count = std::min(4, length);
    std::string first_values = formatValuesForLog(data, first_count);
    std::string log_msg = envelope_type + " envelope " + rate_str + " for MIDI " + midi_str + " = " + first_values;
    
    // Hodnoty v polovině (pokud délka > 8)
    if (length > 8) {
        const int mid_index = length / 2;
        const int mid_count = std::min(4, length - mid_index);
        std::string mid_values = formatValuesForLog(data + mid_index, mid_count);
        log_msg += " ... " + mid_values;
    }
    
    // Poslední 4 hodnoty (pokud jsou jiné než první)
    if (length > 4) {
        const int last_start = std::max(0, length - 4);
        const int last_count = length - last_start;
        if (last_start >= 4) {  // Pouze pokud se nepřekrývají s prvními
            std::string last_values = formatValuesForLog(data + last_start, last_count);
            log_msg += " ... " + last_values;
        }
    }
    
    logger.log("Envelope/generateEnvelopeData", "debug", log_msg);
}

void Envelope::logRuntimeBuffer(const std::string& envelope_type, uint8_t midi, 
                               const float* buffer, int numSamples, sf_count_t start_elapsed, 
                               Logger& logger) const noexcept {
    if (!buffer || numSamples <= 0) {
        return;
    }
    
    const std::string rate_str = "(" + to_string_helper(SAMPLE_RATES[sample_rate_index_]) + " Hz)";
    const std::string midi_str = "value[" + to_string_helper(static_cast<int>(midi)) + "]";
    const std::string elapsed_str = "elapsed[" + to_string_helper(start_elapsed) + "]";
    
    // Hodnoty v polovině bloku
    const int mid_index = numSamples / 2;
    const int sample_count = std::min(4, numSamples - mid_index);
    std::string values = formatValuesForLog(buffer + mid_index, sample_count);
    
    std::string log_msg = envelope_type + " runtime " + rate_str + " for MIDI " + midi_str + 
                         " " + elapsed_str + " = " + values;
    
    logger.log("Envelope/getGainBuffer" + envelope_type, "debug", log_msg);
}