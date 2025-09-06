#define _CRT_SECURE_NO_WARNINGS  // Ignorování warningu pro strncpy v MSVC

#include "sampler.h"  // Include hlavičky pro definice SampleInfo a SamplerIO

/**
 * @brief Funkce pro řízení sampleru – načte WAV soubory z adresáře, vyhledá příklad a loguje výsledky.
 * Tato funkce je volána z main.cpp a obsahuje celou logiku sampleru.
 * Cesta k adresáři je pevně zakódována (lze upravit).
 * Používá logger pro všechny výstupy (info, warn).
 * Deleguje načítání do SamplerIO::loadSamples.
 * @param logger Reference na Logger pro logování.
 * @return 0 při úspěchu, 1 při chybě (ale chyby jsou řešeny ukončením programu).
 */
int runSampler(Logger& logger) {
    logger.log("runSampler", "info", "Starting sampler.");

    // REF: Načtení a test sampleru – delegace do SamplerIO
    SamplerIO sampler;
    std::string sampleDir = R"(c:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)";
    sampler.loadSamples(sampleDir, logger);  // Delegace načítání (obsahuje logování a exit při chybě)
    
    // REF: Příklad vyhledání (MIDI 108, velocity 7) – použití nového jména a getterů
    int index = sampler.findSampleInSampleList(108, 7);
    if (index != -1) {
        // Použití getterů pro přístup k metadatům (předá logger pro kontrolu)
        std::string filename = sampler.getFilename(index, logger);
        int frequency = sampler.getSampleFrequency(index, logger);
        uint8_t midiNote = sampler.getMidiNote(index, logger);
        uint8_t velocity = sampler.getMidiNoteVelocity(index, logger);
        
        std::string msg = "Found sample: " + std::string(filename) + 
                          ", MIDI: " + std::to_string(midiNote) + 
                          ", Vel: " + std::to_string(velocity) + 
                          ", frequency: " + std::to_string(frequency) + " Hz";
        logger.log("runSampler/findSampleInSampleList", "info", msg);
        return 0;  // Úspěch
    } else {
        logger.log("runSampler/findSampleInSampleList", "warn", "Sample for MIDI 108 vel 7 not found.");
        return 0;  // Žádná chyba, jen nenalezeno
    }
}