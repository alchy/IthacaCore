#include "voice_manager.h"
#include "sampler.h"
#include "instrument_loader.h"
#include "envelopes/envelope_static_data.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

// Definice M_PI pro konzistenci
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// ===== CONSTANT POWER PANNING IMPLEMENTATION =====

namespace {
    constexpr int PAN_TABLE_SIZE = 128;
    float pan_left_gains[PAN_TABLE_SIZE];
    float pan_right_gains[PAN_TABLE_SIZE];
    bool pan_tables_initialized = false;
    
    void initializePanTables() {
        if (pan_tables_initialized) return;
        
        // Constant power panning using sin/cos curves
        // MIDI pan: 0 = hard left, 64 = center, 127 = hard right
        constexpr float PI_2 = M_PI / 2.0f;
        
        for (int i = 0; i < PAN_TABLE_SIZE; ++i) {
            // Normalize MIDI value (0-127) to angle (0 to π/2)
            float normalized = static_cast<float>(i) / 127.0f;
            float angle = normalized * PI_2;
            
            // Constant power panning curves
            pan_left_gains[i] = std::cos(angle);   // 1.0 at left, 0.707 at center, 0.0 at right
            pan_right_gains[i] = std::sin(angle);  // 0.0 at left, 0.707 at center, 1.0 at right
        }
        
        pan_tables_initialized = true;
    }
}

// ===== CONSTRUCTOR AND INITIALIZATION =====

VoiceManager::VoiceManager(const std::string& sampleDir, Logger& logger)
    : samplerIO_(),
      instrumentLoader_(),
      envelope_(),
      currentSampleRate_(0),
      sampleDir_(sampleDir),
      systemInitialized_(false),
      voices_(128),
      activeVoices_(),
      voicesToRemove_(),
      activeVoicesCount_(0),
      rtMode_(false) {
    
    // Validate sample directory
    if (sampleDir_.empty()) {
        const std::string errorMsg = "[VoiceManager/constructor] error: Invalid sampleDir - cannot be empty";
        logSafe("VoiceManager/constructor", "error", errorMsg, logger);
        std::exit(1);
    }
    
    // Validate EnvelopeStaticData initialization
    if (!EnvelopeStaticData::isInitialized()) {
        const std::string errorMsg = "[VoiceManager/constructor] error: EnvelopeStaticData not initialized. Call EnvelopeStaticData::initialize() first";
        logSafe("VoiceManager/constructor", "error", errorMsg, logger);
        std::exit(1);
    }
    
    // Initialize pan lookup tables
    initializePanTables();
    
    // Initialize voice pool (128 voices for all MIDI notes)
    for (int i = 0; i < 128; ++i) {
        voices_[i] = Voice(static_cast<uint8_t>(i));
    }
    
    // Pre-allocate management vectors
    activeVoices_.reserve(128);
    voicesToRemove_.reserve(128);

    // Setup error callback for envelope static data
    EnvelopeStaticData::setErrorCallback([&logger](const std::string& component, 
                                                   const std::string& severity, 
                                                   const std::string& message) {
        logger.log(component, severity, message);
    });
    
    logSafe("VoiceManager/constructor", "info", 
           "VoiceManager created with sampleDir '" + sampleDir_ + 
           "' using shared envelope data and constant power panning. Ready for initialization pipeline.", logger);
}

// ===== CONSTANT POWER PANNING =====

void VoiceManager::getPanGains(float pan, float& leftGain, float& rightGain) noexcept {
    // Clamp pan to valid range
    pan = std::max(-1.0f, std::min(1.0f, pan));
    
    // Convert pan (-1.0 to +1.0) to MIDI index (0 to 127)
    int panIndex = static_cast<int>((pan + 1.0f) * 63.5f);
    panIndex = std::max(0, std::min(127, panIndex));
    
    // Lookup from pre-calculated table
    leftGain = pan_left_gains[panIndex];
    rightGain = pan_right_gains[panIndex];
}

// ===== SYSTEM INITIALIZATION =====

void VoiceManager::initializeSystem(Logger& logger) {
    logSafe("VoiceManager/initializeSystem", "info", 
            "=== INIT PHASE 1: System initialization ===", logger);
    logSafe("VoiceManager/initializeSystem", "info", 
            "Scanning directory: " + sampleDir_, logger);
    
    try {
        samplerIO_.scanSampleDirectory(sampleDir_, logger);
        
        logSafe("VoiceManager/initializeSystem", "info", 
                "=== INIT PHASE 1: Completed successfully (envelope data shared) ===", logger);
        
    } catch (...) {
        const std::string errorMsg = "[VoiceManager/initializeSystem] error: INIT PHASE 1: System initialization failed";
        logSafe("VoiceManager/initializeSystem", "error", errorMsg, logger);
        std::exit(1);
    }
}

void VoiceManager::loadForSampleRate(int sampleRate, Logger& logger) {
    logSafe("VoiceManager/loadForSampleRate", "info", 
            "=== INIT PHASE 2: Loading for sample rate " + std::to_string(sampleRate) + " Hz ===", logger);
    
    // Validate sample rate
    if (sampleRate != 44100 && sampleRate != 48000) {
        const std::string errorMsg = "[VoiceManager/loadForSampleRate] error: Invalid sample rate " + 
                                   std::to_string(sampleRate) + " Hz - only 44100 Hz and 48000 Hz are supported";
        logSafe("VoiceManager/loadForSampleRate", "error", errorMsg, logger);
        std::exit(1);
    }
    
    try {
        currentSampleRate_ = sampleRate;
        
        // Load instrument samples for specified sample rate
        instrumentLoader_.loadInstrumentData(samplerIO_, sampleRate, logger);
        
        // Initialize all voices with loaded instruments
        initializeVoicesWithInstruments(logger);
        
        instrumentLoader_.validateStereoConsistency(logger);
        systemInitialized_ = true;
        
        logSafe("VoiceManager/loadForSampleRate", "info", 
                "=== INIT PHASE 2: Completed successfully (using shared envelope data) ===", logger);
        
    } catch (...) {
        const std::string errorMsg = "[VoiceManager/loadForSampleRate] error: INIT PHASE 2: Loading failed";
        logSafe("VoiceManager/loadForSampleRate", "error", errorMsg, logger);
        std::exit(1);
    }
}

// ===== SAMPLE RATE MANAGEMENT =====

void VoiceManager::changeSampleRate(int newSampleRate, Logger& logger) {
    logSafe("VoiceManager/changeSampleRate", "info", 
            "Requested sample rate change to " + std::to_string(newSampleRate) + " Hz", logger);
    
    if (currentSampleRate_ == newSampleRate) {
        logSafe("VoiceManager/changeSampleRate", "info", 
                "Sample rate unchanged: " + std::to_string(newSampleRate) + " Hz", logger);
        return;
    }
    
    stopAllVoices();
    loadForSampleRate(newSampleRate, logger);
    
    logSafe("VoiceManager/changeSampleRate", "info", 
            "Sample rate successfully changed to " + std::to_string(newSampleRate) + " Hz", logger);
}

void VoiceManager::prepareToPlay(int maxBlockSize) noexcept {
    for (int i = 0; i < 128; ++i) {
        voices_[i].prepareToPlay(maxBlockSize);
    }
}

// ===== CORE AUDIO API =====

void VoiceManager::setNoteStateMIDI(uint8_t midiNote, bool isOn, uint8_t velocity) noexcept {
    if (!isValidMidiNote(midiNote)) return;
    
    Voice& voice = voices_[midiNote];
    
    if (isOn) {
        if (!voice.isActive()) {
            addActiveVoice(&voice);
        }
        voice.setNoteState(true, velocity);
    } else {
        voice.setNoteState(false, velocity);
    }
}

void VoiceManager::setNoteStateMIDI(uint8_t midiNote, bool isOn) noexcept {
    if (!isValidMidiNote(midiNote)) return;
    
    Voice& voice = voices_[midiNote];
    
    if (isOn) {
        if (!voice.isActive()) {
            addActiveVoice(&voice);
        }
        voice.setNoteState(true);
    } else {
        voice.setNoteState(false);
    }
}

// ===== AUDIO PROCESSING =====

bool VoiceManager::processBlockUninterleaved(float* outputLeft, float* outputRight, int samplesPerBlock) noexcept {
    if (!outputLeft || !outputRight || samplesPerBlock <= 0) return false;
    
    // Clear output buffers
    std::fill(outputLeft, outputLeft + samplesPerBlock, 0.0f);
    std::fill(outputRight, outputRight + samplesPerBlock, 0.0f);
    
    if (activeVoices_.empty()) return false;
    
    bool anyActive = false;
    
    // Process all active voices
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            if (voice->processBlock(outputLeft, outputRight, samplesPerBlock)) {
                anyActive = true;
            } else {
                voicesToRemove_.push_back(voice);
            }
        } else {
            voicesToRemove_.push_back(voice);
        }
    }
    
    // Clean up inactive voices
    if (!voicesToRemove_.empty()) {
        cleanupInactiveVoices();
    }
    
    return anyActive;
}

bool VoiceManager::processBlockInterleaved(AudioData* outputBuffer, int samplesPerBlock) noexcept {
    if (!outputBuffer || samplesPerBlock <= 0) return false;
    
    // Clear output buffer
    for (int i = 0; i < samplesPerBlock; ++i) {
        outputBuffer[i].left = 0.0f;
        outputBuffer[i].right = 0.0f;
    }
    
    if (activeVoices_.empty()) return false;
    
    // Pre-allocated temp buffers to avoid RT allocations
    static thread_local std::vector<float> tempLeft(16384);
    static thread_local std::vector<float> tempRight(16384);
    
    bool anyActive = false;
    
    // Process all active voices
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            // Verify buffer capacity - critical error must be visible in development
            if (tempLeft.size() < static_cast<size_t>(samplesPerBlock)) {
                std::cout << "[VoiceManager/processBlockInterleaved] error: Temp buffer too small - need " 
                          << samplesPerBlock << " samples, have " << tempLeft.size() << std::endl;
                continue; // Skip this voice but log the issue
            }
            
            // Clear temp buffers
            std::fill(tempLeft.begin(), tempLeft.begin() + samplesPerBlock, 0.0f);
            std::fill(tempRight.begin(), tempRight.begin() + samplesPerBlock, 0.0f);
            
            // Process voice and mix to output
            if (voice->processBlock(tempLeft.data(), tempRight.data(), samplesPerBlock)) {
                anyActive = true;
                
                for (int i = 0; i < samplesPerBlock; ++i) {
                    outputBuffer[i].left += tempLeft[i];
                    outputBuffer[i].right += tempRight[i];
                }
            } else {
                voicesToRemove_.push_back(voice);
            }
        } else {
            voicesToRemove_.push_back(voice);
        }
    }
    
    // Clean up inactive voices
    if (!voicesToRemove_.empty()) {
        cleanupInactiveVoices();
    }
    
    return anyActive;
}

// ===== VOICE CONTROL =====

void VoiceManager::stopAllVoices() noexcept {
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            voice->setNoteState(false, 0);
        }
    }
}

void VoiceManager::resetAllVoices(Logger& logger) {
    for (int i = 0; i < 128; ++i) {
        voices_[i].cleanup(logger);
    }
    
    activeVoices_.clear();
    voicesToRemove_.clear();
    activeVoicesCount_.store(0);
    
    logSafe("VoiceManager/resetAllVoices", "info", "Reset all 128 voices to idle state", logger);
}

// ===== GLOBAL VOICE PARAMETERS =====

void VoiceManager::setAllVoicesMasterGainMIDI(uint8_t midi_gain, Logger& logger) {
    if (midi_gain > 127) {
        const std::string errorMsg = "[VoiceManager/setAllVoicesMasterGain] error: Invalid master MIDI gain " + 
                                   std::to_string(midi_gain) + " (must be 0-127)";
        logSafe("VoiceManager/setAllVoicesMasterGain", "error", errorMsg, logger);
        return;
    }
    
    float gain = midi_gain / 127.0f;

    for (int i = 0; i < 128; ++i) {
        voices_[i].setMasterGain(gain, logger);
    }
    
    logSafe("VoiceManager/setAllVoicesMasterGain", "info", 
           "Master gain set to " + std::to_string(gain) + " for all voices", logger);
}

void VoiceManager::setAllVoicesPanMIDI(uint8_t midi_pan) noexcept {
    if (midi_pan > 127) return;

    float pan = (midi_pan - 64.0f) / 63.0f;
    
    for (int i = 0; i < 128; ++i) {
        voices_[i].setPan(pan);
    }
}

void VoiceManager::setAllVoicesAttackMIDI(uint8_t midi_attack) noexcept {
    if (midi_attack > 127) return;
    
    for (int i = 0; i < 128; ++i) {
        voices_[i].setAttackMIDI(midi_attack);
    }
}

void VoiceManager::setAllVoicesReleaseMIDI(uint8_t midi_release) noexcept {
    if (midi_release > 127) return;
    
    for (int i = 0; i < 128; ++i) {
        voices_[i].setReleaseMIDI(midi_release);
    }
}

void VoiceManager::setAllVoicesSustainLevelMIDI(uint8_t midi_sustain) noexcept {
    if (midi_sustain > 127) return;
    
    for (int i = 0; i < 128; ++i) {
        voices_[i].setSustainLevelMIDI(midi_sustain);
    }
}

// ===== VOICE ACCESS AND STATISTICS =====

Voice& VoiceManager::getVoiceMIDI(uint8_t midiNote) noexcept {
#ifdef _DEBUG
    if (!isValidMidiNote(midiNote)) return voices_[0];
#endif
    return voices_[midiNote];
}

const Voice& VoiceManager::getVoiceMIDI(uint8_t midiNote) const noexcept {
#ifdef _DEBUG
    if (!isValidMidiNote(midiNote)) return voices_[0];
#endif
    return voices_[midiNote];
}

int VoiceManager::getSustainingVoicesCount() const noexcept {
    int count = 0;
    for (const Voice* voice : activeVoices_) {
        if (voice && voice->getState() == VoiceState::Sustaining) ++count;
    }
    return count;
}

int VoiceManager::getReleasingVoicesCount() const noexcept {
    int count = 0;
    for (const Voice* voice : activeVoices_) {
        if (voice && voice->getState() == VoiceState::Releasing) ++count;
    }
    return count;
}

// ===== RT MODE CONTROL =====

void VoiceManager::setRealTimeMode(bool enabled) noexcept {
    rtMode_.store(enabled);
    Voice::setRealTimeMode(enabled);
}

// ===== SYSTEM DIAGNOSTICS =====

void VoiceManager::logSystemStatistics(Logger& logger) {
    if (!systemInitialized_) {
        logSafe("VoiceManager/logSystemStatistics", "warn", 
                "System not initialized - limited statistics available", logger);
        return;
    }
    
    const int totalSamples = instrumentLoader_.getTotalLoadedSamples();
    const int monoSamples = instrumentLoader_.getMonoSamplesCount();
    const int stereoSamples = instrumentLoader_.getStereoSamplesCount();
    const int actualSampleRate = instrumentLoader_.getActualSampleRate();
    
    const int activeCount = getActiveVoicesCount();
    const int sustaining = getSustainingVoicesCount();
    const int releasing = getReleasingVoicesCount();
    
    logSafe("VoiceManager/statistics", "info", "=== SYSTEM STATISTICS ===", logger);
    logSafe("VoiceManager/statistics", "info", "Sample Rate: " + std::to_string(actualSampleRate) + " Hz", logger);
    logSafe("VoiceManager/statistics", "info", "Total loaded samples: " + std::to_string(totalSamples), logger);
    logSafe("VoiceManager/statistics", "info", "Originally mono: " + std::to_string(monoSamples) + 
            ", Originally stereo: " + std::to_string(stereoSamples), logger);
    logSafe("VoiceManager/statistics", "info", "Voice Activity - Active: " + std::to_string(activeCount) + 
            ", Sustaining: " + std::to_string(sustaining) + ", Releasing: " + std::to_string(releasing), logger);
    logSafe("VoiceManager/statistics", "info", "========================", logger);
}

// ===== PRIVATE HELPER METHODS =====

void VoiceManager::initializeVoicesWithInstruments(Logger& logger) {
    logSafe("VoiceManager/initializeVoicesWithInstruments", "info", 
            "Initializing all 128 voices with loaded instruments and shared envelope system...", logger);
    
    for (int i = 0; i < 128; ++i) {
        uint8_t midiNote = static_cast<uint8_t>(i);
        const Instrument& inst = instrumentLoader_.getInstrumentNote(midiNote);
        Voice& voice = voices_[i];
        
        // Initialize voice with per-instance envelope wrapper
        voice.initialize(inst, currentSampleRate_, envelope_, logger);
        voice.prepareToPlay(512);
    }

    // Set default envelope parameters for all voices
    setAllVoicesAttackMIDI(0);      // Fast attack
    setAllVoicesReleaseMIDI(4);     // Short release
    setAllVoicesSustainLevelMIDI(127); // Full sustain
    
    logSafe("VoiceManager/initializeVoicesWithInstruments", "info", 
            "All voices initialized with instruments and shared envelope system successfully", logger);
}

bool VoiceManager::needsReinitialization(int targetSampleRate) const noexcept {
    return (currentSampleRate_ != targetSampleRate) || 
           (instrumentLoader_.getActualSampleRate() != targetSampleRate);
}

void VoiceManager::reinitializeIfNeeded(int targetSampleRate, Logger& logger) {
    if (needsReinitialization(targetSampleRate)) {
        changeSampleRate(targetSampleRate, logger);
    }
}

// ===== VOICE POOL MANAGEMENT =====

void VoiceManager::addActiveVoice(Voice* voice) noexcept {
    if (!voice) return;
    
    auto it = std::find(activeVoices_.begin(), activeVoices_.end(), voice);
    if (it == activeVoices_.end()) {
        activeVoices_.push_back(voice);
        activeVoicesCount_.fetch_add(1);
    }
}

void VoiceManager::removeActiveVoice(Voice* voice) noexcept {
    if (!voice) return;
    
    auto it = std::find(activeVoices_.begin(), activeVoices_.end(), voice);
    if (it != activeVoices_.end()) {
        // Swap-and-pop optimization: O(1) instead of O(n)
        if (it != activeVoices_.end() - 1) {
            *it = activeVoices_.back();  // Swap with last element
        }
        activeVoices_.pop_back();        // Remove last element
        activeVoicesCount_.fetch_sub(1);
    }
}

void VoiceManager::cleanupInactiveVoices() noexcept {
    for (Voice* voice : voicesToRemove_) {
        removeActiveVoice(voice);
    }
    voicesToRemove_.clear();
}

void VoiceManager::logSafe(const std::string& component, const std::string& severity, 
                          const std::string& message, Logger& logger) const {
    if (!rtMode_.load()) {
        // Non-RT context: use proper logger
        logger.log(component, severity, message);
    } else {
        // RT context or development: always log to stdout for visibility
        std::cout << "[" << component << "] " << severity << ": " << message << std::endl;
    }
}