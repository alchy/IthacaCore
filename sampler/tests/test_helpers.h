#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <vector>
#include <string>
#include "core_logger.h"

/**
 * @brief Vytvoří export adresář pro testy, pokud neexistuje
 * @param logger Reference na Logger
 * @return Plnou cestu k export adresáři nebo prázdný string při chybě
 */
std::string createTestExportDirectory(Logger& logger);

/**
 * @brief Helper pro export audio do WAV pomocí WavExporter
 * @param filename Název souboru (bez cesty)
 * @param data Audio data (interleaved stereo)
 * @param numFrames Počet stereo párů
 * @param channels Počet kanálů (1 nebo 2)
 * @param sampleRate Sample rate v Hz
 * @param logger Reference na Logger
 */
void exportTestAudio(const std::string& filename, const float* data, int numFrames, int channels, int sampleRate, Logger& logger);

/**
 * @brief Výpočet počtu bloků pro danou dobu v sekundách
 * @param durationSec Doba v sekundách
 * @param sampleRate Sample rate v Hz
 * @param blockSize Velikost bloku
 * @return Počet bloků
 */
int calculateBlocksForDuration(double durationSec, int sampleRate, int blockSize);

/**
 * @brief Analýza attack fáze (zvýšení gainů)
 * @param envelopeGains Vektor envelope gainů
 * @param attackBlocks Počet bloků attack fáze
 * @param logger Reference na Logger
 * @return true pokud attack fáze je v pořádku
 */
bool analyzeAttackPhase(const std::vector<float>& envelopeGains, int attackBlocks, Logger& logger);

/**
 * @brief Analýza sustain fáze (stabilita gainů)
 * @param envelopeGains Vektor envelope gainů
 * @param attackBlocks Počet bloků attack fáze
 * @param sustainBlocks Počet bloků sustain fáze
 * @param logger Reference na Logger
 * @return true pokud sustain fáze je v pořádku
 */
bool analyzeSustainPhase(const std::vector<float>& envelopeGains, int attackBlocks, int sustainBlocks, Logger& logger);

/**
 * @brief Analýza release fáze (snížení gainů)
 * @param envelopeGains Vektor envelope gainů
 * @param releaseStart Index začátku release fáze
 * @param releaseBlocks Počet bloků release fáze
 * @param logger Reference na Logger
 * @return true pokud release fáze je v pořádku
 */
bool analyzeReleasePhase(const std::vector<float>& envelopeGains, int releaseStart, int releaseBlocks, Logger& logger);

#endif // TEST_HELPERS_H