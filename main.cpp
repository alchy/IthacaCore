// main.cpp - Minimální verze
#include "IthacaConfig.h"

#include "sampler/core_logger.h"
#include "sampler/sampler.h"
#include "sampler/envelopes/envelope_static_data.h"
#include <iostream>
#include <string>

/**
 * @brief Minimální main - pouze core sampler funkcionalita
 */
int main(int argc, char* argv[]) {
    std::cout << "IthacaCore Sampler System - Production Version" << std::endl;
    
    try {
        // Inicializace logger systému
        Logger logger(".");
        
        logger.log("main", LogSeverity::Info, "=== IthacaCore Sampler Starting ===");

        // Spuštění core sampler systému
        int result = runSampler(logger);

        if (result == 0) {
            logger.log("main", LogSeverity::Info, "Sampler system initialized successfully");
            std::cout << "Sampler system ready for use" << std::endl;
        } else {
            logger.log("main", LogSeverity::Error, "Sampler system initialization failed");
            std::cout << "Sampler system initialization or tests failed" << std::endl;
        }

        // Cleanup envelope dat
        EnvelopeStaticData::cleanup();

        logger.log("main", LogSeverity::Info, "=== IthacaCore Sampler Finished ===");
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "UNKNOWN CRITICAL ERROR" << std::endl;
        return 1;
    }
}