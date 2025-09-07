#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include "voice.h"
#include "instrument_loader.h"
#include "sampler.h"
#include <vector>
#include <string>
#include <atomic>
#include <memory>

class VoiceManager {
public:
    VoiceManager(const std::string& sampleDir, int sampleRate, Logger& logger);

    // ✅ ODSTRANĚNO: initializeAll() - bude se dělat manuálně
    // void initializeAll(Logger& logger);

    void setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity) noexcept;
    bool processBlock(float* outputLeft, float* outputRight, int numSamples) noexcept;
    bool processBlock(AudioData* outputBuffer, int numSamples) noexcept;
    void stopAllVoices() noexcept;
    void resetAllVoices(Logger& logger);

    // Gettery
    int getMaxVoices() const noexcept { return 128; }
    int getActiveVoicesCount() const noexcept { return activeVoicesCount_.load(); }
    int getSustainingVoicesCount() const noexcept;
    int getReleasingVoicesCount() const noexcept;

    Voice& getVoice(uint8_t midiNote) noexcept;
    const Voice& getVoice(uint8_t midiNote) const noexcept;

    void setRealTimeMode(bool enabled) noexcept;
    bool isRealTimeMode() const noexcept { return rtMode_.load(); }
    void logStatistics(Logger& logger) const;

private:
    std::vector<Voice> voices_;
    int sampleRate_;
    std::string sampleDir_;

    std::vector<Voice*> activeVoices_;
    std::vector<Voice*> voicesToRemove_;
    
    mutable std::atomic<int> activeVoicesCount_{0};
    std::atomic<bool> rtMode_{false};

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