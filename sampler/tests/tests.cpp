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
        logger.log("verifyBasicFunctionality", LogSeverity::Info, "Starting basic functionality verification");
        
        // Spuštění jednoduchého testu s exportem
        bool simpleTestPassed = runSimpleNoteTest(voiceManager, logger);
        
        if (simpleTestPassed) {
            logger.log("verifyBasicFunctionality", LogSeverity::Info, "Basic functionality test passed successfully");
        } else {
            logger.log("verifyBasicFunctionality", LogSeverity::Warning, "Basic functionality test failed");
        }
        
        return simpleTestPassed;
        
    } catch (const std::exception& e) {
        logger.log("verifyBasicFunctionality", LogSeverity::Error, "Verification failed: " + std::string(e.what()));
        return false;
    } catch (...) {
        logger.log("verifyBasicFunctionality", LogSeverity::Error, "Verification failed: unknown error");
        return false;
    }
}

bool runSimpleNoteTest(VoiceManager& voiceManager, Logger& logger) {
    try {
        logger.log("runSimpleNoteTest", LogSeverity::Info, "Starting simple note-on/off test with WAV export");
        
        // Testovací parametry
        uint8_t testMidi = 70;
        uint8_t testVelocity = 100;
        const int blockSize = 512;
        int sampleRate = voiceManager.getCurrentSampleRate();
        
        // Konfigurace exportu
        bool exportAudio = true;
        
        // Délky fází v sekundách
        const double noteOnDurationSec = 2.0;    // 2 sekundy note-on
        const double releaseDurationSec = 3.0;   // 3 sekundy release
        
        // Výpočet počtu bloků pro každou fázi
        const int noteOnBlocks = calculateBlocksForDuration(noteOnDurationSec, sampleRate, blockSize);
        const int releaseBlocks = calculateBlocksForDuration(releaseDurationSec, sampleRate, blockSize);
        const int totalBlocks = noteOnBlocks + releaseBlocks;
        
        logger.log("runSimpleNoteTest", LogSeverity::Info,
                   "Test phases: Note-on " + std::to_string(noteOnBlocks) + " blocks, "
                   "Release " + std::to_string(releaseBlocks) + " blocks");
        
        // Alokace bufferů
        float* leftBuffer = new float[blockSize];
        float* rightBuffer = new float[blockSize];
        
        // Vektory pro export (stereo interleaved [L,R,L,R...])
        std::vector<float> noteOnExportBuffer;
        std::vector<float> releaseExportBuffer;
        std::vector<float> fullTestExportBuffer;
        
        if (exportAudio) {
            noteOnExportBuffer.reserve(noteOnBlocks * blockSize * 2);
            releaseExportBuffer.reserve(releaseBlocks * blockSize * 2);
            fullTestExportBuffer.reserve(totalBlocks * blockSize * 2);
        }
        
        // Clear buffers
        std::fill(leftBuffer, leftBuffer + blockSize, 0.0f);
        std::fill(rightBuffer, rightBuffer + blockSize, 0.0f);
        
        // Nastavení audio parametrů
        voiceManager.setAllVoicesPanMIDI(32);  // Panning doprava
        voiceManager.setAllVoicesMasterGainMIDI(100, logger);  // Plná hlasitost
        
        // Start note
        voiceManager.setNoteStateMIDI(testMidi, true, testVelocity);
        logger.log("runSimpleNoteTest", LogSeverity::Info, "Note-on sent for MIDI " + std::to_string(testMidi));
        
        bool hasAudio = false;
        
        // Procesování všech bloků
        for (int block = 0; block < totalBlocks; ++block) {
            // Clear buffers před procesováním
            std::fill(leftBuffer, leftBuffer + blockSize, 0.0f);
            std::fill(rightBuffer, rightBuffer + blockSize, 0.0f);
            
            hasAudio = voiceManager.processBlockUninterleaved(leftBuffer, rightBuffer, blockSize);
            
            // Přidání do export vektorů, pokud je export zapnut
            if (exportAudio) {
                for (int j = 0; j < blockSize; ++j) {
                    if (block < noteOnBlocks) {
                        noteOnExportBuffer.push_back(leftBuffer[j]);
                        noteOnExportBuffer.push_back(rightBuffer[j]);
                    } else {
                        releaseExportBuffer.push_back(leftBuffer[j]);
                        releaseExportBuffer.push_back(rightBuffer[j]);
                    }
                    fullTestExportBuffer.push_back(leftBuffer[j]);
                    fullTestExportBuffer.push_back(rightBuffer[j]);
                }
                
                // Debug info pro prvních 5 bloků
                if (block < 5) {
                    float maxSample = 0.0f;
                    for (int j = 0; j < blockSize; ++j) {
                        maxSample = std::max(maxSample, std::max(std::abs(leftBuffer[j]), std::abs(rightBuffer[j])));
                    }
                    logger.log("runSimpleNoteTest", LogSeverity::Info, 
                              "Block " + std::to_string(block) + " max sample: " + std::to_string(maxSample));
                }
            }
            
            // Note-off na konci note-on fáze
            if (block == noteOnBlocks - 1) {
                voiceManager.setNoteStateMIDI(testMidi, false);
                logger.log("runSimpleNoteTest", LogSeverity::Info, "Note-off sent - starting release phase");
            }
        }
        
        // Export WAV souborů, pokud je zapnuto
        if (exportAudio) {
            exportTestAudio("simple_note_on.wav", noteOnExportBuffer.data(), 
                           static_cast<int>(noteOnExportBuffer.size() / 2), 2, sampleRate, logger);
            exportTestAudio("simple_release.wav", releaseExportBuffer.data(), 
                           static_cast<int>(releaseExportBuffer.size() / 2), 2, sampleRate, logger);
            exportTestAudio("simple_full_test.wav", fullTestExportBuffer.data(), 
                           static_cast<int>(fullTestExportBuffer.size() / 2), 2, sampleRate, logger);
            logger.log("runSimpleNoteTest", LogSeverity::Info, "Exported all simple note test audio files");
        }
        
        // Cleanup
        delete[] leftBuffer;
        delete[] rightBuffer;
        
        if (hasAudio) {
            logger.log("runSimpleNoteTest", LogSeverity::Info, "Simple note test passed - audio output detected and exported");
            return true;
        } else {
            logger.log("runSimpleNoteTest", LogSeverity::Warning, "Simple note test failed - no audio output detected");
            return false;
        }
        
    } catch (const std::exception& e) {
        logger.log("runSimpleNoteTest", LogSeverity::Error, "Simple note test failed: " + std::string(e.what()));
        return false;
    } catch (...) {
        logger.log("runSimpleNoteTest", LogSeverity::Error, "Simple note test failed: unknown error");
        return false;
    }
}