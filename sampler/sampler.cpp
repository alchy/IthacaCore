// Ukázka integrace do sampler.cpp
// Přidejte tento include na začátek souboru:
#include "instrument_loader.h"

// Upravená funkce runSampler() s integrací InstrumentLoader:

/**
 * @brief Funkce pro řízení sampleru – prohledá adresář s WAV soubory, načte je do paměti a testuje přístup.
 * ROZŠÍŘENO: Nově také načítá samples do paměti jako 32-bit float buffery pomocí InstrumentLoader.
 * @param logger Reference na Logger pro logování.
 * @return 0 při úspěchu, 1 při chybě (ale chyby jsou řešeny ukončením programu).
 */
int runSampler(Logger& logger) {
    logger.log("runSampler", "info", "Starting sampler with InstrumentLoader integration.");

    // REF: Prohledání a test sampleru – delegace do SamplerIO
    SamplerIO sampler;
    std::string sampleDir = R"(c:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)";
    sampler.scanSampleDirectory(sampleDir, logger);  // Delegace prohledávání (obsahuje logování a exit při chybě)
    
    // NOVÉ: Inicializace InstrumentLoader a načtení všech samplů do paměti
    logger.log("runSampler", "info", "Initializing InstrumentLoader for 44100 Hz target sample rate.");
    InstrumentLoader loader(sampler, 44100, logger);
    
    // Načtení všech instrumentů do RAM jako float buffery
    loader.loadAllInstruments();
    
    // REF: Příklad vyhledávání (MIDI 108, velocity 7, sample rate 44100 Hz) – použití rozšířeného vyhledávání
    int index = sampler.findSampleInSampleList(108, 7, 44100);
    if (index != -1) {
        // Použití getterů pro přístup k metadatům (předá logger pro kontrolu)
        std::string filename = sampler.getFilename(index, logger);
        int frequency = sampler.getFrequency(index, logger);
        uint8_t midiNote = sampler.getMidiNote(index, logger);
        uint8_t velocity = sampler.getMidiNoteVelocity(index, logger);
        
        // Původní metadata
        sf_count_t sampleCount = sampler.getSampleCount(index, logger);
        double duration = sampler.getDurationInSeconds(index, logger);
        int channels = sampler.getChannelCount(index, logger);
        bool isStereo = sampler.getIsStereo(index, logger);
        
        // Metadata - rozšířené atributy
        bool isInterleaved = sampler.getIsInterleavedFormat(index, logger);
        bool needsConversion = sampler.getNeedsConversion(index, logger);
        
        // Sestavení detailní zprávy se všemi metadaty
        std::string stereoInfo = isStereo ? "stereo" : "mono";
        std::string interleavedInfo = isInterleaved ? "interleaved" : "non-interleaved";
        std::string conversionInfo = needsConversion ? "needs float conversion" : "no conversion needed";
        
        std::string msg = "Found sample: " + std::string(filename) + 
                          ", MIDI: " + std::to_string(midiNote) + 
                          ", Vel: " + std::to_string(velocity) + 
                          ", Frequency: " + std::to_string(frequency) + " Hz" +
                          ", Duration: " + std::to_string(duration) + "s" +
                          ", Frames: " + std::to_string(sampleCount) +
                          ", Channels: " + std::to_string(channels) + " (" + stereoInfo + ")" +
                          ", Format: " + interleavedInfo + 
                          ", Conversion: " + conversionInfo;
        
        logger.log("runSampler/findSampleInSampleList", "info", msg);
        
        // NOVÉ: Test přístupu k načtenému bufferu přes InstrumentLoader
        logger.log("runSampler", "info", "Testing buffer access through InstrumentLoader...");
        
        try {
            Instrument& inst = loader.getInstrument(108);
            if (inst.velocityExists[7]) {
                // Buffer byl úspěšně načten
                float* audioBuffer = inst.sample_ptr_velocity[7];
                SampleInfo* sampleInfo = inst.sample_ptr_sampleInfo[7];
                
                if (audioBuffer != nullptr && sampleInfo != nullptr) {
                    logger.log("runSampler/bufferTest", "info", 
                              "SUCCESS: Buffer pro MIDI 108 velocity 7 načten v paměti");
                    logger.log("runSampler/bufferTest", "info", 
                              "Buffer pointer: 0x" + std::to_string(reinterpret_cast<uintptr_t>(audioBuffer)));
                    logger.log("runSampler/bufferTest", "info", 
                              "SampleInfo pointer: 0x" + std::to_string(reinterpret_cast<uintptr_t>(sampleInfo)));
                    
                    // Test prvních několika samplů z bufferu
                    logger.log("runSampler/bufferTest", "info", 
                              "První 4 vzorky z bufferu: [" + 
                              std::to_string(audioBuffer[0]) + ", " +
                              std::to_string(audioBuffer[1]) + ", " +
                              std::to_string(audioBuffer[2]) + ", " +
                              std::to_string(audioBuffer[3]) + "]");
                              
                } else {
                    logger.log("runSampler/bufferTest", "error", 
                              "NULL pointery navzdory velocityExists[7] == true");
                }
            } else {
                logger.log("runSampler/bufferTest", "warn", 
                          "Buffer pro MIDI 108 velocity 7 nebyl načten (velocityExists[7] == false)");
            }
        } catch (...) {
            logger.log("runSampler/bufferTest", "error", 
                      "Výjimka při přístupu k InstrumentLoader");
        }
        
        // Dodatečné logování pro detailní analýzu formátu
        if (needsConversion) {
            logger.log("runSampler/analysis", "info", 
                      "Sample requires format conversion from PCM to 32-bit float for audio processing");
        } else {
            logger.log("runSampler/analysis", "info", 
                      "Sample is already in optimal float format, ready for direct processing");
        }
        
        if (!isInterleaved) {
            logger.log("runSampler/analysis", "warn", 
                      "Non-interleaved format detected - may require special handling");
        } else {
            logger.log("runSampler/analysis", "info", 
                      "Standard interleaved format confirmed - compatible with standard audio processing");
        }
        
        return 0;  // Úspěch
    } else {
        logger.log("runSampler/findSampleInSampleList", "warn", "Sample for MIDI 108 vel 7 at 44100 Hz not found.");
        
        // NOVÉ: I když konkrétní sample nebyl nalezen, test obecné funkčnosti loaderu
        logger.log("runSampler", "info", "Testing InstrumentLoader general functionality...");
        logger.log("runSampler", "info", 
                  "Total loaded samples: " + std::to_string(loader.getTotalLoadedSamples()));
        logger.log("runSampler", "info", 
                  "Originally mono samples: " + std::to_string(loader.getMonoSamplesCount()));
        logger.log("runSampler", "info", 
                  "Originally stereo samples: " + std::to_string(loader.getStereoSamplesCount()));
        logger.log("runSampler", "info", 
                  "Target sample rate: " + std::to_string(loader.getTargetSampleRate()) + " Hz");
        logger.log("runSampler", "info", 
                  "All samples stored as stereo interleaved [L,R,L,R...] format");
        
        return 0;  // Žádná chyba, jen nenalezeno
    }
}