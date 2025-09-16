#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>
#include <cmath>

#include "test_mixer.h"
#include "test_helpers.h"
#include "../voice_manager.h"
#include "../voice.h"
#include "../mixer.h"

bool runMixerEnergyTest(VoiceManager& voiceManager, Logger& logger) {
    try {
        logger.log("runMixerEnergyTest", "info", "Starting energy-based mixer comparison test");
        
        // Testovací parametry
        const int blockSize = 512;
        const int sampleRate = voiceManager.getCurrentSampleRate();
        const uint8_t testVelocity = 100;
        
        // Test scénáře: 
        // 1. Jedna silná nota (MIDI 60)
        // 2. Tři doznívající noty (MIDI 62, 64, 66) + jedna silná nota (MIDI 60)
        
        // Alokace bufferů pro oba systémy
        float* oldLeft = new float[blockSize];
        float* oldRight = new float[blockSize];
        float* newLeft = new float[blockSize];
        float* newRight = new float[blockSize];
        
        // Test 1: Jedna silná nota
        logger.log("runMixerEnergyTest", "info", "=== TEST 1: Single strong note ===");
        
        // Reset systému
        voiceManager.resetAllVoices(logger);
        voiceManager.setAllVoicesMasterGainMIDI(127, logger);
        
        // Spustit notu
        voiceManager.setNoteStateMIDI(60, true, testVelocity);
        
        // Nechat attack fázi proběhnout
        for (int i = 0; i < 20; ++i) {
            voiceManager.processBlockUninterleaved(oldLeft, oldRight, blockSize);
        }
        
        // Zachytit výstup starého systému
        std::fill(oldLeft, oldLeft + blockSize, 0.0f);
        std::fill(oldRight, oldRight + blockSize, 0.0f);
        voiceManager.processBlockUninterleaved(oldLeft, oldRight, blockSize);
        
        // Reset a stejný test s novým mixerem
        voiceManager.resetAllVoices(logger);
        voiceManager.setAllVoicesMasterGainMIDI(127, logger);
        voiceManager.setNoteStateMIDI(60, true, testVelocity);
        
        for (int i = 0; i < 20; ++i) {
            voiceManager.processBlockEnergyBased(newLeft, newRight, blockSize);
        }
        
        std::fill(newLeft, newLeft + blockSize, 0.0f);
        std::fill(newRight, newRight + blockSize, 0.0f);
        voiceManager.processBlockEnergyBased(newLeft, newRight, blockSize);
        
        // Analýza výsledků jedné noty
        float oldPeak = calculatePeakLevel(oldLeft, blockSize);
        float newPeak = calculatePeakLevel(newLeft, blockSize);
        
        logger.log("runMixerEnergyTest", "info", 
                   "Single note - Old peak: " + std::to_string(oldPeak) + 
                   ", New peak: " + std::to_string(newPeak));
        
        // Test 2: Více not - simulace klavírního pedálu
        logger.log("runMixerEnergyTest", "info", "=== TEST 2: Multiple notes (piano pedal simulation) ===");
        
        // Reset systému
        voiceManager.resetAllVoices(logger);
        voiceManager.setAllVoicesMasterGainMIDI(127, logger);
        
        // STARÝ SYSTÉM: Spustit 3 noty, nechat doznít, pak přidat silnou notu
        
        // Spustit 3 doznívající noty
        voiceManager.setNoteStateMIDI(62, true, 60);  // Slabší velocity
        voiceManager.setNoteStateMIDI(64, true, 50);
        voiceManager.setNoteStateMIDI(66, true, 40);
        
        // Nechat je přejít do sustain
        for (int i = 0; i < 30; ++i) {
            voiceManager.processBlockUninterleaved(oldLeft, oldRight, blockSize);
        }
        
        // Vypnout je (release fáze)
        voiceManager.setNoteStateMIDI(62, false);
        voiceManager.setNoteStateMIDI(64, false);
        voiceManager.setNoteStateMIDI(66, false);
        
        // Nechat je jít do release fáze
        for (int i = 0; i < 20; ++i) {
            voiceManager.processBlockUninterleaved(oldLeft, oldRight, blockSize);
        }
        
        // Přidat silnou notu do mixu s doznívajícími
        voiceManager.setNoteStateMIDI(60, true, testVelocity);
        
        // Nechat attack proběhnout a zachytit výstup
        for (int i = 0; i < 20; ++i) {
            voiceManager.processBlockUninterleaved(oldLeft, oldRight, blockSize);
        }
        
        std::fill(oldLeft, oldLeft + blockSize, 0.0f);
        std::fill(oldRight, oldRight + blockSize, 0.0f);
        voiceManager.processBlockUninterleaved(oldLeft, oldRight, blockSize);
        
        // NOVÝ SYSTÉM: Stejný scénář
        voiceManager.resetAllVoices(logger);
        voiceManager.setAllVoicesMasterGainMIDI(127, logger);
        
        voiceManager.setNoteStateMIDI(62, true, 60);
        voiceManager.setNoteStateMIDI(64, true, 50);
        voiceManager.setNoteStateMIDI(66, true, 40);
        
        for (int i = 0; i < 30; ++i) {
            voiceManager.processBlockEnergyBased(newLeft, newRight, blockSize);
        }
        
        voiceManager.setNoteStateMIDI(62, false);
        voiceManager.setNoteStateMIDI(64, false);
        voiceManager.setNoteStateMIDI(66, false);
        
        for (int i = 0; i < 20; ++i) {
            voiceManager.processBlockEnergyBased(newLeft, newRight, blockSize);
        }
        
        voiceManager.setNoteStateMIDI(60, true, testVelocity);
        
        for (int i = 0; i < 20; ++i) {
            voiceManager.processBlockEnergyBased(newLeft, newRight, blockSize);
        }
        
        std::fill(newLeft, newLeft + blockSize, 0.0f);
        std::fill(newRight, newRight + blockSize, 0.0f);
        voiceManager.processBlockEnergyBased(newLeft, newRight, blockSize);
        
        // Analýza výsledků s více notami
        float oldMultiPeak = calculatePeakLevel(oldLeft, blockSize);
        float newMultiPeak = calculatePeakLevel(newLeft, blockSize);
        
        logger.log("runMixerEnergyTest", "info", 
                   "Multiple notes - Old peak: " + std::to_string(oldMultiPeak) + 
                   ", New peak: " + std::to_string(newMultiPeak));
        
        // Výpočet poměrů
        float oldRatio = (oldPeak > 0.0f) ? (oldMultiPeak / oldPeak) : 0.0f;
        float newRatio = (newPeak > 0.0f) ? (newMultiPeak / newPeak) : 0.0f;
        
        logger.log("runMixerEnergyTest", "info", 
                   "Volume ratios - Old system: " + std::to_string(oldRatio) + 
                   ", New system: " + std::to_string(newRatio));
        
        // Test 3: Export srovnávacího audio
        bool exportSuccess = runMixerComparisonExport(voiceManager, logger);
        
        // Cleanup
        delete[] oldLeft;
        delete[] oldRight;
        delete[] newLeft;
        delete[] newRight;
        
        // Vyhodnocení: Nový systém by měl zachovat více hlasitosti silné noty
        bool testPassed = (newRatio > oldRatio * 1.1f) && exportSuccess;
        
        if (testPassed) {
            logger.log("runMixerEnergyTest", "info", 
                      "Energy-based mixer test PASSED - new system preserves strong note energy better");
        } else {
            logger.log("runMixerEnergyTest", "warn", 
                      "Energy-based mixer test FAILED - improvement not detected");
        }
        
        return testPassed;
        
    } catch (const std::exception& e) {
        logger.log("runMixerEnergyTest", "error", "Energy mixer test failed: " + std::string(e.what()));
        return false;
    } catch (...) {
        logger.log("runMixerEnergyTest", "error", "Energy mixer test failed: unknown error");
        return false;
    }
}

bool runMixerComparisonExport(VoiceManager& voiceManager, Logger& logger) {
    try {
        logger.log("runMixerComparisonExport", "info", "Starting mixer comparison audio export");
        
        const int blockSize = 512;
        const int sampleRate = voiceManager.getCurrentSampleRate();
        const int exportBlocks = 200; // ~5 sekund při 44.1kHz
        
        // Buffers
        float* oldLeft = new float[blockSize];
        float* oldRight = new float[blockSize];
        float* newLeft = new float[blockSize];
        float* newRight = new float[blockSize];
        
        // Export buffers
        std::vector<float> oldSystemExport;
        std::vector<float> newSystemExport;
        oldSystemExport.reserve(exportBlocks * blockSize * 2);
        newSystemExport.reserve(exportBlocks * blockSize * 2);
        
        // NAHRÁVÁNÍ STARÉHO SYSTÉMU
        logger.log("runMixerComparisonExport", "info", "Recording old mixing system...");
        
        voiceManager.resetAllVoices(logger);
        voiceManager.setAllVoicesMasterGainMIDI(127, logger);
        
        // Komplex scénář: postupné přidávání not
        for (int block = 0; block < exportBlocks; ++block) {
            
            // Přidávání not podle časování
            if (block == 20) voiceManager.setNoteStateMIDI(60, true, 100);  // Silná nota
            if (block == 40) voiceManager.setNoteStateMIDI(62, true, 70);   
            if (block == 60) voiceManager.setNoteStateMIDI(64, true, 60);   
            if (block == 80) voiceManager.setNoteStateMIDI(66, true, 50);   
            if (block == 100) voiceManager.setNoteStateMIDI(67, true, 40);  
            
            // Vypínání some not
            if (block == 120) voiceManager.setNoteStateMIDI(62, false);
            if (block == 140) voiceManager.setNoteStateMIDI(64, false);
            
            // Nová silná nota v mixu s doznívajícími
            if (block == 160) voiceManager.setNoteStateMIDI(72, true, 120);
            
            std::fill(oldLeft, oldLeft + blockSize, 0.0f);
            std::fill(oldRight, oldRight + blockSize, 0.0f);
            
            voiceManager.processBlockUninterleaved(oldLeft, oldRight, blockSize);
            
            // Přidat do export bufferu (interleaved)
            for (int i = 0; i < blockSize; ++i) {
                oldSystemExport.push_back(oldLeft[i]);
                oldSystemExport.push_back(oldRight[i]);
            }
        }
        
        // NAHRÁVÁNÍ NOVÉHO SYSTÉMU
        logger.log("runMixerComparisonExport", "info", "Recording new energy-based mixing system...");
        
        voiceManager.resetAllVoices(logger);
        voiceManager.setAllVoicesMasterGainMIDI(127, logger);
        
        // Stejný scénář s novým mixerem
        for (int block = 0; block < exportBlocks; ++block) {
            
            if (block == 20) voiceManager.setNoteStateMIDI(60, true, 100);
            if (block == 40) voiceManager.setNoteStateMIDI(62, true, 70);
            if (block == 60) voiceManager.setNoteStateMIDI(64, true, 60);
            if (block == 80) voiceManager.setNoteStateMIDI(66, true, 50);
            if (block == 100) voiceManager.setNoteStateMIDI(67, true, 40);
            
            if (block == 120) voiceManager.setNoteStateMIDI(62, false);
            if (block == 140) voiceManager.setNoteStateMIDI(64, false);
            
            if (block == 160) voiceManager.setNoteStateMIDI(72, true, 120);
            
            std::fill(newLeft, newLeft + blockSize, 0.0f);
            std::fill(newRight, newRight + blockSize, 0.0f);
            
            voiceManager.processBlockEnergyBased(newLeft, newRight, blockSize);
            
            for (int i = 0; i < blockSize; ++i) {
                newSystemExport.push_back(newLeft[i]);
                newSystemExport.push_back(newRight[i]);
            }
        }
        
        // Export WAV souborů
        int totalFrames = static_cast<int>(oldSystemExport.size() / 2);
        
        exportTestAudio("mixer_comparison_old_system.wav", 
                       oldSystemExport.data(), totalFrames, 2, sampleRate, logger);
        exportTestAudio("mixer_comparison_new_energy_system.wav", 
                       newSystemExport.data(), totalFrames, 2, sampleRate, logger);
        
        // Cleanup
        delete[] oldLeft;
        delete[] oldRight;
        delete[] newLeft;
        delete[] newRight;
        
        logger.log("runMixerComparisonExport", "info", 
                  "Successfully exported mixer comparison audio files");
        
        return true;
        
    } catch (const std::exception& e) {
        logger.log("runMixerComparisonExport", "error", 
                  "Mixer comparison export failed: " + std::string(e.what()));
        return false;
    } catch (...) {
        logger.log("runMixerComparisonExport", "error", 
                  "Mixer comparison export failed: unknown error");
        return false;
    }
}

bool runMixerStressTest(VoiceManager& voiceManager, Logger& logger) {
    try {
        logger.log("runMixerStressTest", "info", "Starting mixer stress test (many simultaneous voices)");
        
        const int blockSize = 512;
        const int testBlocks = 100;
        const int maxVoices = 32; // Stress test s mnoha voices
        
        float* outputLeft = new float[blockSize];
        float* outputRight = new float[blockSize];
        
        voiceManager.resetAllVoices(logger);
        voiceManager.setAllVoicesMasterGainMIDI(127, logger);
        
        // Postupně přidávat voices až do maxVoices
        for (int voices = 1; voices <= maxVoices; voices += 4) {
            
            // Přidat 4 nové voices
            for (int v = 0; v < 4 && (voices + v - 1) < maxVoices; ++v) {
                uint8_t midiNote = static_cast<uint8_t>(60 + ((voices + v - 1) % 16));
                uint8_t velocity = static_cast<uint8_t>(80 + (v * 10));
                voiceManager.setNoteStateMIDI(midiNote, true, velocity);
            }
            
            // Test výkonu s aktuálním počtem voices
            auto startTime = std::chrono::high_resolution_clock::now();
            
            for (int block = 0; block < testBlocks; ++block) {
                std::fill(outputLeft, outputLeft + blockSize, 0.0f);
                std::fill(outputRight, outputRight + blockSize, 0.0f);
                
                bool hasOutput = voiceManager.processBlockEnergyBased(outputLeft, outputRight, blockSize);
                
                if (!hasOutput && block > 10) {
                    logger.log("runMixerStressTest", "warn", 
                              "No output detected at " + std::to_string(voices) + " voices, block " + std::to_string(block));
                }
            }
            
            auto endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
            
            float peakLevel = calculatePeakLevel(outputLeft, blockSize);
            int activeCount = voiceManager.getActiveVoicesCount();
            
            logger.log("runMixerStressTest", "info", 
                      "Voices: " + std::to_string(voices) + 
                      ", Active: " + std::to_string(activeCount) +
                      ", Peak: " + std::to_string(peakLevel) +
                      ", Time: " + std::to_string(duration.count()) + "μs");
        }
        
        // Postupně vypínat voices
        for (int note = 60; note < 60 + maxVoices; ++note) {
            voiceManager.setNoteStateMIDI(static_cast<uint8_t>(note), false);
        }
        
        // Nechat release fázi proběhnout
        for (int block = 0; block < 200; ++block) {
            voiceManager.processBlockEnergyBased(outputLeft, outputRight, blockSize);
        }
        
        delete[] outputLeft;
        delete[] outputRight;
        
        logger.log("runMixerStressTest", "info", "Mixer stress test completed successfully");
        return true;
        
    } catch (const std::exception& e) {
        logger.log("runMixerStressTest", "error", "Mixer stress test failed: " + std::string(e.what()));
        return false;
    } catch (...) {
        logger.log("runMixerStressTest", "error", "Mixer stress test failed: unknown error");
        return false;
    }
}

float calculatePeakLevel(const float* buffer, int numSamples) {
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

float calculateRMSLevel(const float* buffer, int numSamples) {
    if (numSamples <= 0) return 0.0f;
    
    float sum = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        sum += buffer[i] * buffer[i];
    }
    
    return std::sqrt(sum / numSamples);
}
