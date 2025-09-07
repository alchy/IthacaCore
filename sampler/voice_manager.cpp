#include "voice_manager.h"
#include <algorithm>  // Pro std::min, pokud potřeba

/**
 * @brief Inicializuje všechny voices s instrumenty z loaderu.
 * Pro každou voice přiřadí instrument pro její MIDI notu (např. voice i pro MIDI i).
 * @param loader Reference na InstrumentLoader.
 * @param logger Reference na logger pro logování této operace.
 */
void VoiceManager::initializeAll(const InstrumentLoader& loader, Logger& logger) {
    for (auto& voice : voices_) {
        const Instrument& inst = loader.getInstrumentNote(voice.getMidiNote());
        voice.initialize(inst, logger);  // Předání loggeru do Voice
    }
    logger.log("VoiceManager/initializeAll", "info", "All voices initialized from loader");
}

/**
 * @brief Nastaví stav noty pro danou MIDI notu.
 * Najde nebo vytvoří voice pro tuto notu, zavolá setNoteState.
 * @param midiNote MIDI nota (0-127).
 * @param isOn True pro note-on, false pro note-off.
 * @param velocity Velocity (0-127).
 * @param logger Reference na logger pro logování této operace.
 */
void VoiceManager::setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity, Logger& logger) {
    Voice* targetVoice = findVoiceForNote(midiNote);
    if (targetVoice) {
        targetVoice->setNoteState(isOn, velocity, logger);  // Předání loggeru do Voice
    }
    logger.log("VoiceManager/setNoteState", "info", 
              "Set state for MIDI " + std::to_string(midiNote) + 
              (isOn ? " ON" : " OFF") + " vel " + std::to_string(velocity));
}

/**
 * @brief Zpracuje audio blok pro všechny voices (polyfonní mixing).
 * Prochází všechny voices, volá processBlock a mixuje do outputu.
 * @param outputBuffer Výstupní stereo buffer.
 * @param numSamples Počet samples v bloku.
 * @param logger Reference na logger pro logování této operace.
 * @return True, pokud alespoň jedna voice je aktivní.
 */
bool VoiceManager::processBlock(AudioBuffer& outputBuffer, int numSamples, Logger& logger) {
    bool anyActive = false;
    for (auto& voice : voices_) {
        if (voice.processBlock(outputBuffer, numSamples, logger)) {  // Předání loggeru do Voice
            anyActive = true;
        }
    }
    if (anyActive) {
        logger.log("VoiceManager/processBlock", "info", 
                  "Processed block of " + std::to_string(numSamples) + 
                  " samples - " + std::to_string(countActiveVoices()) + " active voices");
    }
    return anyActive;
}

/**
 * @brief Najde voice pro MIDI notu (jednoduchá – vrátí první volnou).
 * @param midiNote MIDI nota (0-127).
 * @return Pointer na Voice, nebo první pokud žádná volná (steal).
 */
Voice* VoiceManager::findVoiceForNote(uint8_t midiNote) {
    for (auto& voice : voices_) {
        if (voice.getMidiNote() == midiNote && !voice.isActive()) {
            return &voice;
        }
    }
    // Pokud žádná volná, vrať první (steal)
    return &voices_[0];
}

/**
 * @brief Počítá aktivní voices.
 * @return Počet aktivních voice.
 */
int VoiceManager::countActiveVoices() const {
    int count = 0;
    for (const auto& voice : voices_) {
        if (voice.isActive()) ++count;
    }
    return count;
}