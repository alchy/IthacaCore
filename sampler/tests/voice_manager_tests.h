#ifndef VOICE_MANAGER_TESTS_H
#define VOICE_MANAGER_TESTS_H

// Forward declarations pro eliminaci circular dependencies
class VoiceManager;
class InstrumentLoader;
class Logger;

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

/**
 * @class VoiceManagerTester
 * @brief Separátní testovací třída pro VoiceManager s composition pattern
 * 
 * DESIGN VÝHODY:
 * - Žádné circular dependencies - používá jen forward declarations
 * - Čistý oddělený kód - testovací logika mimo produkční třídu
 * - Flexibilita - může testovat jakýkoli VoiceManager
 * - Snadná implementace - přímočarý composition pattern
 * 
 * POUŽITÍ COMPOSITION PATTERN:
 * VoiceManagerTester používá VoiceManager místo dědění.
 * Všechne testovací metody volají metody na předané VoiceManager instanci.
 */
class VoiceManagerTester {
public:
    /**
     * @brief Konstruktor VoiceManagerTester
     * @param numVoices Počet voices pro testy (default 128)
     * @param sampleRate Sample rate pro testy (default 44100)
     * @param logger Reference na Logger pro test výstupy
     * 
     * Inicializuje test-specifická data:
     * - Časovač pro měření performance
     * - Počítadlo selhání testů
     * - Test konfigurace
     */
    VoiceManagerTester(int numVoices, int sampleRate, Logger& logger);
    
    /**
     * @brief Alternativní konstruktor s default hodnotami
     * @param logger Reference na Logger pro test výstupy
     */
    VoiceManagerTester(Logger& logger) : VoiceManagerTester(128, 44100, logger) {}

    /**
     * @brief Hlavní metoda pro spuštění všech testů
     * @param voiceManager Reference na VoiceManager k testování
     * @param loader Reference na InstrumentLoader pro inicializaci
     * @return int Počet selhání (0 = úspěch)
     * 
     * Agreguje všechny testy:
     * 1. Inicializuje voices přes VoiceManager.initializeAll()
     * 2. Spustí všechny test metody
     * 3. Loguje výsledky přes sdílený Logger
     * 4. Vrací počet selhání
     */
    int runAllTests(VoiceManager& voiceManager, InstrumentLoader& loader);

    // ===== VELOCITY GAIN SPECIFIC TESTY =====
    
    /**
     * @brief Test velocity gain s různými hodnotami
     * @param voiceManager Reference na VoiceManager
     * @return bool true při úspěchu, false při selhání
     * 
     * Testuje aplikaci MIDI velocity na hlasitost:
     * - Testuje velocity: 1, 32, 64, 96, 127
     * - Analyzuje výstupní hlasitost (peak/RMS)
     * - Ověřuje správnost velocity→gain mapování
     * - Loguje detailní výsledky
     */
    bool runVelocityGainTest(VoiceManager& voiceManager);
    
    /**
     * @brief Test master gain nastavení
     * @param voiceManager Reference na VoiceManager
     * @return bool true při úspěchu, false při selhání
     * 
     * Testuje různé master gain hodnoty: 0.1, 0.3, 0.5, 0.8, 1.0
     * s analýzou jejich dopadu na výstupní hlasitost.
     */
    bool runMasterGainTest(VoiceManager& voiceManager);
    
    /**
     * @brief Enhanced single note test s gain monitoringem
     * @param voiceManager Reference na VoiceManager
     * @return bool true při úspěchu, false při selhání
     * 
     * Kombinuje původní single note test s detailním monitoringem
     * envelope, velocity a master gain během přehrávání.
     */
    bool runSingleNoteTestWithGain(VoiceManager& voiceManager);

    // ===== STANDARDNÍ GRANULAR TESTY =====
    
    /**
     * @brief Test jednotlivé voice inspekce
     * @param voiceManager Reference na VoiceManager
     * @return bool true při úspěchu, false při selhání
     */
    bool runIndividualVoiceTest(VoiceManager& voiceManager);
    
    /**
     * @brief Test základní single note funkcionality
     * @param voiceManager Reference na VoiceManager
     * @return bool true při úspěchu, false při selhání
     */
    bool runSingleNoteTest(VoiceManager& voiceManager);
    
    /**
     * @brief Test polyfonních schopností
     * @param voiceManager Reference na VoiceManager
     * @return bool true při úspěchu, false při selhání
     */
    bool runPolyphonyTest(VoiceManager& voiceManager);
    
    /**
     * @brief Test edge cases (invalid MIDI, null instrument, atd.)
     * @param voiceManager Reference na VoiceManager
     * @return bool true při úspěchu, false při selhání
     */
    bool runEdgeCaseTests(VoiceManager& voiceManager);
    
    /**
     * @brief Export testového vzorku do ./exports/tests/
     * @param voiceManager Reference na VoiceManager
     * @return bool true při úspěchu, false při selhání
     * 
     * Používá existující WavExporter pro export testového výstupu.
     * Vytvoří složku pokud neexistuje, loguje cestu.
     */
    bool exportTestSample(VoiceManager& voiceManager);

    // ===== TESTOVACÍ HELPERY =====
    
    /**
     * @brief Vytvoří dummy instrument pro testy
     * @return Pointer na dummy Instrument (caller owns)
     * 
     * Pouze pro testy - vytvoří minimální instrument s test daty.
     */
    void* createTestInstrument();
    
    /**
     * @brief Simuluje sekvenci note-on/off událostí
     * @param voiceManager Reference na VoiceManager
     * @param midiNote MIDI nota
     * @param velocity Velocity hodnota
     * @param durationMs Délka noty v ms
     */
    void simulateNoteSequence(VoiceManager& voiceManager, uint8_t midiNote, 
                             uint8_t velocity, int durationMs);
    
    /**
     * @brief Ověří gain křivku (envelope aplikace)
     * @param expectedGain Očekávaný gain
     * @param actualGain Skutečný gain
     * @param tolerance Tolerance pro porovnání (default 0.01f)
     * @return bool true pokud je v toleranci
     */
    bool verifyGainCurve(float expectedGain, float actualGain, float tolerance = 0.01f);
    
    /**
     * @brief Ověří audio výstup proti očekávaným hodnotám
     * @param outputBuffer Audio buffer k ověření
     * @param numSamples Počet samples
     * @param expectedPeak Očekávaný peak level
     * @param tolerance Tolerance pro porovnání
     * @return bool true pokud vyhovuje
     */
    bool verifyAudioOutput(const float* outputBuffer, int numSamples, 
                          float expectedPeak, float tolerance = 0.01f);
    
    /**
     * @brief Vytvoří dummy audio buffer pro testy
     * @param numSamples Počet samples
     * @param channels Počet kanálů (1=mono, 2=stereo)
     * @return Pointer na buffer (caller owns, use delete[])
     */
    float* createDummyAudioBuffer(int numSamples, int channels = 2);

    // ===== GETTERY PRO STATISTIKY =====
    
    /**
     * @brief Getter pro počet provedených testů
     * @return Počet testů
     */
    int getTestCount() const { return testCount_; }
    
    /**
     * @brief Getter pro počet selhání
     * @return Počet selhání
     */
    int getFailureCount() const { return failureCount_; }
    
    /**
     * @brief Getter pro celkový čas testování
     * @return Čas v ms
     */
    long getTotalTimeMs() const;

private:
    // ===== PRIVATE MEMBERS =====
    
    Logger* logger_;                    // Reference na Logger (může být null pro tichý režim)
    int numVoices_;                     // Počet voices pro testy
    int sampleRate_;                    // Sample rate pro testy
    int testCount_;                     // Počítadlo provedených testů
    int failureCount_;                  // Počítadlo selhání
    std::chrono::steady_clock::time_point startTime_; // Časovač pro měření

    // ===== PRIVATE HELPER METHODS =====
    
    /**
     * @brief Najde platnou testovací MIDI notu
     * @param voiceManager Reference na VoiceManager
     * @return uint8_t Platná MIDI nota pro testy
     */
    uint8_t findValidTestMidiNote(VoiceManager& voiceManager);
    
    /**
     * @brief Najde platné noty pro polyfonii
     * @param voiceManager Reference na VoiceManager
     * @param maxNotes Maximum počet not
     * @return std::vector<uint8_t> Seznam platných not
     */
    std::vector<uint8_t> findValidNotesForPolyphony(VoiceManager& voiceManager, int maxNotes = 3);
    
    /**
     * @brief Loguje test výsledek
     * @param testName Název testu
     * @param success Úspěch testu
     * @param message Dodatečná zpráva
     */
    void logTestResult(const std::string& testName, bool success, const std::string& message = "");
    
    /**
     * @brief Započne měření času testu
     */
    void startTestTimer();
    
    /**
     * @brief Ukončí měření času testu
     * @param testName Název testu pro log
     */
    void endTestTimer(const std::string& testName);
};

#endif // VOICE_MANAGER_TESTS_H