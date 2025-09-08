#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include "voice.h"
#include "instrument_loader.h"  // Pro stack allocated member
#include "sampler.h"           // Pro SamplerIO stack allocated member
#include <vector>
#include <string>
#include <atomic>
#include <memory>

class VoiceManager {
public:
    VoiceManager(const std::string& sampleDir, Logger& logger);

    // Dynamic sample rate management
    void changeSampleRate(int newSampleRate, Logger& logger);
    int getCurrentSampleRate() const noexcept { return currentSampleRate_; }

    // NOVÉ: JUCE integration - prepare all voices for buffer size changes
    void prepareToPlay(int maxBlockSize) noexcept;

    // Initialization pipeline (nahradí runSampler logiku)
    void initializeSystem(Logger& logger);
    void loadAllInstruments(Logger& logger);
    void validateSystemIntegrity(Logger& logger);

    // NOVÉ: Granular testing methods with GAIN FOCUS
    void runSingleNoteTest(Logger& logger);
    void runPolyphonyTest(Logger& logger);
    void runEdgeCaseTests(Logger& logger);
    void runIndividualVoiceTest(Logger& logger);

    // NOVÉ: VELOCITY GAIN SPECIFIC testing methods
    /**
     * @brief NOVÁ metoda: Test velocity gain s různými hodnotami
     * Testuje aplikaci MIDI velocity na hlasitost s detailním logováním.
     * Testuje velocity: 1, 32, 64, 96, 127 a analyzuje výstupní hlasitost.
     * @param logger Reference na Logger
     */
    void runVelocityGainTest(Logger& logger);

    /**
     * @brief NOVÁ metoda: Test master gain nastavení
     * Testuje různé master gain hodnoty: 0.1, 0.3, 0.5, 0.8, 1.0
     * s analýzou jejich dopadu na výstupní hlasitost.
     * @param logger Reference na Logger
     */
    void runMasterGainTest(Logger& logger);

    /**
     * @brief ROZŠÍŘENÝ runSingleNoteTest s gain analýzou
     * Kombinuje původní single note test s detailním monitoringem
     * envelope, velocity a master gain během přehrávání.
     * @param logger Reference na Logger
     */
    void runSingleNoteTestWithGain(Logger& logger);

    // Export and diagnostics
    void exportTestSample(uint8_t midi, uint8_t vel, 
                         const std::string& exportDir, Logger& logger);
    void exportSingleNotePlayback(uint8_t midi, uint8_t velocity, 
                                 const std::string& exportDir, Logger& logger);
    void exportPolyphonyPlayback(const std::vector<uint8_t>& midiNotes, 
                                const std::string& exportDir, Logger& logger);
    void logSystemStatistics(Logger& logger);

    // UPRAVENÉ API - processBlock nyní předává active voice count
    void setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity) noexcept;
    bool processBlock(float* outputLeft, float* outputRight, int numSamples) noexcept;
    bool processBlock(AudioData* outputBuffer, int numSamples) noexcept;
    void stopAllVoices() noexcept;
    void resetAllVoices(Logger& logger);

    // Existing getters
    int getMaxVoices() const noexcept { return 128; }
    int getActiveVoicesCount() const noexcept { return activeVoicesCount_.load(); }
    int getSustainingVoicesCount() const noexcept;
    int getReleasingVoicesCount() const noexcept;

    Voice& getVoice(uint8_t midiNote) noexcept;
    const Voice& getVoice(uint8_t midiNote) const noexcept;

    void setRealTimeMode(bool enabled) noexcept;
    bool isRealTimeMode() const noexcept { return rtMode_.load(); }

private:
    // Stack allocated encapsulated components
    SamplerIO samplerIO_;              // Stack allocated SamplerIO instance
    InstrumentLoader instrumentLoader_; // Stack allocated InstrumentLoader instance

    // Sample rate management
    int currentSampleRate_;
    std::string sampleDir_;
    bool systemInitialized_;           // Flag pro initialization state

    // Voice management
    std::vector<Voice> voices_;
    std::vector<Voice*> activeVoices_;
    std::vector<Voice*> voicesToRemove_;
    
    mutable std::atomic<int> activeVoicesCount_{0};
    std::atomic<bool> rtMode_{false};

    // Helper methods pro dynamic sample rate
    void reinitializeIfNeeded(int targetSampleRate, Logger& logger);
    bool needsReinitialization(int targetSampleRate) const noexcept;
    void initializeVoicesWithInstruments(Logger& logger);

    // Test helper methods
    uint8_t findValidTestMidiNote(Logger& logger) const;
    std::vector<uint8_t> findValidNotesForPolyphony(Logger& logger, int maxNotes = 3) const;

    // Voice management methods
    void addActiveVoice(Voice* voice) noexcept;
    void removeActiveVoice(Voice* voice) noexcept;
    void cleanupInactiveVoices() noexcept;
    void logSafe(const std::string& component, const std::string& severity, 
                const std::string& message, Logger& logger) const;
    bool isValidMidiNote(uint8_t midiNote) const noexcept {
        return midiNote <= 127;
    }
};

#endif // VOICE_MANAGER_H