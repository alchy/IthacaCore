#include "voice.h"
#include <algorithm>  // Pro std::min a clamp
#include <cmath>      // Pro float výpočty
#include <iostream>
#include <sstream>
#include <iomanip>

// Static member initialization
std::atomic<bool> Voice::rtMode_{false};

// Default konstruktor - AKTUALIZOVÁNA inicializace s envelope positions
Voice::Voice() : midiNote_(0), instrument_(nullptr), sampleRate_(0),
                 state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
                 envelope_gain_(0.0f), velocity_gain_(0.0f), master_gain_(0.8f),
                 envelope_(nullptr),
                 envelope_attack_position_(0), envelope_release_position_(0), 
                 release_start_gain_(1.0f) {
}

// Konstruktor pro VoiceManager (pool mód)
Voice::Voice(uint8_t midiNote)
    : midiNote_(midiNote), instrument_(nullptr), sampleRate_(0),
      state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
      envelope_gain_(0.0f), velocity_gain_(0.0f), master_gain_(1.0f),
      envelope_(nullptr),
      envelope_attack_position_(0), envelope_release_position_(0), 
      release_start_gain_(1.0f) {
}

/**
 * @brief AKTUALIZOVANÁ initialize s envelope validation
 */
void Voice::initialize(const Instrument& instrument, int sampleRate, 
                      const Envelope& envelope, Logger& logger) {
    instrument_ = &instrument;
    sampleRate_ = sampleRate;
    envelope_ = &envelope;
    
    // Validace sampleRate
    if (sampleRate_ <= 0) {
        logSafe("Voice/initialize", "error", 
               "Invalid sampleRate " + std::to_string(sampleRate_) + 
               " - must be > 0. Terminating.", logger);
        std::exit(1);
    }
    
    // Validace envelope
    if (!envelope_) {
        logSafe("Voice/initialize", "error", 
               "Envelope reference is null. Terminating.", logger);
        std::exit(1);
    }
    
    // Reset všech stavů pro čistý start
    state_ = VoiceState::Idle;
    position_ = 0;
    currentVelocityLayer_ = 0;
    envelope_gain_ = 0.0f;
    velocity_gain_ = 0.0f;
    master_gain_ = 1.0f;
    
    // Reset envelope positions
    envelope_attack_position_ = 0;
    envelope_release_position_ = 0;

    // Reset release start gain pro čistý stav
    release_start_gain_ = 1.0f;
    
    // gain buffer s dostatecnou rezervou bloku
    gainBuffer_.reserve(32767);
    
    logSafe("Voice/initialize", "info", 
           "Voice initialized for MIDI " + std::to_string(midiNote_) + 
           " with ADSR envelope system (A:" + std::to_string(envelope_->getAttackLength()) + 
           ", R:" + std::to_string(envelope_->getReleaseLength()) + " samples)", logger);
}

/**
 * @brief NON-RT SAFE: Updates internal buffer size for DAW block size changes
 * Must be called from audio thread during buffer size changes, NOT during processing
 */
void Voice::prepareToPlay(int maxBlockSize) noexcept {
    // NON-RT SAFE: This method should be called from audio thread during setup/buffer size changes
    // Never call during active audio processing
    
    if (maxBlockSize > 0 && gainBuffer_.size() < static_cast<size_t>(maxBlockSize)) {
        try {
            gainBuffer_.resize(maxBlockSize);
        } catch (...) {
            // In case of allocation failure, keep old buffer
            // This is better than crashing, though not ideal
        }
    }
}

/**
 * @brief cleanup s envelope position reset a idle transition
 */
void Voice::cleanup(Logger& logger) {
    state_ = VoiceState::Idle;  // OPRAVA: Explicitně nastavit Idle
    position_ = 0;
    envelope_gain_ = 0.0f;
    velocity_gain_ = 0.0f;
    master_gain_ = 0.8f;
    
    // Reset envelope positions
    envelope_attack_position_ = 0;
    envelope_release_position_ = 0;

    // Reset release start gain pro čistý stav
    release_start_gain_ = 1.0f;
    
    logSafe("Voice/cleanup", "info", 
           "Voice cleaned up and reset to idle for MIDI " + std::to_string(midiNote_), logger);
}

/**
 * @brief reinitialize s envelope
 */
void Voice::reinitialize(const Instrument& instrument, int sampleRate,
                         const Envelope& envelope, Logger& logger) {
    initialize(instrument, sampleRate, envelope, logger);

    logSafe("Voice/reinitialize", "info", 
           "Voice reinitialized with new instrument, sampleRate and ADSR envelope for MIDI " + std::to_string(midiNote_), logger);
}

/**
 * @brief setNoteState s envelope position reset
 */
void Voice::setNoteState(bool isOn, uint8_t velocity) noexcept {
    if (!instrument_ || sampleRate_ <= 0 || !envelope_) {
        return;
    }
    
    currentVelocityLayer_ = std::min(static_cast<uint8_t>(velocity / 16), static_cast<uint8_t>(7));
    
    if (isOn) {
        std::cout << "-> setNoteState to ON";                       // xxx - DEBUG PRINT
        updateVelocityGain(velocity);                               // update velicity gain se vola jen pri note on
        std::cout << "-> VoiceState::Attacking";                    // xxx - DEBUG PRINT
        state_ = VoiceState::Attacking;                             // set voice state 
        position_ = 0;                                              // reset sample position in frames
        envelope_gain_ = 0.0f;                                      // reset envelope gain
        envelope_attack_position_ = 0;                              // reset envelope attack position
        release_start_gain_ = 0.0f;                                 // reset na 0.0f při note ON (attack začíná od nuly, připraveno pro případný brzký note OFF)
    } else {
        if (state_ == VoiceState::Sustaining || state_ == VoiceState::Attacking) {
            std::cout << "-> VoiceState::Releasing";                // xxx - DEBUG PRINT
            state_ = VoiceState::Releasing;
            envelope_release_position_ = 0;
            release_start_gain_ = envelope_gain_;                   // zachytit aktuální gain pro škálování release (od attack nebo sustain)
        }
    }
}

/**
 * @brief calculateBlockGains pro ADSR envelope
 */
bool Voice::calculateBlockGains(float* gainBuffer, int numSamples) noexcept {
    if (state_ == VoiceState::Idle || !gainBuffer || numSamples <= 0) {
        return false;
    }
    
    switch (state_) {
        case VoiceState::Attacking: {
            std::cout << "-> getAttackGains";                       // xxx - DEBUG PRINT
            bool attackContinues = envelope_->getAttackGains(gainBuffer, numSamples, envelope_attack_position_);
            
            envelope_attack_position_ += numSamples;
            
            // Zkontroluj dokončení attack
            if (!attackContinues || gainBuffer[numSamples - 1] >= 0.99f) {
                state_ = VoiceState::Sustaining;
                // Dokončit blok sustain hodnotami
                std::cout << "-> getSustainLevel";                  // xxx - DEBUG PRINT
                const float sustainLevel = envelope_->getSustainLevel();
                for (int i = 0; i < numSamples; ++i) {
                    if (gainBuffer[i] >= 0.99f) gainBuffer[i] = sustainLevel;
                }
                release_start_gain_ = sustainLevel;                 // aktualizace na sustain úroveň (připraveno pro budoucí release)
            }
            
            envelope_gain_ = gainBuffer[numSamples - 1];
            return true;
        }
        
        case VoiceState::Sustaining: {
            std::cout << "-> getSustainLevel";                      // xxx - DEBUG PRINT
            const float sustainLevel = envelope_->getSustainLevel();
            for (int i = 0; i < numSamples; ++i) {
                gainBuffer[i] = sustainLevel;
            }
            envelope_gain_ = sustainLevel;
            return true;
        }
        
        case VoiceState::Releasing: {
            std::cout << "-> getReleaseGains";                      // xxx - DEBUG PRINT
            bool releaseContinues = envelope_->getReleaseGains(gainBuffer, numSamples, envelope_release_position_);
            
            // škálovat release gainy na aktuální startovní úroveň (pro plynulý přechod)
            for (int i = 0; i < numSamples; ++i) {
                gainBuffer[i] *= release_start_gain_;
            }

            envelope_release_position_ += numSamples;
            envelope_gain_ = gainBuffer[numSamples - 1];
            
            // proper transition to idle when release ends
            if (!releaseContinues || envelope_gain_ <= 0.001f) {
                state_ = VoiceState::Idle;                          // přechod na idle
                envelope_gain_ = 0.0f;
                return false;                                       // hlas dohral 
            }
            return true;
        }

        default:
            return false;
    }
}

/**
 * @brief Aplikuje gain na výstup
 */
bool Voice::processBlock(float* outputLeft, 
                         float* outputRight, 
                         int samplesPerBlock) noexcept {

    // Early exit conditions - RT optimized
    if (state_ == VoiceState::Idle || !instrument_ || sampleRate_ <= 0) {
        return false;  // Voice není aktivní
    }
    
    if (!outputLeft || !outputRight || samplesPerBlock <= 0) {
        return false;
    }
    
    // Cache frequently used values
    const float* stereoBuffer = instrument_->get_sample_begin_pointer(currentVelocityLayer_);
    const sf_count_t maxFrames = instrument_->get_frame_count(currentVelocityLayer_);

    if (!stereoBuffer || maxFrames == 0) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    // OPRAVA: Safe conversion s explicitním ošetřením přetečení
    const int samplesUntilEnd = maxFrames - position_;
    const int samplesRequested = static_cast<sf_count_t>(samplesPerBlock);
    const int samplesToProcess = std::min(samplesRequested, samplesUntilEnd);
    
        // Use pre-allocated buffer
    if (gainBuffer_.size() < static_cast<size_t>(samplesToProcess)) {
        return false;
    }
    
    // Pre-calculate envelope gains for the block
    if (!calculateBlockGains(gainBuffer_.data(), samplesToProcess)) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    // GAIN CHAIN - envelope * velocity * master
    const sf_count_t startIndex = position_ * 2;
    const float* srcPtr = stereoBuffer + startIndex;
    
    // APLIKOVAT gain na výstup

    // TISK-DEBUG GAIN: Připravíme stream pro tisk
    std::ostringstream gain_stream; 
    
    gain_stream << gainBuffer_[0]; // xxx - DEBUG PRINT

    for (int i = 0; i < samplesToProcess; ++i) {
        const int srcIndex = i * 2;
        
        // Mixdown 
        outputLeft[i] += srcPtr[srcIndex] * gainBuffer_[i];
        outputRight[i] += srcPtr[srcIndex + 1] * gainBuffer_[i];

        gain_stream << gainBuffer_[i]; // xxx - DEBUG PRINT
        if (i > 0) gain_stream << ", "; // xxx - DEBUG PRINT

    }
       
    
    // // TISK-DEBUG GAIN: vytisknout celou sadu gainů na konzoli
    std::cout << "GAINS: [" << gain_stream.str() << "]" << std::endl; // xxx - DEBUG PRINT

    // Čekání na stisk Enteru
    //std::cout << "Press ENTER to continue..." << std::flush; // xxx - DEBUG WAIT ENTER
    //std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // xxx - DEBUG 

    // Batch position update - OPRAVA: Safe conversion zpět
    position_ += static_cast<sf_count_t>(samplesToProcess);
    
    // End condition check - OPRAVA: proper transition to Idle
    if (position_ >= maxFrames) {
        state_ = VoiceState::Idle;
        return false;
    }
    
    return state_ != VoiceState::Idle;
}

// ===== PRIVATE RT-SAFE METODY =====

/**
 * @brief RT-SAFE: NOVÁ metoda pro aplikaci MIDI velocity na hlasitost
 * Mapuje velocity 0-127 na velocity_gain_ 0.0-1.0 s logaritmickou křivkou.
 */
void Voice::updateVelocityGain(uint8_t velocity) noexcept {
    if (velocity == 0) {
        velocity_gain_ = 0.0f;  // Silence pro velocity 0
        return;
    }
    
    // Logaritmická křivka pro přirozenější velocity response
    float normalized = static_cast<float>(velocity) / 127.0f;
    velocity_gain_ = std::sqrt(normalized);
    
    // Clamp pro bezpečnost
    velocity_gain_ = std::max(0.0f, std::min(1.0f, velocity_gain_));
}

/**
 * @brief NON-RT SAFE: Nastaví master gain pro voice (0.0-1.0)
 */
void Voice::setMasterGain(float gain, Logger& logger) {
    if (gain < 0.0f || gain > 1.0f) {
        logSafe("Voice/setMasterGain", "error", 
               "Invalid master gain " + std::to_string(gain) + 
               " (must be 0.0-1.0) for MIDI " + std::to_string(midiNote_), logger);
        return;
    }
    master_gain_ = gain;
    logSafe("Voice/setMasterGain", "info", 
           "Master gain set to " + std::to_string(master_gain_) + 
           " for MIDI " + std::to_string(midiNote_), logger);
}

/**
 * @brief NON-RT SAFE: Získá debug informace o gain structure
 */
std::string Voice::getGainDebugInfo(Logger& logger) const {
    std::string info = "MIDI " + std::to_string(midiNote_) + 
                      " Gains - Envelope: " + std::to_string(envelope_gain_) +
                      ", Velocity: " + std::to_string(velocity_gain_) +
                      ", Master: " + std::to_string(master_gain_) +
                      ", State: " + std::to_string(static_cast<int>(state_));
    
    logSafe("Voice/getGainDebugInfo", "info", info, logger);
    return info;
}

/**
 * @brief NON-RT: Safe logging wrapper pro non-critical operations.
 * Používá se pouze v initialize/cleanup/reinitialize metodách.
 */
void Voice::logSafe(const std::string& component, const std::string& severity, 
                   const std::string& message, Logger& logger) const {
    // Conditional logging - skip pokud je RT mode zapnutý
    if (!rtMode_.load()) {
        logger.log(component, severity, message);
    }
}