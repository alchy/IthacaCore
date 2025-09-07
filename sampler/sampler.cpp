#include "sampler.h"  // Include hlavičky pro definice SampleInfo a SamplerIO

/**
 * @brief Funkce pro řízení sampleru – prohledá adresář s WAV soubory, vyhledá příklad a loguje výsledky.
 * Tato funkce je volána z main.cpp a obsahuje celou logiku sampleru.
 * Cesta k adresáři je pevně zakódována (lze upravit).
 * Používá logger pro všechny výstupy (info, warn).
 * Deleguje prohledávání do SamplerIO::scanSampleDirectory.
 * ROZŠÍŘENO: Nyní také zobrazuje informace o interleaved formátu a potřebě konverze.
 * @param logger Reference na Logger pro logování.
 * @return 0 při úspěchu, 1 při chybě (ale chyby jsou řešeny ukončením programu).
 */
int runSampler(Logger& logger) {
    logger.log("runSampler", "info", "Starting sampler.");

    // REF: Prohledání a test sampleru – delegace do SamplerIO
    SamplerIO sampler;
    std::string sampleDir = R"(c:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)";
    sampler.scanSampleDirectory(sampleDir, logger);  // Delegace prohledávání (obsahuje logování a exit při chybě)
    
    // REF: Příklad vyhledávání (MIDI 108, velocity 7) – použití nového jména a getterů
    int index = sampler.findSampleInSampleList(108, 7);
    if (index != -1) {
        // Použití getterů pro přístup k metadatům (předá logger pro kontrolu)
        std::string filename = sampler.getFilename(index, logger);
        int frequency = sampler.getFrequency(index, logger);
        uint8_t midiNote = sampler.getMidiNote(index, logger);
        uint8_t velocity = sampler.getMidiNoteVelocity(index, logger);
        
        // Původní metadata
        sf_count_t sampleCount = sampler.getSampleCount(index, logger);
        double duration = sampler.getDurationInSeconds(index, logger);  // Upravené volání
        int channels = sampler.getChannelCount(index, logger);
        bool isStereo = sampler.getIsStereo(index, logger);
        
        // NOVÉ metadata - rozšířené atributy
        bool isInterleaved = sampler.getIsInterleavedFormat(index, logger);  // Upravené volání
        bool needsConversion = sampler.getNeedsConversion(index, logger);
        
        // Sestavení detailní zprávy se všemi metadaty včetně nových atributů
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
        
        // NOVÉ: Dodatečné logování pro detailní analýzu formátu
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
        logger.log("runSampler/findSampleInSampleList", "warn", "Sample for MIDI 108 vel 7 not found.");
        return 0;  // Žádná chyba, jen nenalezeno
    }
}