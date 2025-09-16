#include <iostream>
#include <memory>
#include <vector>
#include <algorithm>

#include "tests.h"
#include "test_helpers.h"
#include "../voice_manager.h"
#include "../voice.h"


bool verifyBasicFunctionality(VoiceManager& voiceManager, Logger& logger) {
    try {
        logger.log("verifyBasicFunctionality", "info", "Starting comprehensive functionality verification");
        
        // Spuštění jednoduchého testu
        bool simpleTestPassed = runSimpleNoteTest(voiceManager, logger);
        
        // Spuštění envelope testu
        bool envelopeTestPassed = runEnvelopeTest(voiceManager, logger);
        
        // Celkový úspěch: oba testy musí projít
        bool overallSuccess = simpleTestPassed && envelopeTestPassed;
        
        if (overallSuccess) {
            logger.log("verifyBasicFunctionality", "info", "All functionality tests passed successfully");
        } else {
            logger.log("verifyBasicFunctionality", "warn", "One or more functionality tests failed");
        }
        
        return overallSuccess;
        
    } catch (const std::exception& e) {
        logger.log("verifyBasicFunctionality", "error", "Verification failed: " + std::string(e.what()));
        return false;
    } catch (...) {
        logger.log("verifyBasicFunctionality", "error", "Verification failed: unknown error");
        return false;
    }
}

bool runSimpleNoteTest(VoiceManager& voiceManager, Logger& logger) {
    try {
        logger.log("runSimpleNoteTest", "info", "Starting simple note-on/off test");
        
        // Testovací parametry
        uint8_t testMidi = 70;
        uint8_t testVelocity = 100;
        const int blockSize = 512;
        
        // Alokace bufferů
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        // Clear buffers
        std::fill(leftBuffer, leftBuffer + blockSize, 0.0f);
        std::fill(rightBuffer, rightBuffer + blockSize, 0.0f);
        
        // Set panning to right for all notes
        voiceManager.setAllVoicesPanMIDI(32);

        // Nastav plnou hlasitost vsem notam
        voiceManager.setAllVoicesMasterGainMIDI(100, logger);

        // Start note
        voiceManager.setNoteStateMIDI(testMidi, true, testVelocity);
        
        // Procesování několika bloků s note-on
        bool hasAudio = false;
        for (int i = 0; i < 6; i++) {
            hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
        }

        // Procesování několika bloků
        for (int i = 0; i < 6; i++) {
            hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
        }

        // Note OFF
        voiceManager.setNoteStateMIDI(testMidi, false);
        
        // Procesování bloků s note-off (release fáze)
        for (int i = 0; i < 512; i++) {
            hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
        }
        
        // Cleanup
        delete[] leftBuffer;
        delete[] rightBuffer;
        
        if (hasAudio) {
            logger.log("runSimpleNoteTest", "info", "Simple note test passed - audio output detected");
            return true;
        } else {
            logger.log("runSimpleNoteTest", "warn", "Simple note test failed - no audio output detected");
            return false;
        }
        
    } catch (const std::exception& e) {
        logger.log("runSimpleNoteTest", "error", "Simple note test failed: " + std::string(e.what()));
        return false;
    } catch (...) {
        logger.log("runSimpleNoteTest", "error", "Simple note test failed: unknown error");
        return false;
    }
}

bool runEnvelopeTest(VoiceManager& voiceManager, Logger& logger) {
    try {
        logger.log("runEnvelopeTest", "info", "Starting envelope test with WAV export and phase analysis");
        
        // Konfigurace envelope testu
        bool exportAudio = true;  // Zapnuto pro export do WAV
        int exportBlockSize = 512;
        uint8_t defaultTestVelocity = 100;
        uint8_t testMidi = 70;
        int sampleRate = voiceManager.getCurrentSampleRate();
        
        // Délky fází v sekundách
        const double attackDurationSec = 4.0;
        const double sustainDurationSec = 4.0;
        const double releaseDurationSec = 4.0;
        
        // Výpočet počtu bloků pro každou fázi pomocí helperu
        const int attackBlocks = calculateBlocksForDuration(attackDurationSec, sampleRate, exportBlockSize);
        const int sustainBlocks = calculateBlocksForDuration(sustainDurationSec, sampleRate, exportBlockSize);
        const int releaseBlocks = calculateBlocksForDuration(releaseDurationSec, sampleRate, exportBlockSize);
        const int totalBlocks = attackBlocks + sustainBlocks + releaseBlocks;
        
        logger.log("runEnvelopeTest", "info",
                   "Envelope test phases: Attack " + std::to_string(attackBlocks) + " blocks, "
                   "Sustain " + std::to_string(sustainBlocks) + " blocks, "
                   "Release " + std::to_string(releaseBlocks) + " blocks");
        
        // Získání voice pro testMidi
        Voice& voice = voiceManager.getVoiceMIDI(testMidi);
        
        // Alokace bufferů
        const int blockSize = exportBlockSize;
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        // Dummy buffery s konstantní 1.0f pro test gainů
        std::fill(leftBuffer, leftBuffer + blockSize, 1.0f);
        std::fill(rightBuffer, rightBuffer + blockSize, 1.0f);
        
        // Kumulativní vektory pro export (stereo interleaved [L,R,L,R...])
        std::vector<float> attackExportBuffer;
        std::vector<float> sustainExportBuffer;
        std::vector<float> releaseExportBuffer;
        std::vector<float> fullCycleExportBuffer;
        
        if (exportAudio) {
            attackExportBuffer.reserve(attackBlocks * blockSize * 2);
            sustainExportBuffer.reserve(sustainBlocks * blockSize * 2);
            releaseExportBuffer.reserve(releaseBlocks * blockSize * 2);
            fullCycleExportBuffer.reserve(totalBlocks * blockSize * 2);
        }
        
        // Vektor pro sběr envelope gainů pro analýzu
        std::vector<float> envelopeGains;
        envelopeGains.reserve(totalBlocks);
        
        // Start note-on pro attack fázi
        voiceManager.setNoteStateMIDI(testMidi, true, defaultTestVelocity);
        
        // Procesování všech bloků
        for (int block = 0; block < totalBlocks; ++block) {
            // DŮLEŽITÉ: Nemazat buffery před procesováním!
            // VoiceManager potřebuje vstupní data pro mix s envelope
            // Clear output buffery jen pokud VoiceManager to nevyžaduje
            // std::fill(leftBuffer, leftBuffer + blockSize, 0.0f);
            // std::fill(rightBuffer, rightBuffer + blockSize, 0.0f);
            
            bool hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
            
            // Získání aktuálního envelope gainu
            float currentEnvelopeGain = voice.getCurrentEnvelopeGain();
            envelopeGains.push_back(currentEnvelopeGain);
            
            // Logování pro každý blok (pro debugging)
            logger.log("runEnvelopeTest", "info",
                       "Block " + std::to_string(block) +
                       " - Envelope gain: " + std::to_string(currentEnvelopeGain) +
                       ", State: " + std::to_string(static_cast<int>(voice.getState())));
            
            // Přidání do export vektorů, pokud je export zapnut
            if (exportAudio) {
                for (int j = 0; j < blockSize; ++j) {
                    if (block < attackBlocks) {
                        attackExportBuffer.push_back(leftBuffer[j]);
                        attackExportBuffer.push_back(rightBuffer[j]);
                    } else if (block < attackBlocks + sustainBlocks) {
                        sustainExportBuffer.push_back(leftBuffer[j]);
                        sustainExportBuffer.push_back(rightBuffer[j]);
                    } else {
                        releaseExportBuffer.push_back(leftBuffer[j]);
                        releaseExportBuffer.push_back(rightBuffer[j]);
                    }
                    fullCycleExportBuffer.push_back(leftBuffer[j]);
                    fullCycleExportBuffer.push_back(rightBuffer[j]);
                }
                
                // Debug info pro prvních 5 bloků
                if (block < 5) {
                    float maxSample = 0.0f;
                    for (int j = 0; j < blockSize; ++j) {
                        maxSample = std::max(maxSample, std::max(std::abs(leftBuffer[j]), std::abs(rightBuffer[j])));
                    }
                    logger.log("runEnvelopeTest", "info", "Block " + std::to_string(block) + " max sample: " + std::to_string(maxSample));
                }
            }
            
            // Note-off na konci sustain fáze
            if (block == attackBlocks + sustainBlocks - 1) {
                voiceManager.setNoteStateMIDI(testMidi, false, defaultTestVelocity);
                logger.log("runEnvelopeTest", "info", "Note-off sent - starting release phase");
            }
        }
        
        // Export WAV souborů, pokud je zapnuto
        if (exportAudio) {
            exportTestAudio("envelope_attack.wav", attackExportBuffer.data(), static_cast<int>(attackExportBuffer.size() / 2), 2, sampleRate, logger);
            exportTestAudio("envelope_sustain.wav", sustainExportBuffer.data(), static_cast<int>(sustainExportBuffer.size() / 2), 2, sampleRate, logger);
            exportTestAudio("envelope_release.wav", releaseExportBuffer.data(), static_cast<int>(releaseExportBuffer.size() / 2), 2, sampleRate, logger);
            exportTestAudio("envelope_full_cycle.wav", fullCycleExportBuffer.data(), static_cast<int>(fullCycleExportBuffer.size() / 2), 2, sampleRate, logger);
            logger.log("runEnvelopeTest", "info", "Exported all envelope phase audio files");
        }
        
        // Analýza fází envelope pomocí helperů
        bool attackPhaseOk = analyzeAttackPhase(envelopeGains, attackBlocks, logger);
        bool sustainPhaseOk = analyzeSustainPhase(envelopeGains, attackBlocks, sustainBlocks, logger);
        bool releasePhaseOk = analyzeReleasePhase(envelopeGains, attackBlocks + sustainBlocks, releaseBlocks, logger);
        
        bool envelopeTestPassed = attackPhaseOk && sustainPhaseOk && releasePhaseOk;
        
        // Cleanup
        delete[] leftBuffer;
        delete[] rightBuffer;
        
        if (envelopeTestPassed) {
            logger.log("runEnvelopeTest", "info", "Envelope test passed - all phases analyzed successfully");
            return true;
        } else {
            logger.log("runEnvelopeTest", "warn", "Envelope test failed in one or more phases");
            return false;
        }
        
    } catch (const std::exception& e) {
        logger.log("runEnvelopeTest", "error", "Envelope test failed: " + std::string(e.what()));
        return false;
    } catch (...) {
        logger.log("runEnvelopeTest", "error", "Envelope test failed: unknown error");
        return false;
    }
}