#include "voice.h"
#include <cmath>        // Pro výpočty obálky (sin, pow atd.)

// Inicializace statických členů
int Voice::targetSampleRate_ = 44100;
Logger* Voice::sharedLogger_ = nullptr;

// Default konstruktor
Voice::Voice() : midiNote_(0), instrument_(nullptr), noteOffTime_(std::chrono::steady_clock::now()) {
    // Nic dalšího – čeká na initialize
}

// Konstruktor s 2 argumenty
Voice::Voice(uint8_t midiNote, Logger& logger) : midiNote_(midiNote), instrument_(nullptr), noteOffTime_(std::chrono::steady_clock::now()) {
    sharedLogger_ = &logger;
    sharedLogger_->log("Voice/constructor", "info", 
                       "Voice created for MIDI note " + std::to_string(midiNote_) + " (instrument pending)");
}

// Plný konstruktor
Voice::Voice(uint8_t midiNote, const Instrument& instrument, Logger& logger)
    : midiNote_(midiNote), instrument_(&instrument), noteOffTime_(std::chrono::steady_clock::now()) {
    sharedLogger_ = &logger;
    targetSampleRate_ = 44100;  // Výchozí, lze přepsat
    sharedLogger_->log("Voice/constructor", "info", 
                       "Voice created for MIDI note " + std::to_string(midiNote_) + 
                       " with targetSampleRate " + std::to_string(targetSampleRate_));
}

Voice::~Voice() {
    if (sharedLogger_) {
        sharedLogger_->log("Voice/destructor", "info", 
                           "Voice for MIDI " + std::to_string(midiNote_) + " destroyed");
    }
}

// Inicializace
void Voice::initialize(const Instrument& instrument) {
    instrument_ = &instrument;
    currentState_ = VoiceState::idle;
    currentPosition_ = 0;
    if (sharedLogger_) {
        sharedLogger_->log("Voice/initialize", "info", "Voice initialized for MIDI " + std::to_string(midiNote_));
    }
}

// Vyčištění
void Voice::cleanup() {
    currentState_ = VoiceState::idle;
    currentPosition_ = 0;
    isGateOn_ = false;
    if (sharedLogger_) {
        sharedLogger_->log("Voice/cleanup", "info", "Voice cleaned up for MIDI " + std::to_string(midiNote_));
    }
}

// Reinicializace
void Voice::reinitialize(const Instrument& instrument) {
    instrument_ = &instrument;
    cleanup();  // Reset před novou inicializací
    if (sharedLogger_) {
        sharedLogger_->log("Voice/reinitialize", "info", "Voice reinitialized for MIDI " + std::to_string(midiNote_));
    }
}

// Nastavení stavu noty
void Voice::setNoteState(bool isOn, uint8_t velocity) {
    if (isOn) {
        startNote(velocity);
    } else {
        stopNote();
    }
}

// Posun pozice
void Voice::advancePosition() {
    if (instrument_ && instrument_->velocityExists[activeVelocityLayer_]) {
        sf_count_t frameCount = instrument_->get_frame_count(activeVelocityLayer_);
        currentPosition_ = std::min(currentPosition_ + 1, static_cast<sf_count_t>(frameCount - 1));  // Přetypování pro std::min
    }
}

// Získání aktuálního audio data
AudioData Voice::getCurrentAudioData() const {
    AudioData data = {0.0f, 0.0f};
    if (instrument_ && instrument_->velocityExists[activeVelocityLayer_] && currentPosition_ < instrument_->get_frame_count(activeVelocityLayer_)) {
        sf_count_t idx = currentPosition_ * 2;
        float* sampleData = instrument_->get_sample_begin_pointer(activeVelocityLayer_);
        float gain = getEnvelopeGain();
        data.left = sampleData[idx] * gain;
        data.right = sampleData[idx + 1] * gain;
    }
    return data;
}

void Voice::startNote(uint8_t velocity) {
    isGateOn_ = true;
    activeVelocityLayer_ = mapVelocityToLayer(velocity);
    currentPosition_ = 0;
    currentState_ = VoiceState::attacking;
    noteOffTime_ = std::chrono::steady_clock::now();

    if (sharedLogger_) {
        sharedLogger_->log("Voice/startNote", "info", 
                           "Note started for MIDI " + std::to_string(midiNote_) + 
                           " velocity layer " + std::to_string(activeVelocityLayer_));
    }
}

void Voice::stopNote() {
    isGateOn_ = false;
    currentState_ = VoiceState::releasing;
    noteOffTime_ = std::chrono::steady_clock::now();

    if (sharedLogger_) {
        sharedLogger_->log("Voice/stopNote", "info", 
                           "Note stopped for MIDI " + std::to_string(midiNote_) + " - entering release phase");
    }
}

bool Voice::processBlock(AudioBuffer& outputBuffer, int numSamples) {
    if (currentState_ == VoiceState::idle || !instrument_ || !instrument_->velocityExists[activeVelocityLayer_]) {
        return false;  // Deaktivovat voice
    }

    sf_count_t frameCount = instrument_->get_frame_count(activeVelocityLayer_);
    float* sampleData = instrument_->get_sample_begin_pointer(activeVelocityLayer_);

    if (!sampleData || frameCount == 0) {
        currentState_ = VoiceState::idle;
        return false;
    }

    sf_count_t endPosition = frameCount;  // Konec sample v framech

    for (int sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx) {
        // Omezení pozice s přetypováním
        currentPosition_ = std::min(currentPosition_, static_cast<sf_count_t>(endPosition - 1));

        if (currentPosition_ >= endPosition) {
            currentState_ = VoiceState::idle;
            outputBuffer.leftChannel[sampleIdx] = 0.0f;
            outputBuffer.rightChannel[sampleIdx] = 0.0f;
            continue;
        }

        // De-interleave a aplikace gain
        sf_count_t idx = currentPosition_ * 2;
        float gain = getEnvelopeGain();
        outputBuffer.leftChannel[sampleIdx] = sampleData[idx] * gain;
        outputBuffer.rightChannel[sampleIdx] = sampleData[idx + 1] * gain;

        ++currentPosition_;

        // Kontrola release
        if (currentState_ == VoiceState::releasing && gain < 0.001f) {
            currentState_ = VoiceState::idle;
            return false;
        }
    }

    // Aktualizace stavu
    if (currentPosition_ >= endPosition) {
        currentState_ = VoiceState::idle;
    } else if (isGateOn_ && currentState_ == VoiceState::attacking) {
        currentState_ = VoiceState::sustaining;
    }

    return isActive();
}

float Voice::getEnvelopeGain() const {
    if (currentState_ == VoiceState::idle) return 0.0f;

    auto now = std::chrono::steady_clock::now();
    auto elapsedRelease = std::chrono::duration_cast<std::chrono::milliseconds>(now - noteOffTime_).count();

    if (currentState_ == VoiceState::releasing) {
        float releaseFactor = 1.0f - (static_cast<float>(elapsedRelease) / RELEASE_TIME_MS);
        return std::max(0.0f, releaseFactor);
    }

    return 1.0f;  // Plný gain pro attacking/sustaining
}

uint8_t Voice::mapVelocityToLayer(uint8_t midiVelocity) const {
    return static_cast<uint8_t>(midiVelocity / 16);  // 0-15→0, ..., 112-127→7
}