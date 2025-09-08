#include "sampler.h"  // SamplerIO
#include "instrument_loader.h"  // InstrumentLoader
#include "wav_file_exporter.h"  // WavExporter pro test exportu
#include "voice_manager.h"      // VoiceManager pro polyfonnÃ­ testy

// ✅ PŘIDANÉ INCLUDES PRO ZÁKLADNÍ FUNKCIONALITU:
#include <string>       // Pro std::string
#include <exception>    // Pro std::exception
#include <stdexcept>    // Pro std::runtime_error
#include <vector>       // Pro std::vector
#include <algorithm>    // Pro std::min, std::max
#include <iostream>     // Pro std::cout (pokud potřeba)
#include <cstdlib>      // Pro std::exit

/**
 * @brief Hlavní koordinátor sampleru – spustí celý workflow.
 * @param logger Reference na Logger pro logování.
 * @return 0 při úspěchu.
 */
int runSampler(Logger& logger) {
    logger.log("runSampler", "info", "Starting IthacaCore sampler workflow");

    /* ======= Globální nastavení proměnných (společné pro celý workflow) ====== */
    // Cesta k samples (upravte podle potřeby)
    std::string sampleDir = R"(C:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)";
    
    // Konfigurace pro načítání (targetSampleRate)
    int targetSampleRate = 44100;  // Typická frekvence pro audio plugin
    /* ======= Konec globálního nastavení ====== */

    /* ======= Hlavní workflow: Inicializace a načtení samples ====== */
    // Krok 1: Inicializace SamplerIO a prohledání adresáře
    SamplerIO sampler; // prázdný konstruktor
    logger.log("runSampler", "info", "Scanning sample directory: " + sampleDir);
    sampler.scanSampleDirectory(sampleDir, logger);

    // Krok 2: Načtení do paměti jako stereo buffery
    InstrumentLoader instrument;  // prázdný konstruktor
    instrument.loadInstrumentData(sampler, targetSampleRate, logger);  // načtení dat

    /* ======= PŘESUNUTÉ A OPRAVENÉ: Voice Management testy PŘED destrukcí InstrumentLoader ====== */
    try {
        logger.log("runSampler", "info", "Starting Voice Management system tests");
        
        // ✅ DEBUGGING: Kontrola existence samples pro MIDI 60
        logger.log("runSampler", "info", "=== DEBUGGING: Checking MIDI 60 samples ===");
        
        uint8_t testMidiNote = 60;  // Default test note
        Instrument& debugInst = instrument.getInstrumentNote(60);
        bool hasSamplesFor60 = false;
        
        for (int vel = 0; vel < 8; ++vel) {
            if (debugInst.velocityExists[vel]) {
                hasSamplesFor60 = true;
                logger.log("runSampler", "info", "MIDI 60 has sample at velocity layer " + 
                           std::to_string(vel) + " with " + 
                           std::to_string(debugInst.get_frame_count(vel)) + " frames");
                
                // Test prvních sample values
                float* buffer = debugInst.get_sample_begin_pointer(vel);
                if (buffer) {
                    logger.log("runSampler", "info", "First samples: L=" + 
                               std::to_string(buffer[0]) + ", R=" + std::to_string(buffer[1]));
                } else {
                    logger.log("runSampler", "warn", "Buffer pointer is NULL for velocity " + std::to_string(vel));
                }
            }
        }
        
        if (!hasSamplesFor60) {
            logger.log("runSampler", "warn", "MIDI 60 has NO samples available! Searching alternatives...");
            // Hledání alternativní MIDI noty
            for (int midi = 50; midi <= 70; ++midi) {
                Instrument& testInst = instrument.getInstrumentNote(midi);
                for (int vel = 0; vel < 8; ++vel) {
                    if (testInst.velocityExists[vel]) {
                        logger.log("runSampler", "info", "Using alternative: MIDI " + std::to_string(midi) + 
                                   " has samples at velocity " + std::to_string(vel));
                        testMidiNote = static_cast<uint8_t>(midi);
                        goto found_alternative;
                    }
                }
            }
            found_alternative:;
        }
        
        logger.log("runSampler", "info", "=== END DEBUGGING ===");
        
        // Parametry pro VoiceManager test
        std::string voiceTestDir = sampleDir;
        int voiceTestSampleRate = targetSampleRate;
        
        // ✅ KRITICKÁ OPRAVA: VoiceManager musí používat STEJNÝ InstrumentLoader instance
        VoiceManager voiceManager(voiceTestDir, voiceTestSampleRate, logger);
        
        // ✅ SPECIÁLNÍ INICIALIZACE: Použij už načtený InstrumentLoader
        logger.log("runSampler", "info", "Initializing VoiceManager with existing InstrumentLoader...");
        
        // Manuální inicializace všech 128 voices s už načteným InstrumentLoader
        for (int i = 0; i < 128; ++i) {
            uint8_t midiNote = static_cast<uint8_t>(i);
            const Instrument& inst = instrument.getInstrumentNote(midiNote);
            Voice& voice = voiceManager.getVoice(midiNote);
            voice.initialize(inst, voiceTestSampleRate, logger);
        }
        
        logger.log("runSampler", "info", "VoiceManager initialized with " + 
                   std::to_string(voiceManager.getMaxVoices()) + " voices");
        
        // Test 1: Single note test s ověřenou MIDI notou
        uint8_t testVelocity = 100;
        
        logger.log("runSampler", "info", "Test 1: Single note playback (MIDI " + 
                   std::to_string(testMidiNote) + ")");
        
        // Ověření před spuštěním
        Voice& testVoice = voiceManager.getVoice(testMidiNote);
        logger.log("runSampler", "info", "Pre-test voice state: " + 
                   std::to_string(static_cast<int>(testVoice.getState())));
        
        voiceManager.setNoteState(testMidiNote, true, testVelocity);
        logger.log("runSampler", "info", "Active voices after note-on: " + 
                   std::to_string(voiceManager.getActiveVoicesCount()));
        
        // Post note-on ověření
        logger.log("runSampler", "info", "Post note-on voice state: " + 
                   std::to_string(static_cast<int>(testVoice.getState())) + 
                   ", velocity layer: " + std::to_string(testVoice.getCurrentVelocityLayer()));
        
        // Simulace několika audio bloků
        const int blockSize = 512;
        const int numBlocks = 5;
        
        for (int block = 0; block < numBlocks; ++block) {
            // Alokace test bufferů
            float* leftBuffer = new float[blockSize];
            float* rightBuffer = new float[blockSize];
            
            bool hasAudio = voiceManager.processBlock(leftBuffer, rightBuffer, blockSize);
            
            if (hasAudio) {
                // Analýza výstupního audio (kontrola, že není ticho)
                float maxL = 0.0f, maxR = 0.0f;
                for (int i = 0; i < blockSize; ++i) {
                    maxL = std::max(maxL, std::abs(leftBuffer[i]));
                    maxR = std::max(maxR, std::abs(rightBuffer[i]));
                }
                
                logger.log("runSampler", "info", "Block " + std::to_string(block) + 
                           " - Peak L: " + std::to_string(maxL) + 
                           ", Peak R: " + std::to_string(maxR));
            } else {
                logger.log("runSampler", "info", "Block " + std::to_string(block) + " - Silent");
            }
            
            delete[] leftBuffer;
            delete[] rightBuffer;
        }
        
        voiceManager.setNoteState(testMidiNote, false, 0);
        logger.log("runSampler", "info", "Note-off sent for MIDI " + std::to_string(testMidiNote));
        
        // Test release - několik dalších bloků
        logger.log("runSampler", "info", "Testing release phase...");
        for (int block = 0; block < 3; ++block) {
            float* leftBuffer = new float[blockSize];
            float* rightBuffer = new float[blockSize];
            
            bool hasAudio = voiceManager.processBlock(leftBuffer, rightBuffer, blockSize);
            
            if (hasAudio) {
                float maxL = 0.0f, maxR = 0.0f;
                for (int i = 0; i < blockSize; ++i) {
                    maxL = std::max(maxL, std::abs(leftBuffer[i]));
                    maxR = std::max(maxR, std::abs(rightBuffer[i]));
                }
                
                logger.log("runSampler", "info", "Release block " + std::to_string(block) + 
                           " - Peak L: " + std::to_string(maxL) + 
                           ", Peak R: " + std::to_string(maxR) + 
                           " (should decrease)");
            }
            
            delete[] leftBuffer;
            delete[] rightBuffer;
        }
        
        // Test 2: Polyphony test s ověřenými notami
        logger.log("runSampler", "info", "Test 2: Polyphony test");
        
        std::vector<uint8_t> testNotes;
        
        // Najdi 3 noty, které mají samples
        for (int midi = 50; midi <= 80 && testNotes.size() < 3; ++midi) {
            Instrument& checkInst = instrument.getInstrumentNote(midi);
            for (int vel = 0; vel < 8; ++vel) {
                if (checkInst.velocityExists[vel]) {
                    testNotes.push_back(static_cast<uint8_t>(midi));
                    logger.log("runSampler", "info", "Using MIDI " + std::to_string(midi) + " for polyphony test");
                    break;
                }
            }
        }
        
        if (testNotes.size() >= 3) {
            // Note-on pro všechny noty
            for (uint8_t note : testNotes) {
                voiceManager.setNoteState(note, true, testVelocity);
            }
            
            logger.log("runSampler", "info", "Active voices after chord: " + 
                       std::to_string(voiceManager.getActiveVoicesCount()) + 
                       " (sustaining: " + std::to_string(voiceManager.getSustainingVoicesCount()) + ")");
            
            // Jeden blok polyphonic audio
            float* leftBuffer = new float[blockSize];
            float* rightBuffer = new float[blockSize];
            
            if (voiceManager.processBlock(leftBuffer, rightBuffer, blockSize)) {
                float maxL = 0.0f, maxR = 0.0f;
                for (int i = 0; i < blockSize; ++i) {
                    maxL = std::max(maxL, std::abs(leftBuffer[i]));
                    maxR = std::max(maxR, std::abs(rightBuffer[i]));
                }
                
                logger.log("runSampler", "info", "Polyphonic audio - Peak L: " + 
                           std::to_string(maxL) + ", Peak R: " + std::to_string(maxR));
            } else {
                logger.log("runSampler", "warn", "Polyphonic test produced silence");
            }
            
            delete[] leftBuffer;
            delete[] rightBuffer;
        } else {
            logger.log("runSampler", "warn", "Not enough samples for polyphony test");
        }
        
        voiceManager.stopAllVoices();
        logger.log("runSampler", "info", "All voices stopped. Active count: " + 
                   std::to_string(voiceManager.getActiveVoicesCount()) + 
                   " (releasing: " + std::to_string(voiceManager.getReleasingVoicesCount()) + ")");
        
        // Test 3: Edge cases
        logger.log("runSampler", "info", "Test 3: Edge cases");
        
        voiceManager.setNoteState(static_cast<uint8_t>(200), true, 100);  // Invalid MIDI
        voiceManager.setNoteState(60, true, 0);
        voiceManager.setNoteState(61, true, 127);
        
        logger.log("runSampler", "info", "Active voices after edge case tests: " + 
                   std::to_string(voiceManager.getActiveVoicesCount()));
        
        // Test 4: Individual voice access
        logger.log("runSampler", "info", "Test 4: Individual voice access and state monitoring");
        
        Voice& finalTestVoice = voiceManager.getVoice(testMidiNote);
        logger.log("runSampler", "info", "Voice " + std::to_string(testMidiNote) + " state: " + 
                   std::to_string(static_cast<int>(finalTestVoice.getState())) + 
                   ", position: " + std::to_string(finalTestVoice.getPosition()) + 
                   ", gain: " + std::to_string(finalTestVoice.getCurrentGain()));
        
        // Cleanup
        voiceManager.resetAllVoices(logger);
        
        logger.log("runSampler", "info", "Voice Management system tests completed successfully");
        
    } catch (const std::exception& e) {
        logger.log("runSampler", "error", "Exception in Voice Management tests: " + std::string(e.what()));
        return 1;
    } catch (...) {
        logger.log("runSampler", "error", "Unknown exception in Voice Management tests");
        return 1;
    }
    /* ======= KONEC Voice Management testů ====== */

    /* ======= TESTY: Přístup k instrumentu a export ====== */
    try {
        uint8_t testMidi = 108;
        uint8_t testVel = 5;
        
        // Krok 3: Test přístupu k jednomu instrumentu
        Instrument& instrument_note = instrument.getInstrumentNote(testMidi);
        if (instrument_note.velocityExists[testVel]) {
            logger.log("runSampler", "info", "Test access OK: MIDI " + std::to_string(testMidi) + "/vel " + std::to_string(testVel) + 
                       " exists. Frames: " + std::to_string(instrument_note.get_frame_count(testVel)) + 
                       ", Total samples: " + std::to_string(instrument_note.get_total_sample_count(testVel)) + 
                       ", Originally mono: " + (instrument_note.get_was_originally_mono(testVel) ? "yes" : "no"));

            // Export test
            std::string exportDir = "./exports";
            std::string exportFilename = "export_test.wav";
            sf_count_t framesPerBuffer = 512;
            bool isStereo = true;
            bool realWrite = true;
            
            logger.log("runSampler", "info", "Starting WAV export test for verification");
            WavExporter exporter(exportDir, logger);

            float* exportBuffer = exporter.wavFileCreate(exportFilename, targetSampleRate, static_cast<int>(framesPerBuffer), isStereo, realWrite);
            if (exportBuffer) {
                sf_count_t totalFrames = instrument_note.get_frame_count(testVel);
                sf_count_t remainingFrames = totalFrames;
                float* sourceData = instrument_note.get_sample_begin_pointer(testVel);

                while (remainingFrames > 0) {
                    sf_count_t thisBufferFrames = std::min(framesPerBuffer, remainingFrames);
                    size_t offset = (totalFrames - remainingFrames) * 2;
                    
                    for (sf_count_t i = 0; i < thisBufferFrames * 2; ++i) {
                        exportBuffer[i] = sourceData[offset + i];
                    }

                    if (!exporter.wavFileWriteBuffer(exportBuffer, static_cast<int>(thisBufferFrames))) {
                        logger.log("runSampler", "error", "Export write failed");
                        return 1;
                    }
                    
                    remainingFrames -= thisBufferFrames;
                }

                logger.log("runSampler", "info", "WAV export completed: " + exportFilename + " in " + exportDir + 
                           ". Listen to verify mono->stereo conversion and data integrity (format: Pcm16).");
            } else {
                logger.log("runSampler", "error", "Failed to create export buffer");
                return 1;
            }
        } else {
            logger.log("runSampler", "warn", "Test sample MIDI " + std::to_string(testMidi) + "/vel " + std::to_string(testVel) + " not found");
        }
    } catch (const std::exception& e) {
        logger.log("runSampler", "error", "Exception in tests: " + std::string(e.what()));
        return 1;
    } catch (...) {
        logger.log("runSampler", "error", "Unknown exception in tests");
        return 1;
    }

    /* ======= Závěrečné statistiky ====== */
    logger.log("runSampler", "info", "Total loaded samples: " + std::to_string(instrument.getTotalLoadedSamples()));
    logger.log("runSampler", "info", "Mono originally: " + std::to_string(instrument.getMonoSamplesCount()) + 
               ", Stereo originally: " + std::to_string(instrument.getStereoSamplesCount()));

    logger.log("runSampler", "info", "Sampler workflow completed successfully");
    return 0;
}