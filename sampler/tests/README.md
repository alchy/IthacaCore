VoiceManager Test Suite
Přehled
Tento testovací balíček je navržen pro ověření funkčnosti třídy VoiceManager v rámci projektu IthacaCore. Testy jsou refaktorovány z původní monolitické implementace (voice_manager_tests.h/cpp) do modulární struktury, která využívá dědičnost z TestBase a registraci testů přes TestRegistry. Každý test je zaměřen na specifickou funkcionalitu VoiceManager, jako je zpracování MIDI not, gain, envelope, polyfonie nebo okrajové případy.
Struktura
Testy jsou rozděleny do následujících souborů:

Základní třídy:

test_base.h/cpp: Základní třída TestBase poskytuje společné metody pro logování, analýzu audio bufferů a export WAV souborů. Definuje také struktury TestConfig, TestResult a AudioStats.
test_registry.h/cpp: Třída TestRegistry slouží k registraci a spouštění všech testů.


Specifické testy:

velocity_gain_test.h/cpp: Testuje mapování MIDI velocity na hlasitost (velocity gain) pro hodnoty 1, 32, 64, 96, 127.
master_gain_test.h/cpp: Ověřuje nastavení master gainu (hodnoty 0.1, 0.3, 0.5, 0.8, 1.0) a jeho interakci s velocity.
envelope_test.h/cpp: Testuje chování envelope (attack a release fáze) při přehrávání noty.
single_note_test.h/cpp: Testuje základní funkcionalitu přehrávání jedné noty, včetně aktivace a zpracování audio výstupu.
polyphony_test.h/cpp: Ověřuje polyfonní schopnosti VoiceManager při přehrávání více not současně.
edge_case_test.h/cpp: Testuje okrajové případy, jako jsou neplatné MIDI noty, nulová velocity nebo maximální velocity.
performance_test.h/cpp: Měří výkon VoiceManager při zpracování více audio bloků.



Funkcionalita

Logování: Testy využívají Logger pro detailní výstupy o průběhu a výsledcích testů (úspěch/selhání, statistiky).
Export WAV: Pokud je povolen export (config().exportAudio = true), testy ukládají audio výstupy do WAV souborů v adresáři ./exports/tests. Každý test exportuje soubory specifické pro jeho účel (např. velocity_gain_64.wav, envelope_full.wav).
Konfigurace: Testy jsou konfigurovatelné přes TestConfig, který určuje parametry jako exportBlockSize, defaultTestVelocity nebo testMasterGains.

Použití

Kompilace:

Ujistěte se, že jsou zahrnuty závislosti (voice_manager.h, core_logger.h, instrument_loader.h, wav_file_exporter.h).
Soubory testů kompilujte jako součást projektu IthacaCore.


Spuštění testů:

Níže je příklad, jak volat testy z kódu. Tento kód inicializuje VoiceManager, Logger, TestConfig a registruje všechny testy prostřednictvím TestRegistry. Výsledky jsou poté zpracovány a logovány.
#include "test_registry.h"
#include "velocity_gain_test.h"
#include "master_gain_test.h"
#include "envelope_test.h"
#include "single_note_test.h"
#include "polyphony_test.h"
#include "edge_case_test.h"
#include "performance_test.h"
#include "voice_manager.h"
#include "core_logger.h"
#include <iostream>

int main() {
    // Inicializace loggeru a VoiceManager
    Logger logger;
    VoiceManager voiceManager(128, 44100); // Např. 128 voices, 44.1 kHz
    
    // Konfigurace testů
    TestConfig config;
    config.exportAudio = true; // Povolit export WAV souborů
    config.exportBlockSize = 512;
    config.defaultTestVelocity = 100;
    config.testMasterGains = {0.1f, 0.3f, 0.5f, 0.8f, 1.0f};

    // Inicializace TestRegistry
    TestRegistry registry(logger);

    // Registrace jednotlivých testů
    registry.registerTest(std::make_unique<VelocityGainTest>(logger, config));
    registry.registerTest(std::make_unique<MasterGainTest>(logger, config));
    registry.registerTest(std::make_unique<EnvelopeTest>(logger, config));
    registry.registerTest(std::make_unique<SingleNoteTest>(logger, config));
    registry.registerTest(std::make_unique<PolyphonyTest>(logger, config));
    registry.registerTest(std::make_unique<EdgeCaseTest>(logger, config));
    registry.registerTest(std::make_unique<PerformanceTest>(logger, config));

    // Spuštění všech testů
    auto results = registry.runAll(voiceManager, config);

    // Vypsání výsledků
    for (const auto& [testName, result] : results) {
        std::string status = result.passed ? "PASSED" : "FAILED";
        std::cout << testName << ": " << status;
        if (!result.passed) {
            std::cout << " (" << result.errorMessage << ")";
        }
        if (!result.details.empty()) {
            std::cout << " - " << result.details;
        }
        std::cout << std::endl;
    }

    return 0;
}


Vysvětlení příkladu:

Logger a VoiceManager jsou inicializovány s příslušnými parametry (např. počet hlasů a vzorkovací frekvence).
TestConfig je nastaven pro povolení exportu WAV souborů a definici testovacích parametrů.
TestRegistry je použita k registraci všech testů pomocí std::make_unique pro správu paměti.
Metoda runAll spustí všechny testy a vrátí mapu výsledků, které jsou poté vypsány do konzole.




Výstupy:

Logy jsou vypisovány přes Logger (na stderr nebo jiný výstup podle implementace).
WAV soubory jsou uloženy v ./exports/tests, pokud je povolen export.
Výsledky testů obsahují informace o úspěchu/selhání a podrobnosti (např. chybové zprávy).



Požadavky

C++17 nebo vyšší.
Knihovna std::filesystem pro vytváření exportních adresářů.
Závislosti na třídách VoiceManager, Logger a InstrumentLoader z projektu IthacaCore.

Poznámky

Testy jsou navrženy tak, aby byly nezávislé a mohly být spouštěny jednotlivě nebo jako celek přes TestRegistry.
Export WAV souborů je volitelný a lze jej
