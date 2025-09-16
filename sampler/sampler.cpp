#include <iostream>
#include <memory>

#include "sampler.h"
#include "voice_manager.h"
#include "envelopes/envelope_static_data.h"
#include "tests/tests.h"  // Import testovacích funkcí


/**
 * @brief CORE: runSampler - čistá produkční implementace
 * 
 * Inicializuje a ověří základní funkčnost sampler systému.
 * Bez testů, bez demo funkcí - pouze core funkcionalita.
 * Testy jsou nyní vyčleněny do dedikovaného modulu tests/.
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
        
        // FÁZE 5: Základní ověření funkčnosti (refaktorováno do tests/)
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