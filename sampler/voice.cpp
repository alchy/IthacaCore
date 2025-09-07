#include "voice.h"
#include <algorithm>  // Pro std::min a případné clamp (gain)
#include <cmath>      // Pro float výpočty, pokud potřeba (např. gain)

// Default konstruktor
Voice::Voice() : midiNote_(0), instrument_(nullptr), sampleRate_(0), logger_(nullptr),
                 state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
                 gain_(0.0f), releaseStartPosition_(0), releaseSamples_(0) {
    // Žádná akce - čeká na initialize pro plnou inicializaci
}

// Konstruktor pro VoiceManager (pool mód)
Voice::Voice(uint8_t midiNote, Logger& logger)
    : midiNote_(midiNote), instrument_(nullptr), sampleRate_(0), logger_(&logger),
      state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
      gain_(0.0f), releaseStartPosition_(0), releaseSamples_(0) {
    logger.log("Voice/constructor", "info", "Voice initialized for MIDI " + std::to_string(midiNote) + " in pool mode");
}

// Plný konstruktor
Voice::Voice(uint8_t midiNote, const Instrument& instrument, int sampleRate, Logger& logger)
    : midiNote_(midiNote), instrument_(&instrument), sampleRate_(sampleRate), logger_(&logger),
      state_(VoiceState::Idle), position_(0), currentVelocityLayer_(0),
      gain_(0.0f), releaseStartPosition_(0), releaseSamples_(0) {
    logger.log("Voice/constructor", "info", "Full Voice initialized for MIDI " + std::to_string(midiNote) + 
               " with sampleRate " + std::to_string(sampleRate));
    calculateReleaseSamples();  // Inicializace release na základě sampleRate
}

/**
 * @brief Inicializuje voice s instrumentem a sample rate (pro pool v VoiceManager).
 * Uloží sample rate, validuje ho (>0), nastaví stav na idle, vypočítá releaseSamples.
 * @param instrument Reference na Instrument (pro přístup k bufferům).
 * @param sampleRate Frekvence vzorkování (např. 44100 Hz) pro výpočty obálky.
 * @param logger Reference na Logger pro logování.
 */
void Voice::initialize(const Instrument& instrument, int sampleRate, Logger& logger) {
    instrument_ = &instrument;
    sampleRate_ = sampleRate;
    logger_ = &logger;
    
    // Validace sampleRate - musí být kladný pro správné výpočty obálky
    if (sampleRate_ <= 0) {
        logger.log("Voice/initialize", "error", "Invalid sampleRate " + std::to_string(sampleRate_) + 
                   " - must be > 0. Terminating.");
        std::exit(1);
    }
    
    // Reset všech stavů pro čistý start
    state_ = VoiceState::Idle;
    position_ = 0;
    currentVelocityLayer_ = 0;
    gain_ = 0.0f;
    releaseStartPosition_ = 0;
    
    calculateReleaseSamples();  // Vypočítá délku release na základě sampleRate_
    
    logger_->log("Voice/initialize", "info", "Voice initialized for MIDI " + std::to_string(midiNote_) + 
                 " with sampleRate " + std::to_string(sampleRate_));
}

/**
 * @brief Cleanup: Reset na idle stav, zastaví přehrávání.
 * Používá se při uvolňování voice v poolu.
 */
void Voice::cleanup() {
    state_ = VoiceState::Idle;
    position_ = 0;
    gain_ = 0.0f;
    releaseStartPosition_ = 0;
    if (logger_) {
        logger_->log("Voice/cleanup", "info", "Voice cleaned up and reset to idle");
    }
}

/**
 * @brief Reinicializuje s novým instrumentem a sample rate.
 * Volá initialize pro reset a nové nastavení.
 * @param instrument Nový Instrument.
 * @param sampleRate Nový sample rate.
 * @param logger Logger.
 */
void Voice::reinitialize(const Instrument& instrument, int sampleRate, Logger& logger) {
    initialize(instrument, sampleRate, logger);
    if (logger_) {
        logger_->log("Voice/reinitialize", "info", "Voice reinitialized with new instrument and sampleRate");
    }
}

/**
 * @brief Nastaví stav note: true = startNote (attack/sustain), false = stopNote (release).
 * Při start: Nastaví sustaining (attack zde okamžitý), vybere velocity layer.
 * Při stop: Přejde do releasing, spustí release timer pomocí sampleRate_.
 * @param isOn True pro start, false pro stop.
 * @param velocity Velocity (0-127, mapováno na layer 0-7).
 */
void Voice::setNoteState(bool isOn, uint8_t velocity) {
    if (!instrument_ || sampleRate_ <= 0) {
        if (logger_) {
            logger_->log("Voice/setNoteState", "warn", "Voice not initialized - ignoring note state change");
        }
        return;
    }
    
    // Mapování velocity 0-127 na layer 0-7: 0-15 -> 0, 16-31 -> 1, ..., 112-127 -> 7
    // Explicitní cast na uint8_t pro shodu typů v std::min (velocity / 16 je int)
    currentVelocityLayer_ = std::min(static_cast<uint8_t>(velocity / 16), static_cast<uint8_t>(7));
    
    if (isOn) {
        // Start note: Attack (zde okamžitý) -> Sustain
        state_ = VoiceState::Sustaining;
        position_ = 0;  // Začátek sample
        gain_ = 1.0f;   // Plný gain
        if (logger_) {
            logger_->log("Voice/setNoteState", "info", "Note-on for MIDI " + std::to_string(midiNote_) + 
                         " velocity layer " + std::to_string(currentVelocityLayer_));
        }
    } else {
        // Stop note: Přejdi do release, pokud byl aktivní
        if (state_ == VoiceState::Sustaining || state_ == VoiceState::Attacking) {
            state_ = VoiceState::Releasing;
            releaseStartPosition_ = position_;  // Uložit aktuální pozici pro útlum
            if (logger_) {
                logger_->log("Voice/setNoteState", "info", "Note-off for MIDI " + std::to_string(midiNote_) + 
                             " - starting release with " + std::to_string(releaseSamples_) + " samples");
            }
        }
    }
}

/**
 * @brief Posune pozici ve sample o 1 frame (pro non-real-time processing).
 * Aplikovat release gain, pokud v releasing stavu.
 */
void Voice::advancePosition() {
    if (state_ == VoiceState::Idle || !instrument_) {
        return;  // Nic k posunu
    }
    
    sf_count_t maxFrames = instrument_->get_frame_count(currentVelocityLayer_);
    if (position_ < maxFrames) {
        ++position_;  // Posun o 1 frame (stereo pár L,R)
    }
    
    // Aplikovat lineární release, pokud v releasing stavu
    if (state_ == VoiceState::Releasing) {
        sf_count_t elapsed = position_ - releaseStartPosition_;
        if (elapsed < releaseSamples_) {
            // Lineární útlum: gain = 1.0 - (elapsed / releaseSamples_)
            gain_ = 1.0f - static_cast<float>(elapsed) / static_cast<float>(releaseSamples_);
        } else {
            gain_ = 0.0f;
            state_ = VoiceState::Idle;  // Konec release
        }
    }
}

/**
 * @brief Získá aktuální stereo audio data s aplikovanou obálkou (gain).
 * Používá interleaved buffer z Instrument [L,R,L,R...].
 * @param data Reference na AudioData pro výstup (left, right).
 * @return True, pokud data jsou platná (voice aktivní a pozice v bufferu).
 */
bool Voice::getCurrentAudioData(AudioData& data) const {
    if (state_ == VoiceState::Idle || !instrument_ || 
        position_ >= instrument_->get_frame_count(currentVelocityLayer_)) {
        return false;  // Neaktivní nebo konec sample
    }
    
    float* stereoPtr = instrument_->get_sample_begin_pointer(currentVelocityLayer_);
    if (!stereoPtr) return false;
    
    // Interleaved přístup: index = position_ * 2 pro L, +1 pro R
    sf_count_t idx = position_ * 2;
    data.left = stereoPtr[idx];
    data.right = stereoPtr[idx + 1];
    
    // Aplikovat gain obálky
    applyEnvelope(data.left, data.right);
    
    return true;
}

/**
 * @brief Hlavní metoda: Zpracuje audio blok (např. 512 samples), aplikuje obálku, zapisuje do bufferu.
 * Placeholder implementace - pro plný blok: Loop přes numSamples, posun position_, aplikuj gain.
 * Používá sampleRate_ pro obálku (už vypočítáno v releaseSamples_).
 * @param outputBuffer Simulovaný AudioBuffer pro výstup (stereo, placeholder).
 * @param numSamples Počet samples k zpracování v bloku.
 * @return True, pokud voice je stále aktivní po zpracování (pro VoiceManager).
 */
bool Voice::processBlock(/* AudioBuffer& outputBuffer, int numSamples */) {
    // Placeholder: Pro reálnou implementaci - loop přes numSamples:
    // for (int i = 0; i < numSamples; ++i) {
    //     advancePosition();  // Posun pro každý sample
    //     AudioData sample;
    //     if (getCurrentAudioData(sample)) {
    //         // Přidat sample.left/right do outputBuffer s mixováním
    //     } else {
    //         // Konec sample - ukončit
    //         return false;
    //     }
    // }
    
    if (state_ == VoiceState::Idle) return false;
    
    // Zjednodušeně: Simuluj pro 1 sample (rozšířit pro blok)
    advancePosition();
    
    // Zde by se zapisovalo do outputBuffer s gain_ (už aplikovaným v getCurrentAudioData)
    
    return isActive();  // Vrátí true, pokud stále aktivní
}

/**
 * @brief Kontroluje aktivitu voice (pro řízení v VoiceManager).
 * @return True, pokud stav není Idle (včetně releasing).
 */
bool Voice::isActive() const {
    return state_ != VoiceState::Idle;
}

/**
 * @brief Pomocná: Výpočet releaseSamples na základě sampleRate_ (200 ms release).
 * Délka release v samplech: 0.2 sekundy * sampleRate.
 */
void Voice::calculateReleaseSamples() {
    if (sampleRate_ > 0) {
        releaseSamples_ = static_cast<sf_count_t>(0.2 * sampleRate_);  // 200 ms = 0.2 s
    } else {
        releaseSamples_ = 0;
    }
}

/**
 * @brief Pomocná: Aplikovat gain na stereo sample (používá se v getCurrentAudioData a processBlock).
 * @param left Reference na left kanál (množí gain).
 * @param right Reference na right kanál (množí gain).
 */
void Voice::applyEnvelope(float& left, float& right) const {
    left *= gain_;
    right *= gain_;
}