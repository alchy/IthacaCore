#include "voice_manager.h"
#include "sampler.h"
#include "instrument_loader.h"
#include "envelopes/envelope_static_data.h"
#include "pan.h"
#include "lfopan.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

// ===== CONSTRUCTOR AND INITIALIZATION =====

VoiceManager::VoiceManager(const std::string& sampleDir, Logger& logger, int velocityLayerCount)
    : samplerIO_(),
      instrumentLoader_(),
      envelope_(),
      currentSampleRate_(0),
      sampleDir_(sampleDir),
      systemInitialized_(false),
      velocityLayerCount_(velocityLayerCount),  // Nově
      voices_(128),
      activeVoices_(),
      voicesToRemove_(),
      activeVoicesCount_(0),
      rtMode_(false),
      sustainPedalActive_(false),
      delayedNoteOffs_(),  // Initialized to all false
      panSpeed_(0.0f),
      panSpeedTarget_(0.0f),
      panDepth_(0.0f),
      panDepthTarget_(0.0f),
      panSmoothingTime_(0.5f),  // 500 ms default smoothing for both speed and depth
      lfoPhase_(0.0f),
      lfoPanBuffer_(),
      dspChain_(),
      limiterEffect_(nullptr),
      previousPanLeft_(1.0f),  // Inicializace předchozích gainů
      previousPanRight_(1.0f) {

    // Validate velocity layer count
    if (velocityLayerCount_ < 1 || velocityLayerCount_ > 8) {
        logger.log("VoiceManager/constructor", LogSeverity::Warning,
                  "Invalid velocity layer count " + std::to_string(velocityLayerCount_) +
                  ", using default 8");
        velocityLayerCount_ = 8;
    }

    // Validate sample directory
    if (sampleDir_.empty()) {
        const std::string errorMsg = "[VoiceManager/constructor] error: Invalid sampleDir - cannot be empty";
        logger.log("VoiceManager/constructor", LogSeverity::Error, errorMsg);
        std::exit(1);
    }

    // Validate EnvelopeStaticData initialization
    if (!EnvelopeStaticData::isInitialized()) {
        const std::string errorMsg = "[VoiceManager/constructor] error: EnvelopeStaticData not initialized. Call EnvelopeStaticData::initialize() first";
        logger.log("VoiceManager/constructor", LogSeverity::Error, errorMsg);
        std::exit(1);
    }
    
    // Initialize pan lookup tables
    Panning::initializePanTables();
    
    // Initialize LFO panning lookup tables
    LfoPanning::initializeLfoTables();
    
    // Initialize voice pool (128 voices for all MIDI notes)
    for (int i = 0; i < 128; ++i) {
        voices_[i] = Voice(static_cast<uint8_t>(i));
    }
    
    // Pre-allocate management vectors
    activeVoices_.reserve(128);
    voicesToRemove_.reserve(128);

    // Initialize delayed note-off flags to false
    delayedNoteOffs_.fill(false);

    // Setup error callback for envelope static data
    EnvelopeStaticData::setErrorCallback([&logger](const std::string& component,
                                                   LogSeverity severity,
                                                   const std::string& message) {
        logger.log(component, severity, message);
    });

    // Create and add DSP effects in processing order
    // 1. BBE Maximizer (enhancement - must come before limiter)
    auto bbe = std::make_unique<BBEProcessor>();
    bbeEffect_ = bbe.get();  // Uložit quick pointer
    dspChain_.addEffect(std::move(bbe));

    // 2. Limiter (protection - must be last!)
    auto limiter = std::make_unique<Limiter>();
    limiterEffect_ = limiter.get();  // Uložit quick pointer
    dspChain_.addEffect(std::move(limiter));

    logger.log("VoiceManager/constructor", LogSeverity::Info,
           "VoiceManager created with sampleDir '" + sampleDir_ + "', " +
           std::to_string(velocityLayerCount_) + " velocity layers, " +
           "using shared envelope data, constant power panning, LFO panning, sustain pedal support, and DSP effects chain (BBE Maximizer + Limiter). Ready for initialization pipeline.");
}

// ===== CONSTANT POWER PANNING =====

void VoiceManager::getPanGains(float pan, float& leftGain, float& rightGain) noexcept {
    Panning::getPanGains(pan, leftGain, rightGain);
}

// ===== INITIALIZATION PIPELINE =====

void VoiceManager::initializeSystem(Logger& logger) {
    logger.log("VoiceManager/initializeSystem", LogSeverity::Info,
            "=== INIT PHASE 1: System initialization and directory scanning ===");

    try {
        samplerIO_.scanSampleDirectory(sampleDir_, logger);

        // Check if samples were loaded by checking if we can find at least one valid sample
        const auto& sampleList = samplerIO_.getLoadedSampleList();
        if (sampleList.empty()) {
            const std::string errorMsg = "[VoiceManager/initializeSystem] error: No valid samples found in directory '" + sampleDir_ + "'";
            logger.log("VoiceManager/initializeSystem", LogSeverity::Error, errorMsg);
            std::exit(1);
        }

        // Set velocity layer count in InstrumentLoader
        instrumentLoader_.setVelocityLayerCount(velocityLayerCount_);

        systemInitialized_ = true;

        logger.log("VoiceManager/initializeSystem", LogSeverity::Info,
                "=== INIT PHASE 1 COMPLETED: Sample directory scanned successfully ===");
        
    } catch (...) {
        const std::string errorMsg = "[VoiceManager/initializeSystem] error: INIT PHASE 1: System initialization failed";
        logger.log("VoiceManager/initializeSystem", LogSeverity::Error, errorMsg);
        std::exit(1);
    }
}

void VoiceManager::loadForSampleRate(int sampleRate, Logger& logger) {
    if (!systemInitialized_) {
        const std::string errorMsg = "[VoiceManager/loadForSampleRate] error: Cannot load samples - system not initialized. Call initializeSystem() first";
        logger.log("VoiceManager/loadForSampleRate", LogSeverity::Error, errorMsg);
        std::exit(1);
    }
    
    logger.log("VoiceManager/loadForSampleRate", LogSeverity::Info, 
            "=== INIT PHASE 2: Loading sample data for " + std::to_string(sampleRate) + " Hz ===");
    
    try {
        instrumentLoader_.loadInstrumentData(samplerIO_, sampleRate, logger);
        
        // Check if any samples were actually loaded
        if (instrumentLoader_.getTotalLoadedSamples() == 0) {
            const std::string errorMsg = "[VoiceManager/loadForSampleRate] error: Failed to load any instrument data";
            logger.log("VoiceManager/loadForSampleRate", LogSeverity::Error, errorMsg);
            std::exit(1);
        }
        
        currentSampleRate_ = sampleRate;
        initializeVoicesWithInstruments(logger);
        
        logger.log("VoiceManager/loadForSampleRate", LogSeverity::Info, 
                "=== INIT PHASE 2 COMPLETED: All 128 voices initialized with sample data (with shared envelope data and LFO panning) ===");
        
    } catch (...) {
        const std::string errorMsg = "[VoiceManager/loadForSampleRate] error: INIT PHASE 2: Loading failed";
        logger.log("VoiceManager/loadForSampleRate", LogSeverity::Error, errorMsg);
        std::exit(1);
    }
}

// ===== SAMPLE RATE MANAGEMENT =====

void VoiceManager::changeSampleRate(int newSampleRate, Logger& logger) {
    logger.log("VoiceManager/changeSampleRate", LogSeverity::Info, 
            "Requested sample rate change to " + std::to_string(newSampleRate) + " Hz");
    
    if (currentSampleRate_ == newSampleRate) {
        logger.log("VoiceManager/changeSampleRate", LogSeverity::Info, 
                "Sample rate unchanged: " + std::to_string(newSampleRate) + " Hz");
        return;
    }
    
    stopAllVoices();
    loadForSampleRate(newSampleRate, logger);
    
    logger.log("VoiceManager/changeSampleRate", LogSeverity::Info, 
            "Sample rate successfully changed to " + std::to_string(newSampleRate) + " Hz");
}

void VoiceManager::prepareToPlay(int maxBlockSize) noexcept {
    for (int i = 0; i < 128; ++i) {
        voices_[i].prepareToPlay(maxBlockSize);
    }

    // Prepare DSP chain
    if (currentSampleRate_ > 0) {
        dspChain_.prepare(currentSampleRate_, maxBlockSize);
    }
}

// ===== CORE AUDIO API =====

void VoiceManager::setNoteStateMIDI(uint8_t midiNote, bool isOn, uint8_t velocity) noexcept {
    if (!isValidMidiNote(midiNote)) return;
    
    Voice& voice = voices_[midiNote];
    
    if (isOn) {
        // ===== NOTE-ON =====
        // Don't touch delayed flag - if it's set, the voice will handle
        // the retrigger via damping buffer, and pedal release will still work correctly
        
        // Add to active voices if not already active
        if (!voice.isActive()) {
            addActiveVoice(&voice);
        }
        
        // Start the note (voice handles retrigger internally if already playing)
        voice.setNoteState(true, velocity);
        
    } else {
        // ===== NOTE-OFF =====
        
        if (sustainPedalActive_.load()) {
            // Sustain pedal is pressed → delay the note-off
            delayedNoteOffs_[midiNote] = true;
            // Do NOT send note-off to the voice yet!
            
        } else {
            // Sustain pedal is not pressed → normal note-off
            voice.setNoteState(false, velocity);
        }
    }
}

void VoiceManager::setNoteStateMIDI(uint8_t midiNote, bool isOn) noexcept {
    if (!isValidMidiNote(midiNote)) return;
    
    Voice& voice = voices_[midiNote];
    
    if (isOn) {
        // ===== NOTE-ON =====
        // Don't touch delayed flag - if it's set, the voice will handle
        // the retrigger via damping buffer, and pedal release will still work correctly
        
        // Add to active voices if not already active
        if (!voice.isActive()) {
            addActiveVoice(&voice);
        }
        
        // Start the note with default velocity (voice handles retrigger internally)
        voice.setNoteState(true);
        
    } else {
        // ===== NOTE-OFF =====
        
        if (sustainPedalActive_.load()) {
            // Sustain pedal is pressed → delay the note-off
            delayedNoteOffs_[midiNote] = true;
            // Do NOT send note-off to the voice yet!
            
        } else {
            // Sustain pedal is not pressed → normal note-off
            voice.setNoteState(false);
        }
    }
}

// ===== SUSTAIN PEDAL API =====

void VoiceManager::setSustainPedalMIDI(bool pedalDown) noexcept {
    // Get previous state before updating
    bool wasActive = sustainPedalActive_.load();
    
    // Update sustain pedal state
    sustainPedalActive_.store(pedalDown);
    
    // ===== PEDAL RELEASED (transition from ON → OFF) =====
    if (wasActive && !pedalDown) {
        // Process all delayed note-offs
        processDelayedNoteOffs();
    }
    
    // ===== PEDAL PRESSED (transition from OFF → ON) =====
    // No special action needed - just the flag update above
}

void VoiceManager::processDelayedNoteOffs() noexcept {
    // Iterate through all MIDI notes and send note-off to voices with delayed flags
    for (uint8_t midiNote = 0; midiNote < 128; ++midiNote) {
        if (delayedNoteOffs_[midiNote]) {
            // Send note-off to this voice
            Voice& voice = voices_[midiNote];
            voice.setNoteState(false, 0);
            
            // Clear the delayed note-off flag
            delayedNoteOffs_[midiNote] = false;
        }
    }
}

// ===== AUDIO PROCESSING =====

bool VoiceManager::processBlockUninterleaved(float* outputLeft, float* outputRight, int samplesPerBlock) noexcept {
    // Ověření vstupů
    if (!outputLeft || !outputRight || samplesPerBlock <= 0) return false;

    // Vynulování výstupních bufferů
    std::fill(outputLeft, outputLeft + samplesPerBlock, 0.0f);
    std::fill(outputRight, outputRight + samplesPerBlock, 0.0f);

    if (activeVoices_.empty()) return false;

    bool anyActive = false;

    // Zpracování všech aktivních hlasů bez LFO panningu
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

    // Vyčištění neaktivních hlasů
    if (!voicesToRemove_.empty()) {
        cleanupInactiveVoices();
    }

    // Aplikace LFO panningu na finální mix
    // Always process LFO (runs continuously, even when speed/depth are 0)
    // This simplifies logic and prevents discontinuities
    applyLfoPanningPerSample(samplesPerBlock);
    applyLfoPanToFinalMix(outputLeft, outputRight, samplesPerBlock);

    // Aplikace DSP řetězce (limiter atd.), pokud máme audio
    if (anyActive) {
        dspChain_.process(outputLeft, outputRight, samplesPerBlock);
    }

    return anyActive;
}

bool VoiceManager::processBlockInterleaved(AudioData* outputBuffer, int samplesPerBlock) noexcept {
    // Ověření vstupů
    if (!outputBuffer || samplesPerBlock <= 0) return false;

    // Vynulování výstupního bufferu
    for (int i = 0; i < samplesPerBlock; ++i) {
        outputBuffer[i].left = 0.0f;
        outputBuffer[i].right = 0.0f;
    }

    if (activeVoices_.empty()) return false;

    // Předalokované dočasné buffery pro RT bezpečnost
    static thread_local std::vector<float> tempLeft(16384);
    static thread_local std::vector<float> tempRight(16384);

    bool anyActive = false;

    // Zpracování všech aktivních hlasů bez LFO panningu
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            // Ověření kapacity bufferu
            if (tempLeft.size() < static_cast<size_t>(samplesPerBlock)) {
                std::cout << "[VoiceManager/processBlockInterleaved] error: Temp buffer too small - need "
                          << samplesPerBlock << " samples, have " << tempLeft.size() << std::endl;
                continue;
            }

            // Vynulování dočasných bufferů
            std::fill(tempLeft.begin(), tempLeft.begin() + samplesPerBlock, 0.0f);
            std::fill(tempRight.begin(), tempRight.begin() + samplesPerBlock, 0.0f);

            // Zpracování hlasu a mix do výstupu
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

    // Vyčištění neaktivních hlasů
    if (!voicesToRemove_.empty()) {
        cleanupInactiveVoices();
    }

    // Aplikace LFO panningu na finální mix
    // Always process LFO (runs continuously, even when speed/depth are 0)
    applyLfoPanningPerSample(samplesPerBlock);

    // Převod prokládaného bufferu na neprokládaný pro LFO panning
    std::vector<float> tempLeftOut(samplesPerBlock);
    std::vector<float> tempRightOut(samplesPerBlock);
    for (int i = 0; i < samplesPerBlock; ++i) {
        tempLeftOut[i] = outputBuffer[i].left;
        tempRightOut[i] = outputBuffer[i].right;
    }
    applyLfoPanToFinalMix(tempLeftOut.data(), tempRightOut.data(), samplesPerBlock);
    for (int i = 0; i < samplesPerBlock; ++i) {
        outputBuffer[i].left = tempLeftOut[i];
        outputBuffer[i].right = tempRightOut[i];
    }

    return anyActive;
}

void VoiceManager::applyLfoPanToFinalMix(float* leftOut, float* rightOut, int numSamples) noexcept {
    // Aplikace LFO panningu na finální mix s vyhlazováním gainu
    // Always update previousPan state to prevent discontinuities when depth changes from 0

    float currentPanLeft = previousPanLeft_;
    float currentPanRight = previousPanRight_;

    for (int i = 0; i < numSamples; ++i) {
        // Získání panning gainů z lookup tabulky (již obsahuje interpolovaný depth)
        float panLeft, panRight;
        Panning::getPanGains(lfoPanBuffer_[i], panLeft, panRight);

        // Exponenciální vyhlazování gainů pro eliminaci zip noise
        currentPanLeft = (LFO_SMOOTHING * currentPanLeft) + ((1.0f - LFO_SMOOTHING) * panLeft);
        currentPanRight = (LFO_SMOOTHING * currentPanRight) + ((1.0f - LFO_SMOOTHING) * panRight);

        // Aplikace gainů na výstupní vzorky
        leftOut[i] *= currentPanLeft;
        rightOut[i] *= currentPanRight;
    }

    // Uložení aktuálních gainů pro další blok
    previousPanLeft_ = currentPanLeft;
    previousPanRight_ = currentPanRight;
}

// ===== VOICE CONTROL =====

void VoiceManager::stopAllVoices() noexcept {
    for (Voice* voice : activeVoices_) {
        if (voice && voice->isActive()) {
            voice->setNoteState(false, 0);
        }
    }
    
    // Clear all delayed note-offs
    delayedNoteOffs_.fill(false);
}

void VoiceManager::resetAllVoices(Logger& logger) {
    for (int i = 0; i < 128; ++i) {
        voices_[i].cleanup(logger);
    }
    
    activeVoices_.clear();
    voicesToRemove_.clear();
    activeVoicesCount_.store(0);
    
    // Reset sustain pedal state
    sustainPedalActive_.store(false);
    delayedNoteOffs_.fill(false);
    
    // Reset LFO parameters
    resetLfoParameters();
    
    logger.log("VoiceManager/resetAllVoices", LogSeverity::Info,
           "Reset all 128 voices to idle state, cleared sustain pedal state, and reset LFO parameters");
}

// ===== GLOBAL VOICE PARAMETERS =====

void VoiceManager::setAllVoicesMasterGainMIDI(uint8_t midi_gain, Logger& logger) {
    if (midi_gain > 127) {
        const std::string errorMsg = "[VoiceManager/setAllVoicesMasterGain] error: Invalid master MIDI gain " + 
                                   std::to_string(midi_gain) + " (must be 0-127)";
        logger.log("VoiceManager/setAllVoicesMasterGain", LogSeverity::Error, errorMsg);
        return;
    }
    
    float gain = midi_gain / 127.0f;

    for (int i = 0; i < 128; ++i) {
        voices_[i].setMasterGain(gain);
    }
    
    logger.log("VoiceManager/setAllVoicesMasterGain", LogSeverity::Info, 
           "Master gain set to " + std::to_string(gain) + " for all voices");
}

void VoiceManager::setAllVoicesPanMIDI(uint8_t midi_pan) noexcept {
    if (midi_pan > 127) return;
    
    float pan = (midi_pan - 64.0f) / 63.0f;
    
    // Static pan is overridden by LFO panning if active
    if (!isLfoPanningActive()) {
        for (int i = 0; i < 128; ++i) {
            voices_[i].setPan(pan);
        }
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

void VoiceManager::setAllVoicesStereoFieldAmountMIDI(uint8_t midi_stereo) noexcept {
    if (midi_stereo > 127) return;
    
    for (int i = 0; i < 128; ++i) {
        voices_[i].setStereoFieldAmountMIDI(midi_stereo);
    }
}

// ===== LFO PANNING CONTROL =====

void VoiceManager::setAllVoicesPanSpeedMIDI(uint8_t midi_speed) noexcept {
    if (midi_speed > 127) return;

    panSpeedTarget_ = LfoPanning::getFrequencyFromMIDI(midi_speed);
    // panSpeed_ will be smoothly interpolated to panSpeedTarget_ in applyLfoPanningPerSample()
    // No special handling needed - LFO runs continuously regardless of speed value
}

void VoiceManager::setAllVoicesPanDepthMIDI(uint8_t midi_depth) noexcept {
    if (midi_depth > 127) return;

    panDepthTarget_ = LfoPanning::getDepthFromMIDI(midi_depth);
    // panDepth_ will be smoothly interpolated to panDepthTarget_ in applyLfoPanningPerSample()
}

bool VoiceManager::isLfoPanningActive() const noexcept {
    // LFO is active if speed is set AND either current or target depth is non-zero
    // This ensures LFO continues processing during smooth transitions
    return (panSpeed_ > 0.0f) && ((panDepth_ > 0.0f) || (panDepthTarget_ > 0.0f));
}

// ===== VOICE ACCESS =====

Voice& VoiceManager::getVoiceMIDI(uint8_t midiNote) noexcept {
#ifdef _DEBUG
    if (!isValidMidiNote(midiNote)) return voices_[0];
#endif
    return voices_[midiNote];
}

// ===== STATISTICS =====

int VoiceManager::getSustainingVoicesCount() const noexcept {
    int count = 0;
    for (const Voice* voice : activeVoices_) {
        if (voice && voice->getState() == VoiceState::Sustaining) {
            ++count;
        }
    }
    return count;
}

int VoiceManager::getReleasingVoicesCount() const noexcept {
    int count = 0;
    for (const Voice* voice : activeVoices_) {
        if (voice && voice->getState() == VoiceState::Releasing) {
            ++count;
        }
    }
    return count;
}

// ===== REAL-TIME MODE =====

void VoiceManager::setRealTimeMode(bool enabled) noexcept {
    rtMode_.store(enabled);
}

// ===== SYSTEM DIAGNOSTICS =====

void VoiceManager::logSystemStatistics(Logger& logger) {
    logger.log("VoiceManager/statistics", LogSeverity::Info, "========================");
    logger.log("VoiceManager/statistics", LogSeverity::Info, "VoiceManager Statistics:");
    logger.log("VoiceManager/statistics", LogSeverity::Info, "========================");
    
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "Sample Directory: " + sampleDir_);
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "Current Sample Rate: " + std::to_string(currentSampleRate_) + " Hz");
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "System Initialized: " + std::string(systemInitialized_ ? "Yes" : "No"));
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "Real-Time Mode: " + std::string(rtMode_.load() ? "Enabled" : "Disabled"));
    
    logger.log("VoiceManager/statistics", LogSeverity::Info, "------------------------");
    logger.log("VoiceManager/statistics", LogSeverity::Info, "Voice Pool Status:");
    logger.log("VoiceManager/statistics", LogSeverity::Info, "------------------------");
    
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "Total Voices: 128");
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "Active Voices: " + std::to_string(getActiveVoicesCount()));
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "Sustaining Voices: " + std::to_string(getSustainingVoicesCount()));
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "Releasing Voices: " + std::to_string(getReleasingVoicesCount()));
    
    logger.log("VoiceManager/statistics", LogSeverity::Info, "------------------------");
    logger.log("VoiceManager/statistics", LogSeverity::Info, "Sustain Pedal Status:");
    logger.log("VoiceManager/statistics", LogSeverity::Info, "------------------------");
    
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "Pedal Active: " + std::string(sustainPedalActive_.load() ? "Yes" : "No"));
    
    // Count delayed note-offs
    int delayedCount = 0;
    for (int i = 0; i < 128; ++i) {
        if (delayedNoteOffs_[i]) {
            ++delayedCount;
        }
    }
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "Delayed Note-Offs: " + std::to_string(delayedCount));
    
    logger.log("VoiceManager/statistics", LogSeverity::Info, "------------------------");
    logger.log("VoiceManager/statistics", LogSeverity::Info, "LFO Panning Status:");
    logger.log("VoiceManager/statistics", LogSeverity::Info, "------------------------");
    
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "LFO Speed: " + std::to_string(panSpeed_) + " Hz");
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "LFO Depth: " + std::to_string(panDepth_));
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "LFO Phase: " + std::to_string(lfoPhase_) + " radians");
    logger.log("VoiceManager/statistics", LogSeverity::Info, 
           "LFO Active: " + std::string(isLfoPanningActive() ? "Yes" : "No"));
    
    logger.log("VoiceManager/statistics", LogSeverity::Info, "========================");
}

// ===== PRIVATE HELPER METHODS =====

void VoiceManager::initializeVoicesWithInstruments(Logger& logger) {
    logger.log("VoiceManager/initializeVoicesWithInstruments", LogSeverity::Info, 
            "Initializing all 128 voices with loaded instruments and shared envelope system...");
    
    for (int i = 0; i < 128; ++i) {
        uint8_t midiNote = static_cast<uint8_t>(i);
        const Instrument& inst = instrumentLoader_.getInstrumentNote(midiNote);
        Voice& voice = voices_[i];

        // Initialize voice with per-instance envelope wrapper and InstrumentLoader reference
        // InstrumentLoader pointer enables dynamic velocity layer size calculation (1-8 layers)
        voice.initialize(inst, currentSampleRate_, envelope_, logger, &instrumentLoader_);
        voice.prepareToPlay(512);
    }

    // ===== DEFAULT PARAMETER INITIALIZATION (MIDI values 0-127) =====

    // Envelope defaults
    setAllVoicesAttackMIDI(0);              // MIDI 0   = Fast attack
    setAllVoicesReleaseMIDI(4);             // MIDI 4   = Short release
    setAllVoicesSustainLevelMIDI(127);      // MIDI 127 = Full sustain

    // Static pan defaults
    setAllVoicesPanMIDI(64);                // MIDI 64  = Center pan

    // LFO pan defaults (disabled)
    setAllVoicesPanSpeedMIDI(0);            // MIDI 0   = No LFO speed
    setAllVoicesPanDepthMIDI(0);            // MIDI 0   = No LFO depth

    // Stereo field defaults
    setAllVoicesStereoFieldAmountMIDI(0);   // MIDI 0   = Disabled (mono/natural stereo)

    // DSP defaults
    setLimiterThresholdMIDI(127);           // MIDI 127 = 0 dB (transparent/off)
    setLimiterReleaseMIDI(64);              // MIDI 64  = ~50 ms (medium release)
    setLimiterEnabledMIDI(0);               // MIDI 0   = Disabled
    
    logger.log("VoiceManager/initializeVoicesWithInstruments", LogSeverity::Info, 
            "All 128 voices initialized successfully with default parameters");
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

// ===== LFO PANNING HELPERS =====

void VoiceManager::applyLfoPanningPerSample(int samplesPerBlock) noexcept {
    // Generování hodnot LFO panningu pro každý vzorek
    if (lfoPanBuffer_.size() < static_cast<size_t>(samplesPerBlock)) {
        lfoPanBuffer_.resize(samplesPerBlock);
    }

    // Výpočet interpolačního kroku pro plynulé přechody (stejný pro speed i depth)
    const float deltaPerSample = (currentSampleRate_ > 0)
        ? (1.0f / (panSmoothingTime_ * currentSampleRate_))
        : 0.0f;

    for (int sample = 0; sample < samplesPerBlock; ++sample) {
        // Plynulá interpolace speed k cílové hodnotě (stejně jako depth)
        if (panSpeed_ < panSpeedTarget_) {
            panSpeed_ += deltaPerSample;
            if (panSpeed_ > panSpeedTarget_) {
                panSpeed_ = panSpeedTarget_;
            }
        } else if (panSpeed_ > panSpeedTarget_) {
            panSpeed_ -= deltaPerSample;
            if (panSpeed_ < panSpeedTarget_) {
                panSpeed_ = panSpeedTarget_;
            }
        }

        // Plynulá interpolace depth k cílové hodnotě
        if (panDepth_ < panDepthTarget_) {
            panDepth_ += deltaPerSample;
            if (panDepth_ > panDepthTarget_) {
                panDepth_ = panDepthTarget_;
            }
        } else if (panDepth_ > panDepthTarget_) {
            panDepth_ -= deltaPerSample;
            if (panDepth_ < panDepthTarget_) {
                panDepth_ = panDepthTarget_;
            }
        }

        // Výpočet phase increment z interpolovaného speed (per-sample)
        const float phaseIncrement = (currentSampleRate_ > 0)
            ? LfoPanning::calculatePhaseIncrement(panSpeed_, currentSampleRate_)
            : 0.0f;

        // Výpočet LFO hodnoty z předpočítané sinusové tabulky s interpolovaným depth
        float lfoValue = LfoPanning::getSineValue(lfoPhase_);
        lfoPanBuffer_[sample] = lfoValue * panDepth_;

        // Posun fáze LFO o jeden vzorek (s per-sample vypočítaným phase increment)
        lfoPhase_ += phaseIncrement;

        // Ořezání fáze do platného rozsahu (0.0-2π)
        lfoPhase_ = LfoPanning::wrapPhase(lfoPhase_);
    }
}

void VoiceManager::resetLfoParameters() noexcept {
    panSpeed_ = 0.0f;
    panSpeedTarget_ = 0.0f;
    panDepth_ = 0.0f;
    panDepthTarget_ = 0.0f;
    lfoPhase_ = 0.0f;
    previousPanLeft_ = 1.0f;  // Reset předchozích gainů
    previousPanRight_ = 1.0f;
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

// ========================================================================
// DSP EFFECTS API - MIDI Implementation
// ========================================================================

void VoiceManager::setLimiterThresholdMIDI(uint8_t midiValue) noexcept {
    if (limiterEffect_) {
        limiterEffect_->setThresholdMIDI(midiValue);
    }
}

void VoiceManager::setLimiterReleaseMIDI(uint8_t midiValue) noexcept {
    if (limiterEffect_) {
        limiterEffect_->setReleaseMIDI(midiValue);
    }
}

void VoiceManager::setLimiterEnabledMIDI(uint8_t midiValue) noexcept {
    if (limiterEffect_) {
        limiterEffect_->setEnabled(midiValue > 0);
    }
}

uint8_t VoiceManager::getLimiterThresholdMIDI() const noexcept {
    return limiterEffect_ ? limiterEffect_->getThresholdMIDI() : 127;
}

uint8_t VoiceManager::getLimiterReleaseMIDI() const noexcept {
    return limiterEffect_ ? limiterEffect_->getReleaseMIDI() : 64;
}

uint8_t VoiceManager::getLimiterEnabledMIDI() const noexcept {
    return (limiterEffect_ && limiterEffect_->isEnabled()) ? 127 : 0;
}

uint8_t VoiceManager::getLimiterGainReductionMIDI() const noexcept {
    return limiterEffect_ ? limiterEffect_->getGainReductionMIDI() : 127;
}

// ═════════════════════════════════════════════════════════════════════
// BBE MAXIMIZER CONTROL (Always enabled)
// ═════════════════════════════════════════════════════════════════════

void VoiceManager::setBBEDefinitionMIDI(uint8_t midiValue) noexcept {
    if (bbeEffect_) {
        bbeEffect_->setDefinitionMIDI(midiValue);
    }
}

void VoiceManager::setBBEBassBoostMIDI(uint8_t midiValue) noexcept {
    if (bbeEffect_) {
        bbeEffect_->setBassBoostMIDI(midiValue);
    }
}

