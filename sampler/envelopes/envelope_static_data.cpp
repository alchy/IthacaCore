#include "envelope_static_data.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <vector>

// Statická inicializace členů
std::vector<float> EnvelopeStaticData::attack_buffer_[NUM_SAMPLE_RATES];
std::vector<float> EnvelopeStaticData::release_buffer_[NUM_SAMPLE_RATES];
EnvelopeStaticData::EnvelopeIndex EnvelopeStaticData::attack_index_[NUM_SAMPLE_RATES][128];
EnvelopeStaticData::EnvelopeIndex EnvelopeStaticData::release_index_[NUM_SAMPLE_RATES][128];
std::atomic<bool> EnvelopeStaticData::initialized_{false};
EnvelopeStaticData::ErrorCallback EnvelopeStaticData::errorCallback_;

// ===== PUBLIC API =====

bool EnvelopeStaticData::initialize(Logger& logger) {
    if (initialized_.load()) {
        logger.log("EnvelopeStaticData/initialize", "warning", 
                  "Already initialized, skipping");
        return true;
    }

    logger.log("EnvelopeStaticData/initialize", "info", 
              "Starting global envelope generation for all sample rates");

    try {
        // Generuj obálky pro obě podporované vzorkovací frekvence
        generateEnvelopeForSampleRate(SAMPLE_RATES[SAMPLE_RATE_INDEX_44100], logger);
        generateEnvelopeForSampleRate(SAMPLE_RATES[SAMPLE_RATE_INDEX_48000], logger);

        // Validace inicializace
        bool success = true;
        for (int sr_idx = 0; sr_idx < NUM_SAMPLE_RATES; ++sr_idx) {
            for (int midi = 0; midi <= MAX_MIDI; ++midi) {
                if (attack_index_[sr_idx][midi].data == nullptr ||
                    attack_index_[sr_idx][midi].length == 0 ||
                    release_index_[sr_idx][midi].data == nullptr ||
                    release_index_[sr_idx][midi].length == 0) {
                    logger.log("EnvelopeStaticData/initialize", "error",
                              "Failed to initialize envelope for MIDI " + std::to_string(midi) +
                              " at " + std::to_string(SAMPLE_RATES[sr_idx]) + " Hz");
                    success = false;
                }
            }
        }

        if (!success) {
            logger.log("EnvelopeStaticData/initialize", "error",
                      "Envelope initialization incomplete. Terminating.");
            std::exit(1);
        }

        initialized_.store(true);
        
        // Log memory usage statistics
        size_t totalMemory = 0;
        for (int sr_idx = 0; sr_idx < NUM_SAMPLE_RATES; ++sr_idx) {
            totalMemory += attack_buffer_[sr_idx].size() * sizeof(float);
            totalMemory += release_buffer_[sr_idx].size() * sizeof(float);
        }
        
        logger.log("EnvelopeStaticData/initialize", "info",
                  "Global envelope initialization completed successfully. "
                  "Memory usage: " + std::to_string(totalMemory / 1024 / 1024) + " MB");
        return true;

    } catch (const std::exception& e) {
        logger.log("EnvelopeStaticData/initialize", "error",
                  "Failed to initialize static envelopes: " + std::string(e.what()) + ". Terminating.");
        std::exit(1);
    } catch (...) {
        logger.log("EnvelopeStaticData/initialize", "error",
                  "Failed to initialize static envelopes: unknown error. Terminating.");
        std::exit(1);
    }
}

void EnvelopeStaticData::cleanup() noexcept {
    if (!initialized_.load()) {
        return;
    }
    
    // Vyčisti všechny buffery
    for (int sr_idx = 0; sr_idx < NUM_SAMPLE_RATES; ++sr_idx) {
        attack_buffer_[sr_idx].clear();
        attack_buffer_[sr_idx].shrink_to_fit();
        release_buffer_[sr_idx].clear();
        release_buffer_[sr_idx].shrink_to_fit();
        
        // Reset indexů
        for (int midi = 0; midi <= MAX_MIDI; ++midi) {
            attack_index_[sr_idx][midi].data = nullptr;
            attack_index_[sr_idx][midi].length = 0;
            release_index_[sr_idx][midi].data = nullptr;
            release_index_[sr_idx][midi].length = 0;
        }
    }
    
    initialized_.store(false);
}

bool EnvelopeStaticData::getAttackGains(float* gainBuffer, int numSamples, int position,
                                       uint8_t midiValue, int sampleRate) noexcept {
    // RT-SAFE: Kontroly vstupů
    if (!gainBuffer || numSamples <= 0) return false;
    
    if (!initialized_.load()) {
        exitOnError("EnvelopeStaticData/getAttackGains", "error",
                   "Static envelope data not initialized");
        return false;
    }
    
    if (!isValidMidiValue(midiValue)) {
        exitOnError("EnvelopeStaticData/getAttackGains", "error",
                   "Invalid MIDI value " + std::to_string(midiValue));
        return false;
    }
    
    const int sr_index = getSampleRateIndex(sampleRate);
    if (!isValidSampleRateIndex(sr_index)) {
        exitOnError("EnvelopeStaticData/getAttackGains", "error",
                   "Invalid sample rate " + std::to_string(sampleRate));
        return false;
    }
    
    const EnvelopeIndex& envelope_idx = attack_index_[sr_index][midiValue];
    if (!envelope_idx.data || envelope_idx.length == 0) {
        exitOnError("EnvelopeStaticData/getAttackGains", "error",
                   "Attack envelope data corrupted");
        return false;
    }
    
    const float* data = envelope_idx.data;
    const int envelope_length = envelope_idx.length;
    bool continues = true;
    
    for (int i = 0; i < numSamples; ++i) {
        const int pos = position + i;
        if (pos < envelope_length) {
            gainBuffer[i] = data[pos];
        } else {
            gainBuffer[i] = 1.0f;
            continues = false;
        }
    }
    
    return continues;
}

bool EnvelopeStaticData::getReleaseGains(float* gainBuffer, int numSamples, int position,
                                        uint8_t midiValue, int sampleRate) noexcept {
    // RT-SAFE: Kontroly vstupů
    if (!gainBuffer || numSamples <= 0) return false;
    
    if (!initialized_.load()) {
        exitOnError("EnvelopeStaticData/getReleaseGains", "error",
                   "Static envelope data not initialized");
        return false;
    }
    
    if (!isValidMidiValue(midiValue)) {
        exitOnError("EnvelopeStaticData/getReleaseGains", "error",
                   "Invalid MIDI value " + std::to_string(midiValue));
        return false;
    }
    
    const int sr_index = getSampleRateIndex(sampleRate);
    if (!isValidSampleRateIndex(sr_index)) {
        exitOnError("EnvelopeStaticData/getReleaseGains", "error",
                   "Invalid sample rate " + std::to_string(sampleRate));
        return false;
    }
    
    const EnvelopeIndex& envelope_idx = release_index_[sr_index][midiValue];
    if (!envelope_idx.data || envelope_idx.length == 0) {
        exitOnError("EnvelopeStaticData/getReleaseGains", "error",
                   "Release envelope data corrupted");
        return false;
    }
    
    const float* data = envelope_idx.data;
    const int envelope_length = envelope_idx.length;
    bool continues = true;
    
    for (int i = 0; i < numSamples; ++i) {
        const int pos = position + i;
        if (pos < envelope_length) {
            gainBuffer[i] = data[pos];
        } else {
            gainBuffer[i] = 0.0f;
            continues = false;
        }
    }
    
    return continues;
}

float EnvelopeStaticData::getAttackLength(uint8_t midiValue, int sampleRate) noexcept {
    if (!initialized_.load() || !isValidMidiValue(midiValue)) {
        return 0.0f;
    }
    
    const int sr_index = getSampleRateIndex(sampleRate);
    if (!isValidSampleRateIndex(sr_index)) {
        return 0.0f;
    }
    
    const EnvelopeIndex& envelope_idx = attack_index_[sr_index][midiValue];
    if (envelope_idx.data == nullptr) {
        return 0.0f;
    }
    
    return (static_cast<float>(envelope_idx.length) / static_cast<float>(sampleRate)) * 1000.0f;
}

float EnvelopeStaticData::getReleaseLength(uint8_t midiValue, int sampleRate) noexcept {
    if (!initialized_.load() || !isValidMidiValue(midiValue)) {
        return 0.0f;
    }
    
    const int sr_index = getSampleRateIndex(sampleRate);
    if (!isValidSampleRateIndex(sr_index)) {
        return 0.0f;
    }
    
    const EnvelopeIndex& envelope_idx = release_index_[sr_index][midiValue];
    if (envelope_idx.data == nullptr) {
        return 0.0f;
    }
    
    return (static_cast<float>(envelope_idx.length) / static_cast<float>(sampleRate)) * 1000.0f;
}

void EnvelopeStaticData::setErrorCallback(ErrorCallback callback) {
    errorCallback_ = callback;
}

// ===== PRIVATE IMPLEMENTACE =====

float EnvelopeStaticData::calculateTau(uint8_t midi) noexcept {
    if (midi == 0) return 0.0f;
    return (static_cast<float>(midi) / 127.0f) * (TOTAL_DURATION / TAU_DIVISOR);
}

void EnvelopeStaticData::generateEnvelopeForSampleRate(int sampleRate, Logger& logger) {
    const int sr_index = getSampleRateIndex(sampleRate);
    if (!isValidSampleRateIndex(sr_index)) {
        logger.log("EnvelopeStaticData/generateEnvelopeForSampleRate", "error",
                  "Unsupported sample rate: " + std::to_string(sampleRate) + ". Terminating.");
        std::exit(1);
    }

    logger.log("EnvelopeStaticData/generateEnvelopeForSampleRate", "info",
              "Generating envelopes for " + std::to_string(sampleRate) + " Hz");

    std::vector<float>& attack_buffer = attack_buffer_[sr_index];
    std::vector<float>& release_buffer = release_buffer_[sr_index];
    EnvelopeIndex* attack_index = attack_index_[sr_index];
    EnvelopeIndex* release_index = release_index_[sr_index];

    try {
        // První průchod - vytvoření dočasných vektorů dat
        size_t total_attack_size = 0;
        size_t total_release_size = 0;
        std::vector<std::pair<std::vector<float>, int>> temp_attack_data;
        std::vector<std::pair<std::vector<float>, int>> temp_release_data;
        temp_attack_data.reserve(128);
        temp_release_data.reserve(128);

        for (uint8_t midi = 0; midi <= MAX_MIDI; ++midi) {
            auto attack_result = generateSingleEnvelope(midi, sampleRate, "attack");
            total_attack_size += attack_result.second;
            temp_attack_data.push_back(std::move(attack_result));

            auto release_result = generateSingleEnvelope(midi, sampleRate, "release");
            total_release_size += release_result.second;
            temp_release_data.push_back(std::move(release_result));
        }

        // Alokace souvislých bufferů
        attack_buffer.resize(total_attack_size);
        release_buffer.resize(total_release_size);

        // Druhý průchod - zkopírovat a nastavit indexy
        size_t attack_offset = 0;
        size_t release_offset = 0;

        for (uint8_t midi = 0; midi <= MAX_MIDI; ++midi) {
            const auto& attack_data = temp_attack_data[midi];
            std::copy(attack_data.first.begin(), attack_data.first.end(),
                     attack_buffer.data() + attack_offset);
            attack_index[midi].data = attack_buffer.data() + attack_offset;
            attack_index[midi].length = attack_data.second;
            attack_offset += attack_data.second;

            const auto& release_data = temp_release_data[midi];
            std::copy(release_data.first.begin(), release_data.first.end(),
                     release_buffer.data() + release_offset);
            release_index[midi].data = release_buffer.data() + release_offset;
            release_index[midi].length = release_data.second;
            release_offset += release_data.second;

            // Logování pro debug (non-RT)
            std::vector<float> attack_log_data(attack_index[midi].data,
                                              attack_index[midi].data + attack_index[midi].length);
            std::vector<float> release_log_data(release_index[midi].data,
                                               release_index[midi].data + release_index[midi].length);
            logEnvelopeData(attack_log_data, "attack", sampleRate, midi, logger);
            logEnvelopeData(release_log_data, "release", sampleRate, midi, logger);
        }

    } catch (const std::exception& e) {
        logger.log("EnvelopeStaticData/generateEnvelopeForSampleRate", "error",
                  "Exception during envelope generation: " + std::string(e.what()) + ". Terminating.");
        std::exit(1);
    } catch (...) {
        logger.log("EnvelopeStaticData/generateEnvelopeForSampleRate", "error",
                  "Unknown error during envelope generation. Terminating.");
        std::exit(1);
    }

    logger.log("EnvelopeStaticData/generateEnvelopeForSampleRate", "info",
              "Completed envelope generation for " + std::to_string(sampleRate) +
              " Hz (128 MIDI values, 2 types). Total attack samples: " + std::to_string(attack_buffer.size()) +
              ", total release samples: " + std::to_string(release_buffer.size()));
}

std::pair<std::vector<float>, int> EnvelopeStaticData::generateSingleEnvelope(
    uint8_t midi, int sampleRate, const std::string& envelope_type) {
    const float tau = calculateTau(midi);
    const float target_val = (envelope_type == "attack") ? 1.0f : 0.0f;

    // Speciální případ pro MIDI 0 (okamžitá změna)
    if (midi == 0) {
        std::vector<float> data(1, target_val);
        return std::make_pair(std::move(data), 1);
    }

    // Výpočet času konvergence (t_stable) podle prahu
    const float log_threshold = -std::log(CONVERGENCE_THRESHOLD); // ~4.605 pro threshold=0.01
    float t_stable = tau * log_threshold;
    t_stable = std::min(t_stable, TOTAL_DURATION);

    const int max_samples = static_cast<int>(sampleRate * TOTAL_DURATION) + 1;
    int num_samples = std::max(2, std::min(static_cast<int>(sampleRate * t_stable) + 1, max_samples));

    std::vector<float> data(num_samples);

    // Správný výpočet času t podle np.linspace(0, t_stable, num_samples)
    for (int i = 0; i < num_samples; ++i) {
        const float t = static_cast<float>(i) * t_stable / static_cast<float>(num_samples - 1);

        if (envelope_type == "attack") {
            data[i] = 1.0f - std::exp(-t / tau);
        } else {
            data[i] = std::exp(-t / tau);
        }

        data[i] = std::max(0.0f, std::min(1.0f, data[i]));
    }

    // Oříznutí po dosažení konvergence (threshold)
    int converge_idx = num_samples;
    if (envelope_type == "attack") {
        for (int i = 0; i < num_samples; ++i) {
            if (data[i] >= (1.0f - CONVERGENCE_THRESHOLD)) {
                converge_idx = i + 1;
                break;
            }
        }
    } else {
        for (int i = 0; i < num_samples; ++i) {
            if (data[i] <= CONVERGENCE_THRESHOLD) {
                converge_idx = i + 1;
                break;
            }
        }
    }

    if (converge_idx < num_samples) {
        data.resize(converge_idx);
        num_samples = converge_idx;
    }

    return std::make_pair(std::move(data), num_samples);
}

int EnvelopeStaticData::getSampleRateIndex(int sampleRate) noexcept {
    for (int i = 0; i < NUM_SAMPLE_RATES; ++i) {
        if (SAMPLE_RATES[i] == sampleRate) return i;
    }
    return SAMPLE_RATE_INDEX_INVALID;
}

bool EnvelopeStaticData::isValidSampleRateIndex(int index) noexcept {
    return index >= 0 && index < NUM_SAMPLE_RATES;
}

bool EnvelopeStaticData::isValidMidiValue(uint8_t midi) noexcept {
    return midi <= MAX_MIDI;
}

void EnvelopeStaticData::reportError(const std::string& component, 
                                   const std::string& severity, 
                                   const std::string& message) noexcept {
    if (errorCallback_) {
        try {
            errorCallback_(component, severity, message);
        } catch (...) {
            // Ignoruj callback chyby v RT kontextu
        }
    }
}

void EnvelopeStaticData::exitOnError(const std::string& component, 
                                   const std::string& severity, 
                                   const std::string& message) noexcept {
    reportError(component, "error", message + ". Terminating.");
    std::exit(1);
}

void EnvelopeStaticData::logEnvelopeData(const std::vector<float>& data, const std::string& type,
                                       int sampleRate, uint8_t midi_value, Logger& logger) {
    if (data.empty()) return;

    const int size = static_cast<int>(data.size());
    const std::string component = "EnvelopeStaticData/generate";

    auto formatFloat = [](float val) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << val;
        return oss.str();
    };

    // Prvních 4 hodnot
    const int begin_count = std::min(4, size);
    std::ostringstream begin_msg;
    begin_msg << type << " (" << sampleRate << " Hz) MIDI[" << static_cast<int>(midi_value) << "] begin: [";
    for (int i = 0; i < begin_count; ++i) {
        if (i > 0) begin_msg << ", ";
        begin_msg << formatFloat(data[i]);
    }
    begin_msg << "]";
    logger.log(component, "debug", begin_msg.str());

    // Střední hodnoty
    if (size > 8) {
        const int half_start = size / 2 - 2;
        const int half_count = std::min(4, size - half_start);
        std::ostringstream half_msg;
        half_msg << type << " (" << sampleRate << " Hz) half: [";
        for (int i = 0; i < half_count; ++i) {
            if (i > 0) half_msg << ", ";
            half_msg << formatFloat(data[half_start + i]);
        }
        half_msg << "]";
        logger.log(component, "debug", half_msg.str());
    }

    // Poslední 4 hodnoty
    if (size > 4) {
        const int end_start = std::max(0, size - 4);
        const int end_count = size - end_start;
        std::ostringstream end_msg;
        end_msg << type << " (" << sampleRate << " Hz) end: [";
        for (int i = 0; i < end_count; ++i) {
            if (i > 0) end_msg << ", ";
            end_msg << formatFloat(data[end_start + i]);
        }
        end_msg << "]";
        logger.log(component, "debug", end_msg.str());
    }
}