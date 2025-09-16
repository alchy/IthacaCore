#ifndef TEST_MIXER_H
#define TEST_MIXER_H

#include <chrono>
#include "../core_logger.h"

// Forward declaration
class VoiceManager;

/**
 * @brief Hlavní test energy-based mixeru proti původnímu systému
 * 
 * Testuje rozdíly mezi starým count-based a novým energy-based mixing algoritmem.
 * Měří jak oba systémy zacházejí s kombinací silných a slabých hlasů.
 * 
 * @param voiceManager Reference na VoiceManager
 * @param logger Reference na Logger
 * @return true pokud nový systém prokáže lepší chování
 */
bool runMixerEnergyTest(VoiceManager& voiceManager, Logger& logger);

/**
 * @brief Export srovnávacího audio mezi starým a novým mixerem
 * 
 * Vytvoří dva WAV soubory demonstrující rozdíly v mixování:
 * - mixer_comparison_old_system.wav (původní count-based)
 * - mixer_comparison_new_energy_system.wav (nový energy-based)
 * 
 * @param voiceManager Reference na VoiceManager
 * @param logger Reference na Logger
 * @return true při úspěšném exportu
 */
bool runMixerComparisonExport(VoiceManager& voiceManager, Logger& logger);

/**
 * @brief Stress test nového mixeru s mnoha simultánními hlasy
 * 
 * Testuje výkon a stabilitu energy-based mixeru při vysokém počtu
 * aktivních voices (až 32 simultánních hlasů).
 * 
 * @param voiceManager Reference na VoiceManager
 * @param logger Reference na Logger
 * @return true pokud mixer zvládne zátěž bez problémů
 */
bool runMixerStressTest(VoiceManager& voiceManager, Logger& logger);

/**
 * @brief Helper: Výpočet peak úrovně v audio bufferu
 * @param buffer Audio buffer
 * @param numSamples Počet samples
 * @return Peak úroveň (0.0-1.0+)
 */
float calculatePeakLevel(const float* buffer, int numSamples);

/**
 * @brief Helper: Výpočet RMS úrovně v audio bufferu
 * @param buffer Audio buffer
 * @param numSamples Počet samples
 * @return RMS úroveň
 */
float calculateRMSLevel(const float* buffer, int numSamples);

#endif // TEST_MIXER_H