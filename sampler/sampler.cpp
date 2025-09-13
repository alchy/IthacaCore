#include <iostream>
#include <memory>

#include "sampler.h"
#include "voice_manager.h"
#include "envelopes/envelope_static_data.h"
#include "wav_file_exporter.h"


/**
 * @brief CORE: runSampler - čistá produkční implementace
 * 
 * Inicializuje a ověří základní funkčnost sampler systému.
 * Bez testů, bez demo funkcí - pouze core funkcionalita.
 */
int runSampler(Logger& logger) {
    logger.log("runSampler", "info", "=== CORE SAMPLER SYSTEM STARTING ===");
    
    try {
        // FÁZE 0: KRITICKÁ - Globální inicializace envelope dat
        logger.log("runSampler", "info", "Initializing envelope static data...");
        if (!EnvelopeStaticData::initialize(logger)) {
            logger.log("runSampler", "error", "Failed to initialize envelope static data");
            return 1;
        }

        // FÁZE 1: Vytvoření VoiceManager instance
        logger.log("runSampler", "info", "Creating VoiceManager instance...");
        VoiceManager voiceManager(DEFAULT_SAMPLE_DIR, logger);
        
        // FÁZE 2: Systémová inicializace
        logger.log("runSampler", "info", "Initializing system...");
        voiceManager.initializeSystem(logger);
        
        // FÁZE 3: Načtení pro sample rate
        logger.log("runSampler", "info", "Loading for sample rate " + std::to_string(DEFAULT_SAMPLE_RATE) + " Hz");
        voiceManager.loadForSampleRate(DEFAULT_SAMPLE_RATE, logger);
        
        // FÁZE 4: JUCE příprava
        logger.log("runSampler", "info", "Preparing for audio processing...");
        voiceManager.prepareToPlay(DEFAULT_JUCE_BLOCK_SIZE);
        
        // FÁZE 5: Základní ověření funkčnosti
        logger.log("runSampler", "info", "Verifying basic functionality...");
        if (!verifyBasicFunctionality(voiceManager, logger)) {
            logger.log("runSampler", "error", "Basic functionality verification failed");
            return 1;
        }
        
        // FÁZE 6: Systémové statistiky
        voiceManager.logSystemStatistics(logger);
        
        logger.log("runSampler", "info", "=== CORE SAMPLER SYSTEM READY ===");
        return 0;
        
    } catch (const std::exception& e) {
        logger.log("runSampler", "error", "CRITICAL ERROR: " + std::string(e.what()));
        return 1;
    } catch (...) {
        logger.log("runSampler", "error", "UNKNOWN CRITICAL ERROR");
        return 1;
    }
}

// ... (existující includes v sampler.cpp)
// Přidej tyto, pokud chybí:
#include <vector>
#include <cmath>
#include <algorithm>
#include "wav_file_exporter.h"  // Pro WavExporter

// Helper pro export audio do WAV pomocí WavExporter
void exportTestAudio(const std::string& filename, const float* data, int numFrames, int channels, int sampleRate, Logger& logger) {
    // Inicializace WavExporter s cestou "./exports/tests/" a formátem Float
    WavExporter exporter("./exports/tests/", logger, ExportFormat::Float);
    float* buffer = exporter.wavFileCreate(filename, sampleRate, numFrames, channels == 2, true);  // Reálný zápis
    // Kopírování dat do bufferu (interleaved)
    std::memcpy(buffer, data, static_cast<size_t>(numFrames) * channels * sizeof(float));
    bool success = exporter.wavFileWriteBuffer(buffer, numFrames);
    if (!success) {
        logger.log("exportTestAudio", "error", "Failed to write WAV: " + filename);
    } else {
        logger.log("exportTestAudio", "info", "Exported WAV: " + filename);
    }
    // Destruktor exporteru se postará o close a free
}

// Analýza attack fáze (zvýšení gainů)
bool analyzeAttackPhase(const std::vector<float>& envelopeGains, int attackBlocks, Logger& logger) {
    if (envelopeGains.size() < static_cast<size_t>(attackBlocks) || attackBlocks <= 1) return false;
    
    int increasingCount = 0;
    for (int i = 1; i < attackBlocks; ++i) {
        if (envelopeGains[i] >= envelopeGains[i - 1]) increasingCount++;
    }
    
    float ratio = static_cast<float>(increasingCount) / (attackBlocks - 1);
    bool ok = ratio >= 0.7f;
    logger.log("analyzeAttackPhase", "info", "Attack: " + std::to_string(increasingCount) + "/" + std::to_string(attackBlocks - 1) + " increasing (ratio: " + std::to_string(ratio) + ")");
    return ok;
}

// Analýza sustain fáze (stabilita gainů)
bool analyzeSustainPhase(const std::vector<float>& envelopeGains, int attackBlocks, int sustainBlocks, Logger& logger) {
    int start = attackBlocks;
    int end = attackBlocks + sustainBlocks;
    if (envelopeGains.size() < static_cast<size_t>(end) || sustainBlocks <= 1) return false;
    
    float sustainLevel = envelopeGains[start];
    float maxVariation = 0.0f;
    for (int i = start; i < end; ++i) {
        maxVariation = std::max(maxVariation, std::abs(envelopeGains[i] - sustainLevel));
    }
    
    bool ok = maxVariation <= 0.2f;
    logger.log("analyzeSustainPhase", "info", "Sustain: level=" + std::to_string(sustainLevel) + ", max variation=" + std::to_string(maxVariation));
    return ok;
}

// Analýza release fáze (snížení gainů)
bool analyzeReleasePhase(const std::vector<float>& envelopeGains, int releaseStart, int releaseBlocks, Logger& logger) {
    if (envelopeGains.size() < static_cast<size_t>(releaseStart + releaseBlocks) || releaseBlocks <= 1) return false;
    
    int decreasingCount = 0;
    for (int i = releaseStart + 1; i < releaseStart + releaseBlocks; ++i) {
        if (envelopeGains[i] <= envelopeGains[i - 1]) decreasingCount++;
    }
    
    float ratio = static_cast<float>(decreasingCount) / (releaseBlocks - 1);
    bool ok = ratio >= 0.7f;
    logger.log("analyzeReleasePhase", "info", "Release: " + std::to_string(decreasingCount) + "/" + std::to_string(releaseBlocks - 1) + " decreasing (ratio: " + std::to_string(ratio) + ")");
    return ok;
}

/**
 * @brief Základní ověření funkčnosti - minimální test včetně envelope_test
 *
 * Funkce provede původní jednoduchý test note-on/off a přidá envelope_test
 * s exportem do WAV souborů. Používá dummy buffery pro simulaci audio vstupu.
 * Analyzuje chování envelope fází (attack, sustain, release).
 *
 * @param voiceManager Reference na VoiceManager pro testování
 * @param logger Reference na Logger pro logování
 * @return true pokud všechny testy prošly, jinak false
 */
bool verifyBasicFunctionality(VoiceManager& voiceManager, Logger& logger) {
    try {
        // --- Původní jednoduchý test (basic_test) ---
        // Jednoduchý test - start note, process block, stop note
        uint8_t testMidi = 70;
        uint8_t testVelocity = 100;
        const int blockSize = 512;
        
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        // Clear buffers
        std::fill(leftBuffer, leftBuffer + blockSize, 0.0f);
        std::fill(rightBuffer, rightBuffer + blockSize, 0.0f);
        
        // Start note
        bool hasAudio;
        int i;
        voiceManager.setNoteState(testMidi, true, testVelocity);
        for(i = 0; i < 12; i++) {
            hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
        }
        voiceManager.setNoteState(testMidi, false, testVelocity);
        for(i = 0; i < 512; i++) {
            hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
        }
        
        if (hasAudio) {
            logger.log("verifyBasicFunctionality", "info", "Basic functionality verification passed");
        } else {
            logger.log("verifyBasicFunctionality", "warn", "No audio output detected in basic test");
            // Pokračujeme i při varování, ale označíme jako neúspěch na konci
        }
        
        // --- Nový test: envelope_test ---
        logger.log("verifyBasicFunctionality", "info", "Starting envelope_test");
        
        // Konfigurace envelope testu
        bool exportAudio = true;  // Zapnuto pro export do WAV
        int exportBlockSize = 512;
        uint8_t defaultTestVelocity = 100;
        int sampleRate = voiceManager.getCurrentSampleRate();  // Získání sample rate z VoiceManager
        
        // Délky fází v sekundách
        const double attackDurationSec = 4.0;
        const double sustainDurationSec = 4.0;
        const double releaseDurationSec = 4.0;
        
        // Výpočet počtu bloků pro každou fázi
        const int attackBlocks = static_cast<int>(std::ceil(attackDurationSec * sampleRate / exportBlockSize));
        const int sustainBlocks = static_cast<int>(std::ceil(sustainDurationSec * sampleRate / exportBlockSize));
        const int releaseBlocks = static_cast<int>(std::ceil(releaseDurationSec * sampleRate / exportBlockSize));
        const int totalBlocks = attackBlocks + sustainBlocks + releaseBlocks;
        
        logger.log("verifyBasicFunctionality", "info",
                   "Envelope test phases: Attack " + std::to_string(attackBlocks) + " blocks, "
                   "Sustain " + std::to_string(sustainBlocks) + " blocks, "
                   "Release " + std::to_string(releaseBlocks) + " blocks");
        
        // Získání voice pro testMidi
        Voice& voice = voiceManager.getVoice(testMidi);
        
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
        voiceManager.setNoteState(testMidi, true, defaultTestVelocity);
        
        // Procesování všech bloků
        for (int block = 0; block < totalBlocks; ++block) {
            // Clear output buffery před procesováním
            std::fill(leftBuffer, leftBuffer + blockSize, 0.0f);
            std::fill(rightBuffer, rightBuffer + blockSize, 0.0f);
            
            hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
            
            // Získání aktuálního envelope gainu
            float currentEnvelopeGain = voice.getCurrentEnvelopeGain();
            envelopeGains.push_back(currentEnvelopeGain);
            
            // Logování pro každý blok (pro debugging)
            logger.log("verifyBasicFunctionality", "info",
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
            }
            
            // Note-off na konci sustain fáze
            if (block == attackBlocks + sustainBlocks - 1) {
                voiceManager.setNoteState(testMidi, false, defaultTestVelocity);
                logger.log("verifyBasicFunctionality", "info", "Note-off sent - starting release phase");
            }
        }
        
        // Export WAV souborů, pokud je zapnuto
        if (exportAudio) {
            exportTestAudio("envelope_attack.wav", attackExportBuffer.data(), static_cast<int>(attackExportBuffer.size() / 2), 2, sampleRate, logger);
            exportTestAudio("envelope_sustain.wav", sustainExportBuffer.data(), static_cast<int>(sustainExportBuffer.size() / 2), 2, sampleRate, logger);
            exportTestAudio("envelope_release.wav", releaseExportBuffer.data(), static_cast<int>(releaseExportBuffer.size() / 2), 2, sampleRate, logger);
            exportTestAudio("envelope_full_cycle.wav", fullCycleExportBuffer.data(), static_cast<int>(fullCycleExportBuffer.size() / 2), 2, sampleRate, logger);
            logger.log("verifyBasicFunctionality", "info", "Exported all envelope phase audio files");
        }
        
        // Analýza fází envelope
        bool attackPhaseOk = analyzeAttackPhase(envelopeGains, attackBlocks, logger);
        bool sustainPhaseOk = analyzeSustainPhase(envelopeGains, attackBlocks, sustainBlocks, logger);
        bool releasePhaseOk = analyzeReleasePhase(envelopeGains, attackBlocks + sustainBlocks, releaseBlocks, logger);
        
        bool envelopeTestPassed = attackPhaseOk && sustainPhaseOk && releasePhaseOk;
        
        if (envelopeTestPassed) {
            logger.log("verifyBasicFunctionality", "info", "Envelope test passed");
        } else {
            logger.log("verifyBasicFunctionality", "warn", "Envelope test failed in one or more phases");
        }
        
        // Cleanup
        delete[] leftBuffer;
        delete[] rightBuffer;
        
        // Celkový úspěch: původní test AND envelope test
        return hasAudio && envelopeTestPassed;
        
    } catch (const std::exception& e) {
        logger.log("verifyBasicFunctionality", "error", "Verification failed: " + std::string(e.what()));
        return false;
    } catch (...) {
        logger.log("verifyBasicFunctionality", "error", "Verification failed: unknown error");
        return false;
    }
}