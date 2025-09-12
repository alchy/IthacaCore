# IthacaCore: C++ Sampler s Loggerem a InstrumentLoader

## Popis projektu
IthacaCore je profesionální audio sampler engine napsaný v C++17. Systém je modulární, s podporou logování (`Logger`), prohledávání WAV sample souborů (`SamplerIO`), načítání do paměti jako stereo float buffery (`InstrumentLoader`) a koordinaci celého workflow (`runSampler`). **NOVÉ: Rozšířena podpora pro voice management (`Voice` a `VoiceManager`) pro polyfonní přehrávání s ADSR obálkou a stavy (idle, attacking, sustaining, releasing).** Navržen pro snadnou integraci do větších projektů, jako jsou audio pluginy nebo standalone aplikace. Používá `libsndfile` pro práci s WAV soubory a podporuje parsování názvů souborů ve formátu `mXXX-velY-fZZ.wav` (kde XXX je MIDI nota 0-127, Y je velocity 0-7, ZZ je zkrácená frekvence s 'f' prefixem).

**Klíčové vlastnosti:**
- Thread-safe logování do souboru
- Prohledávání metadat (frekvence, MIDI nota, velocity, délka, počet kanálů) z WAV souborů
- Automatické načítání všech samples do paměti jako stereo interleaved float buffery
- Mono→Stereo konverze s duplikací dat (L=R) pro jednotný formát
- JUCE-ready buffer layout `[L1,R1,L2,R2...]` pro optimální audio processing
- Detekce audio formátu (interleaved/non-interleaved) a potřeby konverze do float
- Analýza subformátu (16-bit PCM, 24-bit PCM, 32-bit float, atd.)
- Validace konzistence frekvence mezi názvem souboru a obsahem
- Vyhledávání sample podle MIDI noty a velocity
- Přístup k metadatům přes bezpečné gettery (s kontrolou indexu)
- Automatická validace stereo konzistence všech načtených bufferů
- Simulace JUCE AudioBuffer pro stereo výstup bez závislosti na JUCE
- **NOVÉ: Polyfonní voice management s ADSR obálkou (attack, decay, sustain, release)**

## Požadavky
- Visual Studio Code
- Visual Studio 2022 Build Tools (s C++ komponentami) nebo Community Edition
- CMake (verze 3.10+, přidejte do PATH)
- Rozšíření VS Code: CMake Tools a C/C++ od Microsoftu
- `libsndfile` (stáhne se automaticky přes `add_subdirectory` v CMakeLists.txt)
- C++17 kompatibilní kompilátor (MSVC/GCC/Clang)

## Postup nastavení
1. Uložte všechny soubory do kořenové složky projektu (např. `IthacaCore`)
2. Vytvořte složku `.vscode` a uložte do ní `tasks.json`, `launch.json` a `settings.json`
3. Vytvořte složku `libsndfile` a stáhněte do ní zdrojový kód `libsndfile` (nebo použijte externí build)
4. Otevřete projekt v VS Code (File → Open Folder)
5. Spusťte CMake: Configure (Ctrl+Shift+P → CMake: Configure). To vygeneruje build soubory v `build` složce

## Sestavení a spuštění
- **Sestavení**: Spusťte úlohu "build" (Ctrl+Shift+P → Tasks: Run Task → build). To nastaví prostředí MSVC přes `vcvars64.bat` a spustí `cmake --build .`. Vytvoří `Debug/IthacaCore.exe`
- **Spuštění**: Klikněte na tlačítko spustit (|>) nebo stiskněte F5. Program se sestaví a spustí
- **Výstup**:
  - Na konzoli: Úvodní zpráva "[i] Starting IthacaCore" a případně parametry
  - V logu (`core_logger/core_logger.log`): Záznamy o inicializaci loggeru, prohledávání sample, operacích `InstrumentLoader` (načítání do paměti, mono→stereo konverze, validace), operacích `Voice`/`VoiceManager` (inicializace voice, `setNoteState`, `processBlock`, ADSR obálka), vyhledávání (např. "Loaded: m108-vel7-f44.wav (MIDI: 108, Vel: 7, Freq: 44100 Hz, Duration: 2.5s, Channels: 2 (stereo), Frames: 110250, Format: interleaved, needs float conversion)")
- **Čištění**: Smažte složku `build` a `core_logger` pro reset

**Poznámka**: Cesta k `vcvars64.bat` v `tasks.json` je pro VS 2022 Build Tools. Pokud máte Community, upravte na `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`. Pro PowerShell execution policy: `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser`.

## Použití v vlastním projektu
Pro integraci do vašeho C++ projektu zahrňte hlavičky a linkujte `libsndfile`. Příklad minimálního použití (v `main.cpp`):

```cpp
#include "core_logger.h"
#include "sampler.h"
#include "instrument_loader.h"
#include "voice.h"
#include "voice_manager.h"
#include "envelopes/envelope.h"

int main() {
    Logger logger("./"); // Cesta k logům
    int result = runSampler(logger); // Spustí celý workflow včetně InstrumentLoader a VoiceManager
    return result;
}
```

Pro vlastní logiku použijte třídu `SamplerIO` (pro IO operace), `InstrumentLoader` (pro načítání do paměti) a **třídy `Voice` a `VoiceManager`** (pro polyfonní přehrávání s ADSR obálkou). Vždy inicializujte `Logger` pro logování.

### Použití třídy SamplerIO
`SamplerIO` pro čisté IO operace (prohledávání, vyhledávání, přístup k metadatům). Vytvořte instanci a předejte logger.

| Metoda | Parametry | Příklad | Komentář | Návratový typ | Příklad návratu / Chyba |
|--------|-----------|---------|----------|---------------|-------------------------|
| `SamplerIO()` | - | `SamplerIO io;` | Konstruktor: Inicializuje prázdný seznam `sampleList`. Žádné logování v konstruktoru. | `void` (konstruktor) | - (žádný návratový kód, úspěšná inicializace) |
| `~SamplerIO()` | - | Automatický | Destruktor: Žádné speciální akce (seznam se uvolní automaticky). | `void` (destruktor) | - (automatické uvolnění) |
| `scanSampleDirectory(const std::string& directoryPath, Logger& logger)` | `directoryPath` – cesta k adresáři se WAV soubory (např. `"./samples"`), `logger` – reference na Logger | `io.scanSampleDirectory(R"(c:\samples)", logger);` | Prohledá adresář. Deleguje parsování názvů (regex), načte metadata (libsndfile). Validuje konzistenci frekvence mezi názvem a souborem. Detekuje formát (interleaved) a potřebu konverze (PCM→float). Loguje info/warn/error. Chyby (neexistující adresář, nekonzistentní frekvence): zaloguje a `std::exit(1)`. | `void` | - (úspěch: načte data; chyba: exit(1) po logu) |
| `findSampleInSampleList(uint8_t midi_note, uint8_t velocity, int sampleRate) const` | `midi_note` (0-127), `velocity` (0-7), `sampleRate` (frekvence v Hz) | `int idx = io.findSampleInSampleList(60, 5, 44100);` | Vyhledá index sample v interním seznamu. Vrátí -1, pokud nenalezeno. Lineární prohledávání, žádné logování. | `int` | `0` (index první shody); `-1` (nenalezeno, není chyba) |
| `getLoadedSampleList() const` | - | `const auto& list = io.getLoadedSampleList();` | Vrátí konstantní referenci na vektor `SampleInfo` pro čtení metadat. Žádné logování. | `const std::vector<SampleInfo>&` | Reference na vektor (prázdný, pokud není načteno) |

### Použití třídy InstrumentLoader
`InstrumentLoader` pro načítání WAV samples do paměti jako 32-bit stereo float buffery optimalizované pro JUCE. **Klíčové vlastnosti:**
- **Povinná stereo konverze**: Všechny samples (i původně mono) jsou uloženy jako stereo `[L1,R1,L2,R2...]`
- **Mono→Stereo duplikace**: Mono samples se automaticky duplikují do obou kanálů (L=R)
- **JUCE-ready formát**: Buffer layout optimalizovaný pro JUCE AudioBuffer de-interleaving
- **Automatická validace**: Kontrola konzistence všech načtených bufferů
- **Filtrování frekvence**: Načítá pouze samples s požadovanou frekvencí vzorkování

| Metoda | Parametry | Příklad | Komentář | Návratový typ |
|--------|-----------|---------|----------|---------------|
| `InstrumentLoader(SamplerIO& sampler, int targetSampleRate, Logger& logger)` | `sampler` – reference na SamplerIO, `targetSampleRate` – požadovaná frekvence (např. 44100), `logger` – reference na Logger | `InstrumentLoader loader(sampler, 44100, logger);` | Konstruktor: Inicializuje pole pro všechny MIDI noty 0-127. Loguje inicializaci s targetSampleRate. | `void` (konstruktor) |
| `loadInstrument()` | - | `loader.loadInstrument();` | Načte všechny dostupné samples do paměti jako stereo float buffery. Prochází všechny MIDI noty 0-127 a velocity 0-7. Provádí mono→stereo konverzi. Validuje konzistenci po načtení. Loguje progress a statistiky. | `void` |
| `getInstrumentNote(uint8_t midi_note)` | `midi_note` (0-127) | `Instrument& inst = loader.getInstrumentNote(108);` | Vrátí referenci na Instrument strukturu pro danou MIDI notu. Kontroluje platnost rozsahu. Při chybě: log error a exit(1). | `Instrument&` |
| `getTotalLoadedSamples()` | - | `int total = loader.getTotalLoadedSamples();` | Getter pro celkový počet úspěšně načtených samples. | `int` |
| `getMonoSamplesCount()` | - | `int mono = loader.getMonoSamplesCount();` | Getter pro počet původně mono samples (před konverzí na stereo). | `int` |
| `getStereoSamplesCount()` | - | `int stereo = loader.getStereoSamplesCount();` | Getter pro počet původně stereo samples. | `int` |
| `getTargetSampleRate()` | - | `int rate = loader.getTargetSampleRate();` | Getter pro nastavenou target sample rate v Hz. | `int` |

#### Struktura `Instrument`
```cpp
struct Instrument {
    SampleInfo* sample_ptr_sampleInfo[8];    // pointery na SampleInfo
    float* sample_ptr_velocity[8];           // pointery na stereo float buffery
    bool velocityExists[8];                  // indikátory existence
    sf_count_t frame_count_stereo[8];        // počet stereo frame párů
    sf_count_t total_samples_stereo[8];      // celkový počet float hodnot (frame_count * 2)
    bool was_originally_mono[8];             // původní formát před konverzí
    
    float* get_sample_begin_pointer(uint8_t velocity);    // pointer na stereo data [L,R,L,R...]
    sf_count_t get_frame_count(uint8_t velocity);         // počet stereo frame párů
    sf_count_t get_total_sample_count(uint8_t velocity);  // celkový počet float hodnot
    bool get_was_originally_mono(uint8_t velocity);       // původní formát info
};
```

#### Příklad použití InstrumentLoader
```cpp
Logger logger("./");
SamplerIO sampler;
std::string dir = R"(c:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)";

// 1. Prohledání adresáře
sampler.scanSampleDirectory(dir, logger);

// 2. Načtení všech samples do paměti jako stereo buffery
InstrumentLoader loader(sampler, 44100, logger);
loader.loadInstrument();  // Automatická mono→stereo konverze a validace

// 3. Přístup k načteným stereo datům
Instrument& inst = loader.getInstrumentNote(108);
if (inst.velocityExists[7]) {
    float* stereoData = inst.get_sample_begin_pointer(7);      // [L1,R1,L2,R2...]
    sf_count_t frames = inst.get_frame_count(7);               // počet stereo párů
    sf_count_t totalSamples = inst.get_total_sample_count(7);  // celkový počet float hodnot
    bool wasMono = inst.get_was_originally_mono(7);            // původní formát
}
```

### Použití tříd Voice a VoiceManager
Třídy `Voice` a `VoiceManager` spravují polyfonní přehrávání s ADSR obálkou (attack, decay, sustain, release). `Voice` reprezentuje jednu hlasovou jednotku, která zpracovává sample s obálkou a stavy (idle, attacking, sustaining, releasing). `VoiceManager` spravuje pool hlasů (např. 128 hlasů) a koordinuje jejich přehrávání.

#### Klíčové vlastnosti
- **Polyfonie**: `VoiceManager` přiřazuje MIDI noty k volným hlasům.
- **ADSR obálka**: Implementována v třídě `Envelope`, použita v `Voice` pro generování gainů (attack a release).
- **Stavy**: Enum `VoiceState` (Idle, Attacking, Sustaining, Releasing) řídí lifecycle hlasu.
- **AudioBuffer**: Simulace JUCE bufferu pro stereo výstup [left, right].
- **AudioData**: Struktura pro jeden stereo sample.

#### Třída `Voice`
`Voice` spravuje jednu MIDI notu s přiřazeným instrumentem, aplikuje ADSR obálku a generuje stereo výstup.

| Metoda | Vstupní parametry | Výstup | Popis |
|--------|-------------------|--------|-------|
| `Voice()` | - | `void` (konstruktor) | Default konstruktor: Inicializuje hlas v idle stavu pro použití v poolu `VoiceManager`. |
| `Voice(uint8_t midiNote, Logger& logger, Envelope& envelope)` | `midiNote` (0-127), `logger` (reference na Logger), `envelope` (reference na Envelope) | `void` (konstruktor) | Konstruktor: Inicializuje hlas s MIDI notou a referencí na `Envelope`. Instrument se nastavuje později přes `initialize`. |
| `Voice(uint8_t midiNote, const Instrument& instrument, Logger& logger, Envelope& envelope)` | `midiNote` (0-127), `instrument` (reference na Instrument), `logger`, `envelope` | `void` (konstruktor) | Plný konstruktor: Inicializuje hlas s MIDI notou, instrumentem a obálkou. |
| `initialize(const Instrument& instrument, int sampleRate)` | `instrument` (reference na Instrument), `sampleRate` (frekvence v Hz) | `void` | Inicializuje hlas s instrumentem a nastaví vzorkovací frekvenci pro `Envelope`. |
| `cleanup()` | - | `void` | Resetuje hlas do idle stavu, uvolní zdroje. |
| `reinitialize(const Instrument& instrument, int sampleRate)` | `instrument`, `sampleRate` | `void` | Reinicializuje hlas s novým instrumentem a vzorkovací frekvencí. |
| `setNoteState(bool isOn, uint8_t velocity)` | `isOn` (true pro note-on, false pro note-off), `velocity` (0-127) | `void` | Nastaví stav hlasu: note-on přepne do Attacking/Sustaining, note-off do Releasing. Mapuje velocity na layer (0-7). |
| `advancePosition()` | - | `void` | Posune pozici o 1 frame (používáno pro non-real-time zpracování). |
| `getCurrentAudioData() const` | - | `AudioData` | Vrátí aktuální stereo sample (left/right) s aplikovanou obálkou. |
| `processBlock(AudioBuffer& outputBuffer, int numSamples)` | `outputBuffer` (reference na simulovaný AudioBuffer), `numSamples` (počet vzorků) | `bool` | Zpracuje blok vzorků, aplikuje ADSR obálku a zapisuje do stereo bufferu. Vrátí true, pokud je hlas aktivní. |
| `getMidiNote() const` | - | `uint8_t` | Vrátí MIDI notu přiřazenou hlasu. |
| `isActive() const` | - | `bool` | Vrátí true, pokud je hlas aktivní (není Idle). |
| `getState() const` | - | `VoiceState` | Vrátí aktuální stav hlasu (Idle, Attacking, Sustaining, Releasing). |
| `getPosition() const` | - | `sf_count_t` | Vrátí aktuální pozici v samplech. |
| `getCurrentEnvelopeGain() const` | - | `float` | Vrátí aktuální hodnotu obálky (0.0-1.0). |
| `getVelocityGain() const` | - | `float` | Vrátí gain odvozený z velocity (0.0-1.0). |
| `getMasterGain() const` | - | `float` | Vrátí master gain hlasu. |
| `getFinalGain() const` | - | `float` | Vrátí kombinovaný gain (velocity * envelope * master). |
| `setMasterGain(float gain, Logger& logger)` | `gain` (0.0-1.0), `logger` | `void` | Nastaví master gain hlasu s logováním. |

#### Třída `VoiceManager`
`VoiceManager` spravuje pool hlasů (např. 128) a koordinuje polyfonní přehrávání s ADSR obálkou.

| Metoda | Vstupní parametry | Výstup | Popis |
|--------|-------------------|--------|-------|
| `VoiceManager(int maxVoices, Logger& logger)` | `maxVoices` (počet hlasů, např. 128), `logger` (reference na Logger) | `void` (konstruktor) | Inicializuje pool hlasů a nastaví logger. |
| `initializeAll(InstrumentLoader& loader, int sampleRate)` | `loader` (reference na InstrumentLoader), `sampleRate` (frekvence v Hz) | `void` | Inicializuje všechny hlasy s příslušnými instrumenty a nastaví vzorkovací frekvenci pro obálky. |
| `setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity)` | `midiNote` (0-127), `isOn` (true/false), `velocity` (0-127) | `void` | Nastaví stav hlasu pro danou MIDI notu (note-on/note-off). |
| `processBlock(AudioBuffer& outputBuffer, int numSamples)` | `outputBuffer` (reference na simulovaný AudioBuffer), `numSamples` (počet vzorků) | `bool` | Zpracuje blok pro všechny aktivní hlasy, mixuje jejich výstup do stereo bufferu. Vrátí true, pokud je alespoň jeden hlas aktivní. |
| `getVoice(uint8_t midiNote)` | `midiNote` (0-127) | `Voice&` | Vrátí referenci na hlas přiřazený k MIDI notě. |
| `getActiveVoicesCount() const` | - | `int` | Vrátí počet aktuálně aktivních hlasů. |

#### Příklad použití Voice a VoiceManager
```cpp
#include "core_logger.h"
#include "instrument_loader.h"
#include "voice.h"
#include "voice_manager.h"
#include "envelopes/envelope.h"

int main() {
    Logger logger("./");
    SamplerIO sampler;
    std::string dir = R"(c:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)";

    // 1. Prohledání adresáře
    sampler.scanSampleDirectory(dir, logger);

    // 2. Načtení samples do paměti
    InstrumentLoader loader(sampler, 44100, logger);
    loader.loadInstrument();

    // 3. Inicializace VoiceManager a Envelope
    Envelope envelope(500.0f, 300.0f, 0.7f, 1000.0f); // ADSR: 500ms attack, 300ms decay, 0.7 sustain, 1000ms release
    envelope.setEnvelopeFrequency(44100, logger);
    VoiceManager manager(128, logger);
    manager.initializeAll(loader, 44100);

    // 4. Spuštění noty (MIDI 60, velocity 100)
    manager.setNoteState(60, true, 100);

    // 5. Zpracování audio bloku
    AudioBuffer output(512); // Simulovaný stereo buffer
    if (manager.processBlock(output, 512)) {
        // Výstup je v output.leftChannel a output.rightChannel
        // Pro JUCE: převeďte do AudioBuffer<float>
    }

    // 6. Note-off
    manager.setNoteState(60, false, 0);

    // 7. Kontrola stavu hlasu
    Voice& voice = manager.getVoice(60);
    logger.log("demo", "info", "Voice state: " + std::to_string(static_cast<int>(voice.getState())) +
               ", Final gain: " + std::to_string(voice.getFinalGain()));
}
```

### Funkce runSampler
- **Signatura**: `int runSampler(Logger& logger)`
- **Příklad**: `runSampler(logger);`
- **Komentář**: Spustí kompletní workflow včetně `InstrumentLoader`, `VoiceManager` a `Envelope`. Deleguje prohledávání do `SamplerIO`, inicializuje `InstrumentLoader`, načte všechny samples do paměti jako stereo buffery, inicializuje `VoiceManager` a `Envelope`, nastaví testovací notu (např. MIDI 108/vel 7), procesuje blok s ADSR obálkou a loguje stavy. Testuje API metody `get_sample_begin_pointer()`, `get_frame_count()` a `processBlock`. Loguje všechny kroky včetně detailních informací o stereo konverzi a JUCE-ready formátu. Vrátí 0 při úspěchu (chyby končí `std::exit(1)`).

## Struktura projektu
### Klíčové soubory
- **CMakeLists.txt**: Definuje projekt, přidává `libsndfile`, linkuje soubory (`main.cpp`, `sampler/*.cpp`, `envelopes/*.cpp`) a include cesty. Obsahuje `instrument_loader.h/cpp`, `voice.h/cpp`, `voice_manager.h/cpp`, `envelope.h/cpp`.
- **main.cpp**: Hlavní vstup – inicializuje logger, zpracuje argumenty a volá `runSampler`.
- **sampler/core_logger.h/cpp**: Implementace třídy `Logger`.
- **sampler/sampler.h/cpp**: Deklarace a implementace funkce `runSampler` a třídy `SamplerIO`.
- **sampler/sampler_io.cpp**: Implementace `SamplerIO` metod.
- **sampler/instrument_loader.h/cpp**: Implementace třídy `InstrumentLoader` pro načítání do paměti.
- **sampler/voice.h/cpp**: Implementace třídy `Voice` pro hlasovou jednotku s ADSR obálkou.
- **sampler/voice_manager.h/cpp**: Implementace třídy `VoiceManager` pro polyfonní management.
- **sampler/envelopes/envelope.h/cpp**: Implementace třídy `Envelope` pro ADSR obálku.
- **sampler/wav_file_exporter.h/cpp**: Export WAV pro testování.
- **.vscode/**: Konfigurace pro VS Code (build tasky, launch, settings pro terminal a file associations).
- **README.md**: Tento soubor.

### Struktury a třídy
#### Struktura `SampleInfo` (v `sampler.h`)
Uchovává metadata o WAV samplu – beze změn.

#### Struktura `Instrument` (v `instrument_loader.h`)
Reprezentuje jeden MIDI note s velocity vrstvami a stereo metadaty (viz výše).

#### Třída `InstrumentLoader` (v `instrument_loader.h/cpp`)
Centralizuje načítání WAV samples do paměti s klíčovými vlastnostmi (viz výše).

#### Třídy `Voice` a `VoiceManager` (v `voice.h/cpp` a `voice_manager.h/cpp`)
- `Voice`: Spravuje jednu notu s ADSR obálkou, stavy a přístupem k stereo bufferům. Podporuje metody pro inicializaci, `setNoteState` a `processBlock`.
- `VoiceManager`: Pool hlasů pro polyfonii, inicializuje hlasy, nastavuje stavy a procesuje bloky.
- **Simulace JUCE**: `AudioBuffer` pro výstup, `AudioData` pro sample data.

#### Třída `Envelope` (v `envelopes/envelope.h/cpp`)
Implementuje RT-safe ADSR obálku s fixními parametry pro attack, decay, sustain a release.

#### Třída `Logger` (v `core_logger.h/cpp`)
Zůstává stejná jako před aktualizací.

## Řešení problémů
- **Execution Policy**: Spusťte `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser` v PowerShell.
- **libsndfile chybí**: Stáhněte z [libsndfile GitHub](https://github.com/libsndfile/libsndfile) a upravte CMakeLists.txt.
- **Linkovací chyba LNK1104**: Ukončete všechny procesy IthacaCore.exe, smažte build složku a sestavte znovu.
- **Logy**: Sledujte `core_logger/core_logger.log`. Pro real-time: `Get-Content -Path "core_logger/core_logger.log" -Tail 10 -Wait`.
- **Rozšíření**: Upravte cestu v `sampler.cpp` pro jiné samples. Pro práci s načtenými buffery použijte `InstrumentLoader` API; pro přehrávání `VoiceManager`.
- **Chyba - nekonzistence frekvence**: Program se ukončí s chybou, pokud frekvence v názvu souboru neodpovídá frekvenci v WAV headeru.
- **Nepodporovaný subformát**: Program končí s chybou při detekci nepodporovaného audio formátu.
- **Validace stereo**: Program končí s chybou při selhání validace stereo konzistence v `InstrumentLoader`.
- **Chyba v Voice**: Pokud instrument není inicializován, `processBlock` vrátí false (log warning).
- **Chyba v Envelope**: Pokud není nastavena vzorkovací frekvence, metody `getAttackGains`/`getReleaseGains` vrací false a vyplní buffer nulami.

## Poznámky
- **Názvosloví souborů**: Samples musí mít formát `mXXX-velY-fZZ.wav` (XXX = MIDI nota 0-127, Y = velocity 0-7, ZZ = zkrácená frekvence s 'f' prefixem: f8, f11, f16, f22, f44, f48, f88, f96, f176, f192). Jinak se ignorují s warningem.
- **Validace**: Frekvence v názvu (fZZ) musí odpovídat frekvenci v WAV headeru, jinak program končí s chybou.
- **Stereo formát**: Všechny buffery jsou uloženy jako stereo interleaved `[L,R,L,R...]` bez ohledu na původní formát.
- **Mono konverze**: Mono samples se automaticky duplikují do stereo formátu (L=R) pro jednotnost.
- **JUCE optimalizace**: Buffer layout je optimalizovaný pro JUCE AudioBuffer de-interleaving.
- **Detekce formátu**: Program analyzuje, zda je WAV interleaved (standard) a jaký má subformát (16-bit PCM, float, atd.).
- **Podporované formáty**: 16-bit PCM, 24-bit PCM, 32-bit PCM, 32-bit float, 64-bit double.
- **Optimální formát**: 32-bit float (žádná konverze potřebná).
- **Paměťové nároky**: Vyšší díky uložení všech samples jako stereo float buffery.
- **Chyby**: Selhání inicializace (adresář, přístup, neplatný index, nekonzistentní frekvence, nepodporovaný formát, validace stereo, neinicializovaný instrument v `Voice`, neplatná frekvence v `Envelope`) vede k logu a `std::exit(1)`.
- **Thread-safety**: Pouze logování je chráněno mutexem; sampler není navržen pro multi-threading.
- **Rozšířená metadata**: Program poskytuje přístup k délce, počtu kanálů, stereo detekci, počtu vzorků, formátu, potřebě konverze a stereo metadatům.
- **Validace integrity**: Automatická validace konzistence všech načtených stereo bufferů.
- **Voice stavy**: Použijte `isActive()` pro kontrolu, `processBlock` pro renderování.
- **Projekt je přenositelný**: Pro složitější aplikace přidejte JUCE nebo další knihovny.

## JUCE Integrace
`InstrumentLoader` a `VoiceManager` poskytují perfektní integraci s JUCE AudioBuffer. `Voice` procesuje do simulovaného AudioBuffer, který lze snadno převést do reálného JUCE bufferu.

```cpp
// Získání stereo dat z InstrumentLoader
Instrument& inst = loader.getInstrumentNote(midiNote);
if (inst.velocityExists[velocity]) {
    float* stereoData = inst.get_sample_begin_pointer(velocity);  // [L,R,L,R...]
    sf_count_t frameCount = inst.get_frame_count(velocity);
    
    // De-interleaving pro JUCE AudioBuffer
    AudioBuffer<float> juceBuffer(2, frameCount);
    float* leftChannel = juceBuffer.getWritePointer(0);
    float* rightChannel = juceBuffer.getWritePointer(1);
    
    for (int frame = 0; frame < frameCount; frame++) {
        leftChannel[frame] = stereoData[frame * 2];         // L kanál
        rightChannel[frame] = stereoData[frame * 2 + 1];    // R kanál
    }
}

// Použití s Voice pro ADSR obálku
Envelope envelope(500.0f, 300.0f, 0.7f, 1000.0f);
envelope.setEnvelopeFrequency(44100, logger);
Voice voice(midiNote, inst, logger, envelope);
voice.setNoteState(true, velocity);
AudioBuffer simBuf(512);  // Simulace pro test
voice.processBlock(simBuf, 512);  // Aplikován ADSR gain, výstup v simBuf
// Převeď simBuf do JUCE bufferu
```

---

# Vysvětlení frames, samples a bytes v audio kontextu

V audio zpracování (např. v JUCE nebo libsndfile) se tyto termíny používají pro popis velikosti a struktury audio dat. Vysvětlení v kontextu stereo interleaved formát [L,R,L,R...]:

- **Frames**: Časová jednotka audio signálu, která obsahuje data pro **všechny kanály v jednom okamžiku**. Pro mono = 1 frame = 1 sample. Pro stereo (2 kanály) = 1 frame = 2 samples (levý + pravý). V logu: **529200 frames** znamená délku zvuku v časových bodech (např. při 44.1 kHz sample rate = ~12 sekund).

- **Samples**: Nejmenší jednotka daty – jedna hodnota pro jeden kanál v jednom časovém bodě. V multi-kanálovém audio (stereo) se "total samples" sčítá přes všechny kanály. V logu: **1058400 total samples** = 2 kanály × 529200 frames (každý frame má 2 samples).

- **Bytes**: Celková velikost dat v paměti (v bytech). Závisí na bitové hloubce (např. float = 4 bytes na sample). V logu: **4233600 bytes** = 1058400 samples × 4 bytes (pravděpodobně 32-bit float). To je pro interleaved formát, kde data jsou propletená (L,R,L,R...), takže buffer zabere tolik místa.

---

# Helper commands

## Grab-Files
```
PowerShell.exe -ExecutionPolicy Bypass -File .\Grab-Files.ps1
```

## Watch log
```
Get-Content -Path "./core_logger/core_logger.log" -Tail 10 -Wait
```

## Git submodule init
```
git submodule update --init --recursive
```

## Pridani submodulu rucne
```
git submodule add https://github.com/libsndfile/libsndfile libsndfile
```

## Grep z logu

```
# Sleduje log soubor v reálném čase a filtruje řádky obsahující "VoiceManagerTester"
Get-Content -Path "C:\Users\jindr\IthacaCore\build\Debug\Debug\core_logger\core_logger.log" -Tail 10 -Wait | 
Where-Object { $_ -match "VoiceManagerTester" }
```

```
Get-Content -Path "C:\Users\nemej992\Documents\Repos\IthacaCore\build\Debug\Debug\core_logger\core_logger.log" -Tail 10 -Wait | Where-Object { $_ -like "*Test/*" }
```

```
Get-Content -Path "C:\Users\nemej992\Documents\Repos\IthacaCore\build\Debug\Debug\core_logger\core_logger.log" -Tail 10 -Wait | Where-Object { $_ -like "*Envelope/*" }
```

