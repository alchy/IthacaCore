/*
THIS FILE IS LOCKED, IT IS FUNCTIONAL AND WILL NOT BE CHANGED
EXCEPT FOR REQUIRED UPDATES LIKE SAMPLEDIR PROPAGATION
*/

#include "voice_manager.h"
#include <algorithm>  // Pro případné výpočty (např. clamp gainu v mixu, ale zde ne)

/**
 * @brief Konstruktor: Vytvoří fixní pool 128 voice, uloží sampleDir, sampleRate.
 * Každý hlas je inicializován s midiNote_ = index (0-127) pro přímý přístup.
 * @param sampleDir Cesta k adresáři se samples.
 * @param sampleRate Frekvence vzorkování (Hz).
 * @param logger Reference na Logger.
 */
VoiceManager::VoiceManager(const std::string& sampleDir, int sampleRate, Logger& logger)
    : logger_(logger), sampleRate_(sampleRate), sampleDir_(sampleDir) {
    if (sampleRate_ <= 0) {
        logger.log("VoiceManager/constructor", "error", 
                   "Invalid sampleRate " + std::to_string(sampleRate_) + " - must be > 0. Terminating.");
        std::exit(1);
    }
    
    if (sampleDir_.empty()) {
        logger.log("VoiceManager/constructor", "error", 
                   "Invalid sampleDir - cannot be empty. Terminating.");
        std::exit(1);
    }
    
    voices_.resize(128);  // Fixní velikost pro MIDI 0-127
    for (int i = 0; i < 128; ++i) {
        // Každý hlas má fixní midiNote = index (pro přímý přístup voices_[midiNote])
        voices_[i] = Voice(static_cast<uint8_t>(i), logger_);
    }
    
    logger_.log("VoiceManager/constructor", "info", 
                "VoiceManager created with fixed 128 voices (one per MIDI note), sampleDir '" + sampleDir_ + 
                "' and sampleRate " + std::to_string(sampleRate_));
}

/**
 * @brief Inicializuje všechny 128 voices s instrumenty z Loaderu.
 * Lokálně vytvoří SamplerIO a InstrumentLoader s sampleDir, sampleRate.
 * Pro každou voice (index = midiNote): Nastaví instrument z loader.getInstrumentNote(index).
 */
void VoiceManager::initializeAll() {
    if (sampleRate_ <= 0 || sampleDir_.empty()) {
        logger_.log("VoiceManager/initializeAll", "error", "SampleRate or sampleDir not set - cannot initialize");
        std::exit(1);
    }
    
    // Lokální vytvoření SamplerIO pro prohledání sampleDir
    SamplerIO samplerIO;
    samplerIO.scanSampleDirectory(sampleDir_, logger_);
    
    // Lokální vytvoření InstrumentLoader pro načtení do bufferů
    InstrumentLoader loader(samplerIO, sampleRate_, logger_);
    loader.loadInstrument();
    
    int initialized = 0;
    for (int i = 0; i < 128; ++i) {
        // Přímý přístup: index = midiNote
        uint8_t midiNote = static_cast<uint8_t>(i);
        const Instrument& inst = loader.getInstrumentNote(midiNote);
        voices_[i].initialize(inst, sampleRate_, logger_);
        ++initialized;
    }
    
    logger_.log("VoiceManager/initializeAll", "info", 
                "Initialized all 128 voices with instruments from sampleDir '" + sampleDir_ + 
                "' and sampleRate " + std::to_string(sampleRate_));
}

/**
 * @brief Nastaví stav note pro danou MIDI notu (přímý přístup k voice[midiNote]).
 * Žádný hledání nebo steal – fixní mapování.
 * @param midiNote MIDI nota (0-127) - slouží jako index do voices_.
 * @param isOn True pro note-on, false pro note-off.
 * @param velocity Velocity (0-127).
 */
void VoiceManager::setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity) {
    if (midiNote > 127) {
        logger_.log("VoiceManager/setNoteState", "warn", 
                    "Invalid MIDI note " + std::to_string(midiNote) + " - ignoring");
        return;
    }
    
    // Přímý přístup: voices_[midiNote] (fixní mapování)
    voices_[midiNote].setNoteState(isOn, velocity);
    
    std::string stateStr = isOn ? "note-on" : "note-off";
    logger_.log("VoiceManager/setNoteState", "info", 
                "Set " + stateStr + " for MIDI " + std::to_string(midiNote) + 
                " (voice index " + std::to_string(midiNote) + ")");
}

/**
 * @brief Procesuje audio blok: Prochází všechny 128 hlasů, součítá výstupy aktivních do bufferu.
 * Pro každou aktivní voice: Volá processBlock a přidává k výstupu (placeholder pro mix).
 * @param outputBuffer Simulovaný buffer pro výstup (stereo, placeholder).
 * @param numSamples Počet samples v bloku.
 * @return True, pokud je alespoň jedna aktivní voice.
 */
bool VoiceManager::processBlock(/* AudioBuffer& outputBuffer, int numSamples */) {
    bool anyActive = false;
    int activeCount = 0;
    
    // Procházet všechny 128 hlasů (fixní loop – deterministický mixdown)
    for (int i = 0; i < 128; ++i) {
        if (voices_[i].processBlock(/* outputBuffer slice pro tento hlas, numSamples */)) {
            ++activeCount;
            anyActive = true;
            // Placeholder mixdown: Součítat výstup voice[i] do globálního outputBuffer
            // Např.: float* left = outputBuffer.getWritePointer(0);
            //       float* right = outputBuffer.getWritePointer(1);
            //       // Pro každý sample v numSamples: left[j] += voice_left[j]; right[j] += voice_right[j];
            //       // Obálka (gain) je už aplikována v Voice::processBlock
        }
    }
    
    if (activeCount > 0) {
        logger_.log("VoiceManager/processBlock", "info", 
                    "Processed block with " + std::to_string(activeCount) + " active voices (mixdown of all 128)");
    } else {
        logger_.log("VoiceManager/processBlock", "info", "No active voices - silent block");
    }
    
    return anyActive;
}

/**
 * @brief Getter pro počet aktivních voices (prochází všechny 128).
 * @return Počet hlasů, kde isActive() == true (včetně releasing).
 */
int VoiceManager::getActiveVoicesCount() const {
    int count = 0;
    for (const auto& voice : voices_) {
        if (voice.isActive()) ++count;
    }
    return count;
}