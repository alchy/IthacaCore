#define _CRT_SECURE_NO_WARNINGS  // Ignorování warningu pro strncpy v MSVC

#include "sampler.h"  // Include hlavičky pro definice Sampler a SamplerIO
#include <algorithm>  // Pro std::min
#include <cstdlib>    // Pro std::exit

/**
 * @brief Konstruktor Sampler třídy
 * @param logger Reference na logger pro zaznamenávání
 */
Sampler::Sampler(Logger& logger) : logger_(logger) {
    logger_.log("Sampler/constructor", "info", "Sampler main coordinator instance created");
    logger_.log("Sampler/constructor", "info", "Initializing SamplerIO module");
    
    // Vytvoření SamplerIO modulu
    samplerIO_ = std::make_unique<SamplerIO>(logger_);
    
    logger_.log("Sampler/constructor", "info", "Sampler initialization completed successfully");
}

/**
 * @brief Destruktor Sampler třídy
 */
Sampler::~Sampler() {
    logger_.log("Sampler/destructor", "info", "Sampler coordinator instance being destroyed");
    // samplerIO_ se automaticky zničí díky unique_ptr
    logger_.log("Sampler/destructor", "info", "All sub-modules cleaned up successfully");
}

/**
 * @brief Načte samples z adresáře pomocí SamplerIO modulu
 * @param directoryPath Cesta k adresáři se samples
 */
void Sampler::loadSamples(const std::string& directoryPath) {
    logger_.log("Sampler/loadSamples", "info", "Delegating sample loading to SamplerIO module");
    logger_.log("Sampler/loadSamples", "info", "Target directory: " + directoryPath);
    
    if (!samplerIO_) {
        logger_.log("Sampler/loadSamples", "error", "SamplerIO module not initialized - critical error");
        logger_.log("Sampler/loadSamples", "error", "Program termination due to module failure");
        std::exit(1);
    }
    
    samplerIO_->loadSamples(directoryPath);
    
    logger_.log("Sampler/loadSamples", "info", "Sample loading delegation completed successfully");
}

/**
 * @brief Vyhledá sample podle MIDI noty a velocity
 * @param midi_note MIDI nota (0-127)
 * @param velocity Velocity (0-7)
 * @return Index v seznamu nebo -1 pokud nenalezeno
 */
int Sampler::findSample(uint8_t midi_note, uint8_t velocity) const {
    logger_.log("Sampler/findSample", "info", "Delegating sample search to SamplerIO module");
    
    if (!samplerIO_) {
        logger_.log("Sampler/findSample", "error", "SamplerIO module not initialized - returning failure");
        return -1;
    }
    
    int result = samplerIO_->findSample(midi_note, velocity);
    
    logger_.log("Sampler/findSample", "info", "Sample search delegation completed");
    return result;
}

/**
 * @brief Getter pro přístup k sample seznamu
 * @return Konstantní reference na vektor SampleInfo
 */
const std::vector<SampleInfo>& Sampler::getSampleList() const {
    logger_.log("Sampler/getSampleList", "info", "Delegating sample list access to SamplerIO module");
    
    if (!samplerIO_) {
        logger_.log("Sampler/getSampleList", "error", "SamplerIO module not initialized - returning empty list");
        static const std::vector<SampleInfo> empty;
        return empty;
    }
    
    return samplerIO_->getSampleList();
}

/**
 * @brief Funkce pro řízení hlavního sampleru – vytváří Sampler instanci a řídí workflow.
 * Tato funkce je volána z main.cpp a obsahuje celou logiku koordinace modulů.
 * Cesta k adresáři je pevně zakódována (lze upravit).
 * DŮLEŽITÉ: Žádné výstupy na konzoli - pouze logování přes Logger.
 * @param logger Reference na Logger pro logování.
 * @return 0 při úspěchu, 1 při chybě (ale chyby jsou řešeny ukončením programu).
 */
int runSampler(Logger& logger) {
    logger.log("runSampler", "info", "=== Starting IthacaCore Sampler System ===");
    logger.log("runSampler", "info", "Initializing main sampler coordinator");

    // Vytvoření hlavní Sampler instance
    logger.log("runSampler", "info", "Creating main Sampler coordinator instance");
    Sampler sampler(logger);
    
    // Definice cesty k sample adresáři
    std::string sampleDir = R"(c:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)";
    logger.log("runSampler", "info", "Sample directory configured: " + sampleDir);
    
    // Načtení samples přes Sampler koordinátor
    logger.log("runSampler", "info", "=== Starting sample loading phase ===");
    sampler.loadSamples(sampleDir);
    
    // Kontrola počtu načtených samples
    const auto& sampleList = sampler.getSampleList();
    if (sampleList.empty()) {
        logger.log("runSampler", "warn", "No samples were loaded - sample library is empty");
        logger.log("runSampler", "info", "Sampler system completed with empty library");
        return 0;
    }
    
    logger.log("runSampler", "info", "=== Sample loading phase completed ===");
    logger.log("runSampler", "info", "Total samples loaded into system: " + std::to_string(sampleList.size()));
    
    // Testování vyhledávacího systému
    logger.log("runSampler", "info", "=== Starting sample search system testing ===");
    uint8_t testMidi = 108;
    uint8_t testVelocity = 7;
    
    logger.log("runSampler", "info", 
              "Executing test search for MIDI=" + std::to_string(testMidi) + 
              ", Velocity=" + std::to_string(testVelocity));
    
    int index = sampler.findSample(testMidi, testVelocity);
    
    if (index != -1) {
        const auto& sample = sampleList[index];
        std::string successMsg = "Search system working correctly! Found sample: " + std::string(sample.filename) + 
                               " | Sample rate: " + std::to_string(sample.sample_frequency) + " Hz" +
                               " | Library position: " + std::to_string(index);
        logger.log("runSampler/searchTest", "info", successMsg);
        
        // Dodatečné informace o nalezeném samplu
        logger.log("runSampler/searchTest", "info", 
                  "Sample verification - MIDI: " + std::to_string(sample.midi_note) + 
                  ", Velocity: " + std::to_string(sample.midi_note_velocity));
    } else {
        logger.log("runSampler/searchTest", "warn", 
                  "Search test failed - target sample not found: MIDI=" + std::to_string(testMidi) + 
                  ", Velocity=" + std::to_string(testVelocity));
        
        // Ukázka dostupných samples pro debugging
        if (!sampleList.empty()) {
            logger.log("runSampler/searchTest", "info", "Available samples preview (first 5):");
            int displayCount = std::min(5, static_cast<int>(sampleList.size()));
            for (int i = 0; i < displayCount; ++i) {
                const auto& sample = sampleList[i];
                logger.log("runSampler/searchTest", "info", 
                          "  [" + std::to_string(i) + "] MIDI=" + std::to_string(sample.midi_note) + 
                          ", Vel=" + std::to_string(sample.midi_note_velocity) + 
                          " (" + std::string(sample.filename) + ")");
            }
            if (sampleList.size() > 5) {
                logger.log("runSampler/searchTest", "info", 
                          "  ... and " + std::to_string(sampleList.size() - 5) + " more samples available");
            }
        }
    }
    
    logger.log("runSampler", "info", "=== Search system testing completed ===");
    logger.log("runSampler", "info", "=== IthacaCore Sampler System completed successfully ===");
    logger.log("runSampler", "info", "All modules operational - system ready for use");
    
    return 0;  // Vždy úspěch (chyby jsou řešeny exit v modulech)
}