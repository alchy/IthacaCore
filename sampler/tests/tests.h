#ifndef TESTS_H
#define TESTS_H

#include "../core_logger.h"

// Forward declaration
class VoiceManager;

/**
 * @brief Základní ověření funkčnosti systému - wrapper pro všechny testy
 * @param voiceManager Reference na VoiceManager
 * @param logger Reference na Logger
 * @return true při úspěchu všech testů
 */
bool verifyBasicFunctionality(VoiceManager& voiceManager, Logger& logger);

/**
 * @brief Jednoduchý test note-on/off bez envelope analýzy
 * @param voiceManager Reference na VoiceManager
 * @param logger Reference na Logger
 * @return true při úspěchu
 */
bool runSimpleNoteTest(VoiceManager& voiceManager, Logger& logger);

/**
 * @brief Komplexní test envelope s exportem WAV a analýzou fází
 * @param voiceManager Reference na VoiceManager
 * @param logger Reference na Logger
 * @return true při úspěchu
 */
bool runEnvelopeTest(VoiceManager& voiceManager, Logger& logger);

#endif // TESTS_H
