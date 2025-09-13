#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <string>
#include <vector>
#include <cstdint>

/**
 * @struct TestConfig
 * @brief Rozšířená konfigurace pro testovací framework s envelope podporou
 * CENTRALIZOVANÁ DEFINICE - používá se napříč celým systémem
 */
struct TestConfig {
    bool exportAudio = false;                    // Export audio výsledků
    int exportBlockSize = 512;                   // Block size pro export
    uint8_t defaultTestVelocity = 100;           // Default MIDI velocity
    std::vector<float> testMasterGains = {0.8f}; // Test master gain values
    std::string exportDir = "./exports/tests";   // Export directory
    bool verboseLogging = false;                 // Verbose test logging
    
    // NOVÉ envelope-specific konfigurace
    bool testEnvelopeMemoryOptimization = true;  // Test memory optimizations
    bool testSampleRateSwitching = true;         // Test sample rate changes
    std::vector<int> testSampleRates = {44100, 48000}; // Sample rates k testování
    int envelopeTestBlocks = 10;                 // Počet bloků pro envelope testy
    bool validateSharedEnvelopeData = true;      // Validace sdílených dat
};

/**
 * @struct TestResult
 * @brief Rozšířený výsledek testu s envelope metrics
 * CENTRALIZOVANÁ DEFINICE - používá se napříč celým systémem
 */
struct TestResult {
    bool passed = false;                         // Základní test result
    std::string errorMessage;                    // Chybová zpráva
    std::string details;                         // Detaily testu
    
    // NOVÉ envelope-specific metrics
    bool envelopeDataValid = false;              // Validita envelope dat
    size_t memoryFootprintBytes = 0;            // Paměťová stopa
    double averageProcessingTimeMs = 0.0;       // Průměrný processing čas
    int sampleRatesSwitched = 0;                // Počet sample rate změn
};

/**
 * @struct AudioStats
 * @brief Statistiky audio bufferu
 */
struct AudioStats {
    float peakLevel = 0.0f;
    float rmsLevel = 0.0f;
};

/**
 * @struct MasterGainTestData
 * @brief Data pro MasterGainTest
 */
struct MasterGainTestData {
    float masterGain = 0.0f;
    float measuredLevel = 0.0f;
    bool passed = false;
};

/**
 * @struct PerformanceMetrics
 * @brief Metriky pro PerformanceTest
 */
struct PerformanceMetrics {
    size_t voiceCount = 0;
    double avgBlockTimeUs = 0.0;
    double audioBlocksRatio = 0.0;
};

#endif // TEST_COMMON_H