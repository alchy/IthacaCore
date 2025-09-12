#include "envelope.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdlib>  // Pro std::exit
#include <atomic>
#include <vector>

// @brief Statická inicializace RT módu
std::atomic<bool> Envelope::rtMode_{false};

// @brief Konstruktor - inicializace polí a výchozích hodnot
Envelope::Envelope()
    : sample_rate_index_(SAMPLE_RATE_INDEX_INVALID)
    , current_sample_rate_(0)
    , attack_midi_index_(64)
    , release_midi_index_(64)
    , sustain_level_(0.7f)
{
    for (int sr_idx = 0; sr_idx < NUM_SAMPLE_RATES; ++sr_idx) {
        for (int midi = 0; midi <= MAX_MIDI; ++midi) {
            attack_index_[sr_idx][midi] = {nullptr, 0};
            release_index_[sr_idx][midi] = {nullptr, 0};
        }
    }
}

// @brief Nastavení callbacku pro hlášení chyb
void Envelope::setErrorCallback(ErrorCallback callback) {
    errorCallback_ = callback;
}

// @brief Interní hlášení chyby (non-RT)
void Envelope::reportError(const std::string& component, const std::string& severity, const std::string& message) const {
    if (!rtMode_.load() && errorCallback_) {
        errorCallback_(component, severity, message);
    }
}

// @brief Hlášení chyby a ukončení programu
void Envelope::exitOnError(const std::string& component, const std::string& severity, const std::string& message) const {
    reportError(component, "error", message + ". Terminating.");
    std::exit(1);
}

// @brief Inicializace obálek pro všechny vzorkovací frekvence
bool Envelope::initialize(Logger& logger) {
    logSafe("Envelope/initialize", "info", "Starting envelope generation for both sample rates", logger);

    if (attack_index_[SAMPLE_RATE_INDEX_44100][0].data != nullptr ||
        attack_index_[SAMPLE_RATE_INDEX_48000][0].data != nullptr) {
        logSafe("Envelope/initialize", "warning", "Envelope already initialized, reinitializing", logger);
    }

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
                    logSafe("Envelope/initialize", "error",
                           "Failed to initialize envelope for MIDI " + std::to_string(midi) +
                           " at " + std::to_string(SAMPLE_RATES[sr_idx]) + " Hz", logger);
                    success = false;
                }
            }
        }

        if (!success) {
            logSafe("Envelope/initialize", "error",
                   "Envelope initialization incomplete. Terminating.", logger);
            std::exit(1);
        }

        logSafe("Envelope/initialize", "info",
               "Envelope initialization completed successfully", logger);
        return true;

    } catch (const std::exception& e) {
        logSafe("Envelope/initialize", "error",
               "Failed to initialize envelopes: " + std::string(e.what()) + ". Terminating.", logger);
        std::exit(1);
    } catch (...) {
        logSafe("Envelope/initialize", "error",
               "Failed to initialize envelopes: unknown error. Terminating.", logger);
        std::exit(1);
    }
}

// @brief Nastavení vzorkovací frekvence
bool Envelope::setEnvelopeFrequency(int sampleRate, Logger& logger) {
    const int sr_index = getSampleRateIndex(sampleRate);
    if (!isValidSampleRateIndex(sr_index)) {
        logSafe("Envelope/setEnvelopeFrequency", "error",
               "Invalid sample rate " + std::to_string(sampleRate) +
               " Hz. Supported: 44100, 48000", logger);
        return false;
    }

    sample_rate_index_ = sr_index;
    current_sample_rate_ = sampleRate;
    logSafe("Envelope/setEnvelopeFrequency", "info",
           "Sample rate set to " + std::to_string(sampleRate) + " Hz", logger);
    return true;
}

// @brief Kopírování attack dat do bufferu
bool Envelope::getAttackGains(float* gain_buffer, int num_samples, int envelope_attack_position) const {
    // RT-SAFE: Kontroly vstupů
    if (!gain_buffer || num_samples <= 0) return false;

    if (!isValidSampleRateIndex(sample_rate_index_)) {
        exitOnError("Envelope/getAttackGains", "error",
            "Sample rate not set (call setEnvelopeFrequency first). Terminating.");
    }

    const EnvelopeIndex& envelope_idx = attack_index_[sample_rate_index_][attack_midi_index_];
    if (!envelope_idx.data || envelope_idx.length == 0) {
        exitOnError("Envelope/getAttackGains", "error",
               "Attack envelope data not initialized (call initialize first). Terminating.");
    }

    const float* data = envelope_idx.data;
    const int envelope_length = envelope_idx.length;
    bool continues = true;

    for (int i = 0; i < num_samples; ++i) {
        const int pos = envelope_attack_position + i;
        if (pos < envelope_length) {
            gain_buffer[i] = data[pos];
        } else {
            gain_buffer[i] = 1.0f;
            continues = false;
        }
    }

    return continues;
}

// @brief Kopírování release dat do bufferu
bool Envelope::getReleaseGains(float* gain_buffer, int num_samples, int envelope_release_position) const {
    // RT-SAFE: Kontroly vstupů
    if (!gain_buffer || num_samples <= 0) return false;

    if (!isValidSampleRateIndex(sample_rate_index_)) {
        exitOnError("Envelope/getReleaseGains", "error",
               "Sample rate not set (call setEnvelopeFrequency first). Terminating.");
    }

    const EnvelopeIndex& envelope_idx = release_index_[sample_rate_index_][release_midi_index_];
    if (!envelope_idx.data || envelope_idx.length == 0) {
        exitOnError("Envelope/getReleaseGains", "error",
               "Release envelope data not initialized (call initialize first). Terminating.");
    }

    const float* data = envelope_idx.data;
    const int envelope_length = envelope_idx.length;
    bool continues = true;

    for (int i = 0; i < num_samples; ++i) {
        const int pos = envelope_release_position + i;
        if (pos < envelope_length) {
            gain_buffer[i] = data[pos];
        } else {
            gain_buffer[i] = 0.0f;
            continues = false;
        }
    }

    return continues;
}

// @brief Nastavení MIDI indexu pro attack
void Envelope::setAttackMIDI(uint8_t midi_value) {
    if (!isValidSampleRateIndex(sample_rate_index_)) {
        exitOnError("Envelope/setAttackMIDI", "error",
               "Cannot set attack MIDI before setting sample rate (call setEnvelopeFrequency first). Terminating.");
    }

    bool buffers_empty = true;
    for (int sr_idx = 0; sr_idx < NUM_SAMPLE_RATES; ++sr_idx) {
        if (!attack_buffer_[sr_idx].empty()) { buffers_empty = false; break; }
    }

    if (buffers_empty) {
        exitOnError("Envelope/setAttackMIDI", "error",
               "Cannot set attack MIDI before initialization (call initialize first). Terminating.");
    }

    attack_midi_index_ = std::min(midi_value, static_cast<uint8_t>(MAX_MIDI));
}

// @brief Nastavení MIDI indexu pro release
void Envelope::setReleaseMIDI(uint8_t midi_value) {
    if (!isValidSampleRateIndex(sample_rate_index_)) {
        exitOnError("Envelope/setReleaseMIDI", "error",
               "Cannot set release MIDI before setting sample rate (call setEnvelopeFrequency first). Terminating.");
    }

    bool buffers_empty = true;
    for (int sr_idx = 0; sr_idx < NUM_SAMPLE_RATES; ++sr_idx) {
        if (!release_buffer_[sr_idx].empty()) { buffers_empty = false; break; }
    }

    if (buffers_empty) {
        exitOnError("Envelope/setReleaseMIDI", "error",
               "Cannot set release MIDI before initialization (call initialize first). Terminating.");
    }

    release_midi_index_ = std::min(midi_value, static_cast<uint8_t>(MAX_MIDI));
}

// @brief Nastavení sustain úrovně dle MIDI
void Envelope::setSustainLevelMIDI(uint8_t midi_value) {
    sustain_level_ = std::clamp(static_cast<float>(midi_value) / 127.0f, 0.0f, 1.0f);
}

// @brief Získání sustain úrovně
float Envelope::getSustainLevel() const { return sustain_level_; }

// @brief Získání délky attack fáze v ms
float Envelope::getAttackLength() const {
    if (!isValidSampleRateIndex(sample_rate_index_) || current_sample_rate_ <= 0) {
        exitOnError("Envelope/getAttackLength", "error",
               "Sample rate not set or envelope not initialized. Terminating.");
    }

    const EnvelopeIndex& envelope_idx = attack_index_[sample_rate_index_][attack_midi_index_];
    if (envelope_idx.data == nullptr) {
        exitOnError("Envelope/getAttackLength", "error",
               "Attack envelope not initialized for " + std::to_string(current_sample_rate_) +
               " Hz. Terminating.");
    }

    return (static_cast<float>(envelope_idx.length) / static_cast<float>(current_sample_rate_)) * 1000.0f;
}

// @brief Získání délky release fáze v ms
float Envelope::getReleaseLength() const {
    if (!isValidSampleRateIndex(sample_rate_index_) || current_sample_rate_ <= 0) {
        exitOnError("Envelope/getReleaseLength", "error",
               "Sample rate not set or envelope not initialized. Terminating.");
    }

    const EnvelopeIndex& envelope_idx = release_index_[sample_rate_index_][release_midi_index_];
    if (envelope_idx.data == nullptr) {
        exitOnError("Envelope/getReleaseLength", "error",
               "Release envelope not initialized for " + std::to_string(current_sample_rate_) +
               " Hz. Terminating.");
    }

    return (static_cast<float>(envelope_idx.length) / static_cast<float>(current_sample_rate_)) * 1000.0f;
}

// @brief Zapnutí/vypnutí RT módu
void Envelope::setRTMode(bool enabled) { rtMode_.store(enabled); }

// ===== PRIVATE METODY =====

int Envelope::getSampleRateIndex(int sampleRate) const {
    for (int i = 0; i < NUM_SAMPLE_RATES; ++i) {
        if (SAMPLE_RATES[i] == sampleRate) return i;
    }
    return SAMPLE_RATE_INDEX_INVALID;
}

bool Envelope::isValidSampleRateIndex(int index) const {
    return index >= 0 && index < NUM_SAMPLE_RATES;
}

// @brief Výpočet časové konstanty tau
float Envelope::calculateTau(uint8_t midi) const {
    if (midi == 0) return 0.0f;
    return (static_cast<float>(midi) / 127.0f) * (TOTAL_DURATION / TAU_DIVISOR);
}

// @brief Generování obálek pro danou vzorkovací frekvenci
void Envelope::generateEnvelopeForSampleRate(int sampleRate, Logger& logger) {
    const int sr_index = getSampleRateIndex(sampleRate);
    if (!isValidSampleRateIndex(sr_index)) {
        logSafe("Envelope/generateEnvelopeForSampleRate", "error",
               "Unsupported sample rate: " + std::to_string(sampleRate) + ". Terminating.", logger);
        std::exit(1);
    }

    logSafe("Envelope/generateEnvelopeForSampleRate", "info",
           "Generating envelopes for " + std::to_string(sampleRate) + " Hz", logger);

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
        logSafe("Envelope/generateEnvelopeForSampleRate", "error",
               "Exception during envelope generation: " + std::string(e.what()) + ". Terminating.", logger);
        std::exit(1);
    } catch (...) {
        logSafe("Envelope/generateEnvelopeForSampleRate", "error",
               "Unknown error during envelope generation. Terminating.", logger);
        std::exit(1);
    }

    logSafe("Envelope/generateEnvelopeForSampleRate", "info",
           "Completed envelope generation for " + std::to_string(sampleRate) +
           " Hz (128 MIDI values, 2 types). Total attack samples: " + std::to_string(attack_buffer.size()) +
           ", total release samples: " + std::to_string(release_buffer.size()), logger);
}

// @brief Generování jedné obálky (attack/release)
std::pair<std::vector<float>, int> Envelope::generateSingleEnvelope(uint8_t midi, int sampleRate, const std::string& envelope_type) const {
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

    // ===== OPRAVENO: Správný výpočet času t podle np.linspace(0, t_stable, num_samples)
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

// @brief Logování dat obálky (begin / half / end)
void Envelope::logEnvelopeData(const std::vector<float>& data, const std::string& type,
                              int sampleRate, uint8_t midi_value, Logger& logger) const {
    if (data.empty()) return;

    const int size = static_cast<int>(data.size());
    const std::string component = "Envelope/generate";

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
    logSafe(component, "debug", begin_msg.str(), logger);

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
        logSafe(component, "debug", half_msg.str(), logger);
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
        logSafe(component, "debug", end_msg.str(), logger);
    }
}

// @brief Logování blokových gain hodnot (pro debug z get*Gains)
void Envelope::logGainsData(const float* buffer, int num_samples, const std::string& type,
                           int position, int sampleRate, Logger& logger) const {
    if (!buffer || num_samples <= 0) return;

    const std::string component = "Envelope/get" + type + "Gains";

    auto formatFloat = [](float val) -> std::string {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(6) << val;
        return oss.str();
    };

    const std::string params = " (position: " + std::to_string(position) +
                              ", numSamples: " + std::to_string(num_samples) +
                              ", sampleRate: " + std::to_string(sampleRate) + ")";

    // Prvních 4 hodnot blok
    const int begin_count = std::min(4, num_samples);
    std::ostringstream begin_msg;
    begin_msg << "block begin: [";
    for (int i = 0; i < begin_count; ++i) {
        if (i > 0) begin_msg << ", ";
        begin_msg << formatFloat(buffer[i]);
    }
    begin_msg << "]" << params;
    logSafe(component, "debug", begin_msg.str(), logger);

    // Střed blok
    if (num_samples > 8) {
        const int half_start = num_samples / 2 - 2;
        const int half_count = std::min(4, num_samples - half_start);
        std::ostringstream half_msg;
        half_msg << "block half: [";
        for (int i = 0; i < half_count; ++i) {
            if (i > 0) half_msg << ", ";
            half_msg << formatFloat(buffer[half_start + i]);
        }
        half_msg << "]";
        logSafe(component, "debug", half_msg.str(), logger);
    }

    // Konec bloku
    if (num_samples > 4) {
        const int end_start = std::max(0, num_samples - 4);
        const int end_count = num_samples - end_start;
        std::ostringstream end_msg;
        end_msg << "block end: [";
        for (int i = 0; i < end_count; ++i) {
            if (i > 0) end_msg << ", ";
            end_msg << formatFloat(buffer[end_start + i]);
        }
        end_msg << "]";
        logSafe(component, "debug", end_msg.str(), logger);
    }
}

// @brief Bezpečné logování pouze v non-RT režimu
void Envelope::logSafe(const std::string& component, const std::string& severity,
                      const std::string& message, Logger& logger) const {
    if (!rtMode_.load()) {
        logger.log(component, severity, message);
    }
}
