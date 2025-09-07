#include "voice.h"
#include <cmath>        // Pro výpočty obálky (sin, pow atd.)
#include <algorithm>    // Pro std::min (omezení pozice)

// Inicializace statických členů
int Voice::targetSampleRate_ = 44100;

/**
 * @brief Inicializuje voice s instrumentem (pro pool v VoiceManager).
 * Nastaví instrument pointer a zaloguje (logger předán zvenčí). 
 * MidiNote je nastaven v konstruktoru.
 * @param instrument Reference na načtený Instrument z InstrumentLoader.
 * @param logger Reference na Logger (sdílený zvenčí, používá se pro logování).
 */
void Voice::initialize(const Instrument& instrument, Logger& logger) {
    instrument_ = &instrument;
    currentState_ = VoiceState::idle;
    currentPosition_ = 0;
    isGateOn_ = false;
    activeVelocityLayer_ = 0;
    targetSampleRate_ = 44100;  // Výchozí, lze přepsat
    logger.log("Voice/initialize", "info",
              "Voice initialized for MIDI " + std::to_string(midiNote_) +
              " with targetSampleRate " + std::to_string(targetSampleRate_));
}

/**
 * @brief Reinicializuje s novým instrumentem (pro dynamickou změnu).
 * Nastaví nový instrument pointer, zavolá cleanup pro reset. Zaloguje (logger předán zvenčí).
 * MidiNote se nemění (nastaven v konstruktoru).
 * @param instrument Reference na nový Instrument.
 * @param logger Reference na Logger (pro logování).
 */
void Voice::reinitialize(const Instrument& instrument, Logger& logger) {
    cleanup(logger);  // Reset před novou inicializací
    instrument_ = &instrument;
    logger.log("Voice/reinitialize", "info", "Voice reinitialized for MIDI " + std::to_string(midiNote_));
}

/**
 * @brief Resetuje voice na idle stav (pro vrácení do poolu).
 * Resetuje stav, pozici a gate. Zaloguje (logger předán zvenčí).
 * @param logger Reference na Logger (pro logování).
 */
void Voice::cleanup(Logger& logger) {
    currentState_ = VoiceState::idle;
    currentPosition_ = 0;
    isGateOn_ = false;
    activeVelocityLayer_ = 0;
    logger.log("Voice/cleanup", "info", "Voice cleaned up for MIDI " + std::to_string(midiNote_));
}

/**
 * @brief Nastaví stav noty (start/stop).
 * Pokud isOn, zavolá startNote; jinak stopNote.
 * @param isOn true pro start, false pro stop.
 * @param velocity MIDI velocity (0-127) pro start (ignorováno pro stop).
 * @param logger Reference na Logger (pro logování v start/stop).
 */
void Voice::setNoteState(bool isOn, uint8_t velocity, Logger& logger) {
    if (isOn) {
        startNote(velocity, logger);
    } else {
        stopNote(logger);
    }
}

/**
 * @brief Posune pozici v sample o 1 frame (pro non-real-time processing).
 * Omezí pozici koncem sample pomocí std::min s přetypováním na sf_count_t.
 * Žádný log – čistá metoda.
 */
void Voice::advancePosition() {
    if (instrument_ && instrument_->velocityExists[activeVelocityLayer_]) {
        sf_count_t frameCount = instrument_->get_frame_count(activeVelocityLayer_);
        currentPosition_ = std::min(currentPosition_ + 1, static_cast<sf_count_t>(frameCount - 1));
    }
}

/**
 * @brief Získává aktuální stereo audio data (left, right) na aktuální pozici.
 * Aplikován gain z obálky. Pokud mimo rozsah, vrátí nuly.
 * @return AudioData struktura s aplikovaným gainem.
 * Žádný log – čistá query metoda.
 */
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

/**
 * @brief Spustí notu (note-on event).
 * Nastaví gate on, vybere velocity layer, resetuje pozici a stav na attacking.
 * Zaloguje (logger předán zvenčí).
 * @param velocity MIDI velocity (0-127).
 * @param logger Reference na Logger (pro logování).
 */
void Voice::startNote(uint8_t velocity, Logger& logger) {
    isGateOn_ = true;
    activeVelocityLayer_ = mapVelocityToLayer(velocity);
    currentPosition_ = 0;
    currentState_ = VoiceState::attacking;
    noteOffTime_ = std::chrono::steady_clock::now();
    logger.log("Voice/startNote", "info",
              "Note started for MIDI " + std::to_string(midiNote_) +
              " velocity layer " + std::to_string(activeVelocityLayer_));
}

/**
 * @brief Zastaví notu (note-off event).
 * Nastaví gate off a stav na releasing, uloží čas pro obálku.
 * Zaloguje (logger předán zvenčí).
 * @param logger Reference na Logger (pro logování).
 */
void Voice::stopNote(Logger& logger) {
    isGateOn_ = false;
    currentState_ = VoiceState::releasing;
    noteOffTime_ = std::chrono::steady_clock::now();
    logger.log("Voice/stopNote", "info",
              "Note stopped for MIDI " + std::to_string(midiNote_) + " - entering release phase");
}

/**
 * @brief Hlavní processing metoda (analogie k JUCE processBlock).
 * Zpracuje blok samples: de-interleave z stereo bufferu, aplikuje gain z obálky, zapisuje do výstupu.
 * Pokud konec sample nebo release dokončen, vrátí false pro deaktivaci voice.
 * Non-const: Mění stav (currentState_, currentPosition_).
 * @param outputBuffer Výstupní stereo buffer (simulace JUCE).
 * @param numSamples Počet samples k zpracování.
 * @param logger Reference na Logger (pro logování výjimek, např. konec sample).
 * @return true, pokud voice stále aktivní; false pro deaktivaci.
 */
bool Voice::processBlock(AudioBuffer& outputBuffer, int numSamples, Logger& logger) {
    if (currentState_ == VoiceState::idle || !instrument_ || !instrument_->velocityExists[activeVelocityLayer_]) {
        return false;  // Deaktivovat voice
    }
    sf_count_t frameCount = instrument_->get_frame_count(activeVelocityLayer_);
    float* sampleData = instrument_->get_sample_begin_pointer(activeVelocityLayer_);
    if (!sampleData || frameCount == 0) {
        logger.log("Voice/processBlock", "warn", "Invalid sample data for MIDI " + std::to_string(midiNote_));
        currentState_ = VoiceState::idle;
        return false;
    }
    sf_count_t endPosition = frameCount;  // Konec sample v framech
    for (int sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx) {
        // Omezení pozice s přetypováním na sf_count_t pro kompatibilitu std::min
        currentPosition_ = std::min(currentPosition_, static_cast<sf_count_t>(endPosition - 1));
        if (currentPosition_ >= endPosition) {
            logger.log("Voice/processBlock", "info", "End of sample reached for MIDI " + std::to_string(midiNote_));
            currentState_ = VoiceState::idle;
            outputBuffer.leftChannel[sampleIdx] = 0.0f;
            outputBuffer.rightChannel[sampleIdx] = 0.0f;
            continue;
        }
        // De-interleave z interleaved [L,R,L,R...] a aplikace gainu
        sf_count_t idx = currentPosition_ * 2;
        float gain = getEnvelopeGain();
        outputBuffer.leftChannel[sampleIdx] += sampleData[idx] * gain;  // Additive pro polyfonii
        outputBuffer.rightChannel[sampleIdx] += sampleData[idx + 1] * gain;
        ++currentPosition_;
        // Kontrola dokončení release (gain blízko nule)
        if (currentState_ == VoiceState::releasing && gain < 0.001f) {
            logger.log("Voice/processBlock", "info", "Release completed for MIDI " + std::to_string(midiNote_));
            currentState_ = VoiceState::idle;
            return false;
        }
    }
    // Aktualizace stavu po zpracování bloku
    if (currentPosition_ >= endPosition) {
        currentState_ = VoiceState::idle;
    } else if (isGateOn_ && currentState_ == VoiceState::attacking) {
        currentState_ = VoiceState::sustaining;  // Přechod do sustain po attack
    }
    return isActive();
}

/**
 * @brief Získává aktuální gain obálky (time-based).
 * Pro releasing: Lineární útlum od 1.0 k 0.0 za RELEASE_TIME_MS.
 * Pro ostatní stavy: Plný gain 1.0 (zjednodušená obálka bez full ADSR).
 * @return Gain faktor (0.0f - 1.0f).
 * Const: Pouze čte stav, ne mění. Žádný log.
 */
float Voice::getEnvelopeGain() const {
    if (currentState_ == VoiceState::idle) return 0.0f;
    auto now = std::chrono::steady_clock::now();
    auto elapsedRelease = std::chrono::duration_cast<std::chrono::milliseconds>(now - noteOffTime_).count();
    if (currentState_ == VoiceState::releasing) {
        float releaseFactor = 1.0f - (static_cast<float>(elapsedRelease) / RELEASE_TIME_MS);
        return std::max(0.0f, releaseFactor);  // Clamp na 0.0f
    }
    return 1.0f;  // Plný gain pro attacking/sustaining
}

/**
 * @brief Mapuje MIDI velocity (0-127) na velocity layer (0-7).
 * Jednoduché dělení: 0-15 → 0, 16-31 → 1, ..., 112-127 → 7.
 * @param midiVelocity MIDI velocity hodnota.
 * @return Velocity layer index (0-7).
 * Žádný log – interní metoda.
 */
uint8_t Voice::mapVelocityToLayer(uint8_t midiVelocity) const {
    return static_cast<uint8_t>(midiVelocity / 16);  // 0-15→0, 16-31→1, ..., 112-127→7
}