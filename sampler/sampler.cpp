#include "sampler.h"  // SamplerIO
#include "instrument_loader.h"  // InstrumentLoader
#include "wav_file_exporter.h"  // WavExporter pro test exportu

/**
 * @brief Hlavní koordinátor sampleru – spustí celý workflow.
 * @param logger Reference na Logger pro logování.
 * @return 0 při úspěchu.
 * 
 * Workflow:
 * 1. Prohledá adresář se samples (SamplerIO).
 * 2. Načte všechny samples do paměti jako stereo float buffery (InstrumentLoader).
 * 3. TESTY: Na konci v try-catch bloku – test přístupu k instrumentu (retrieve: MIDI 108, vel 7) a export WAV pro ověření konverze.
 *    - Provádí se vždy (pokud sample existuje).
 *    - Používá Pcm16 formát (default), buffer 512 samples (JUCE-like).
 *    - Měří časy zápisu pro debugging.
 *    - Vytvoří soubor ./exports/export_test.wav pro poslech.
 * 4. Statistiky na úplném konci.
 * Chyby v testech: Zachytí try-catch, log + return 1. Jiné chyby končí std::exit(1) po logu.
 */
int runSampler(Logger& logger) {
    logger.log("runSampler", "info", "Starting IthacaCore sampler workflow");

    /* ======= Globální nastavení proměnných (společné pro celý workflow) ====== */
    // Cesta k samples (upravte podle potřeby)
    std::string sampleDir = R"(C:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)";
    
    // Konfigurace pro načítání (targetSampleRate)
    int targetSampleRate = 44100;  // Typická frekvence pro audio plugin
    /* ======= Konец globálního nastavení ====== */

    /* ======= Hlavní workflow: Inicializace a načtení samples ====== */
    // Krok 1: Inicializace SamplerIO a prohledání adresáře
    SamplerIO sampler;
    logger.log("runSampler", "info", "Scanning sample directory: " + sampleDir);
    sampler.scanSampleDirectory(sampleDir, logger);

    // Krok 2: Načtení do paměti jako stereo buffery
    InstrumentLoader loader(sampler, targetSampleRate, logger);
    loader.loadInstrument();

    /* ======= TESTY: Přístup k instrumentu a export (v try-catch na konci) ====== */
    try {
        /* ======= Parametry pro retrieve (test přístupu) ====== */
        uint8_t testMidi = 108;  // Příklad MIDI noty pro test přístupu
        uint8_t testVel = 5;     // Příklad velocity vrstvy pro test přístupu
        /* ======= Konец parametrů pro retrieve ====== */
        
        // Krok 3: Test přístupu k jednomu instrumentu (příklad: MIDI 108, vel 7)
        Instrument& testInst = loader.getInstrumentNote(testMidi);
        if (testInst.velocityExists[testVel]) {
            logger.log("runSampler", "info", "Test access OK: MIDI " + std::to_string(testMidi) + "/vel " + std::to_string(testVel) + 
                       " exists. Frames: " + std::to_string(testInst.get_frame_count(testVel)) + 
                       ", Total samples: " + std::to_string(testInst.get_total_sample_count(testVel)) + 
                       ", Originally mono: " + (testInst.get_was_originally_mono(testVel) ? "yes" : "no"));

            /* ======= Parametry pro test exportu ====== */
            std::string exportDir = "./exports";  // Adresář pro výstupní WAV soubory
            std::string exportFilename = "export_test.wav";  // Název výstupního souboru
            sf_count_t framesPerBuffer = 512;     // Velikost bufferu pro JUCE-like zpracování (samples na blok)
            bool isStereo = true;                 // Formát: stereo (interleaved [L,R,L,R...])
            bool realWrite = true;                // Reálný zápis (ne dummy)
            /* ======= Konец parametrů pro test exportu ====== */
            
            // Inicializace exportu
            logger.log("runSampler", "info", "Starting WAV export test for verification");
            WavExporter exporter(exportDir, logger);  // Default Pcm16

            float* exportBuffer = exporter.wavFileCreate(exportFilename, targetSampleRate, static_cast<int>(framesPerBuffer), isStereo, realWrite);  // Stereo, reálný zápis
            if (exportBuffer) {
                sf_count_t totalFrames = testInst.get_frame_count(testVel);
                sf_count_t remainingFrames = totalFrames;
                float* sourceData = testInst.get_sample_begin_pointer(testVel);  // Zdroj: [L,R,L,R...] ze sample

                // Zjednodušená smyčka exportu (minimalizováno vnoření)
                while (remainingFrames > 0) {
                    sf_count_t thisBufferFrames = std::min(framesPerBuffer, remainingFrames);
                    size_t offset = (totalFrames - remainingFrames) * 2;  // *2 pro stereo
                    
                    // Kopírování ze source do exportBuffer (interleaved stereo)
                    for (sf_count_t i = 0; i < thisBufferFrames * 2; ++i) {
                        exportBuffer[i] = sourceData[offset + i];
                    }

                    // Zápis bufferu (měří čas)
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
    /* ======= KONEC TESTŮ ====== */

    /* ======= Závěrečné statistiky ====== */
    // Statistiky
    logger.log("runSampler", "info", "Total loaded samples: " + std::to_string(loader.getTotalLoadedSamples()));
    logger.log("runSampler", "info", "Mono originally: " + std::to_string(loader.getMonoSamplesCount()) + 
               ", Stereo originally: " + std::to_string(loader.getStereoSamplesCount()));

    logger.log("runSampler", "info", "Sampler workflow completed successfully");
    return 0;
}