#include "voice_manager.h"
#include <algorithm>  // Pro std::min

VoiceManager::VoiceManager(int maxVoices, Logger& logger) : logger_(&logger) {
    voices_.reserve(maxVoices);
    for (int i = 0; i < maxVoices; ++i) {
        voices_.emplace_back(static_cast<uint8_t>(i % 128), *Voice::sharedLogger_);  // Použití public statického sharedLogger_
    }
    if (logger_) {
        logger_->log("VoiceManager/constructor", "info", "VoiceManager initialized with " + std::to_string(maxVoices) + " voices");
    }
}

VoiceManager::~VoiceManager() {
    for (auto& voice : voices_) {
        voice.cleanup();
    }
    if (logger_) {
        logger_->log("VoiceManager/destructor", "info", "VoiceManager destroyed");
    }
}

void VoiceManager::initializeAll(const InstrumentLoader& loader) {
    for (auto& voice : voices_) {
        uint8_t midiNote = voice.getMidiNote();  // Použití getteru místo přímého přístupu
        const Instrument& inst = loader.getInstrumentNote(midiNote);
        voice.initialize(inst);
    }
    if (logger_) {
        logger_->log("VoiceManager/initializeAll", "info", "All voices initialized");
    }
}

void VoiceManager::setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity) {
    // Najdi volnou voice pro note-on nebo specifickou pro note-off (zde zjednodušeně první)
    for (auto& voice : voices_) {
        if (voice.getMidiNote() == midiNote || (isOn && !voice.isActive())) {  // Použití getteru
            voice.setNoteState(isOn, velocity);
            break;
        }
    }
}

bool VoiceManager::processBlock(AudioBuffer& outputBuffer, int numSamples) {
    bool anyActive = false;
    // Inicializuj výstup nulami
    std::fill(outputBuffer.leftChannel, outputBuffer.leftChannel + numSamples, 0.0f);
    std::fill(outputBuffer.rightChannel, outputBuffer.rightChannel + numSamples, 0.0f);

    for (auto& voice : voices_) {
        if (voice.processBlock(outputBuffer, numSamples)) {
            anyActive = true;
        }
    }
    return anyActive;
}