# IthacaCore: C++ Sampler s Loggerem

## Popis projektu
IthacaCore je profesionální audio sampler engine napsaný v C++17. Systém je modulární, s podporou logování (Logger), načítání WAV sample souborů (SamplerIO) a koordinaci celého workflow (runSampler). Navržen pro snadnou integraci do větších projektů, jako je audio plugin nebo standalone aplikace. Používá libsndfile pro práci s WAV soubory a podporuje parsování názvů souborů ve formátu `mXXX-velY-fZZ.wav` (kde XXX je MIDI nota 0-127, Y je velocity 0-7, ZZ je zkrácená frekvence s 'f' prefixem).

Klíčové vlastnosti:
- Thread-safe logování do souboru.
- Načítání metadat (frekvence, MIDI nota, velocity, délka, počet kanálů) z WAV souborů.
- **NOVÉ**: Detekce audio formátu (interleaved/non-interleaved) a potřeby konverze do float.
- **NOVÉ**: Analýza subformátu (16-bit PCM, 24-bit PCM, 32-bit float, atd.).
- Validace konzistence frekvence mezi názvem souboru a obsahem.
- Vyhledávání sample podle MIDI noty a velocity.
- Přístup k metadatům přes bezpečné gettery (s kontrolou indexu).
- Žádné konzolové výstupy v modulech – vše logováno.
- Detekce mono/stereo formátu a další audio metadata.

## Požadavky
- Visual Studio Code
- Visual Studio 2022 Build Tools (s C++ komponentami) nebo Community Edition
- CMake (verze 3.10+, přidejte do PATH)
- Rozšíření VS Code: CMake Tools a C/C++ od Microsoftu
- libsndfile (stáhne se automaticky přes `add_subdirectory` v CMakeLists.txt)
- C++17 kompatibilní kompilátor (MSVC/GCC/Clang)

## Postup nastavení
1. Uložte všechny soubory do kořenové složky projektu (např. `IthacaCore`).
2. Vytvořte složku `.vscode` a uložte do ní `tasks.json`, `launch.json` a `settings.json`.
3. Vytvořte složku `libsndfile` a stáhněte do ní zdrojový kód libsndfile (nebo použijte externí build).
4. Otevřete projekt v VS Code (File → Open Folder).
5. Spusťte CMake: Configure (Ctrl+Shift+P → CMake: Configure). To vygeneruje build soubory v `build` složce.

## Sestavení a spuštění
- **Sestavení**: Spusťte úlohu "build" (Ctrl+Shift+P → Tasks: Run Task → build). To nastaví prostředí MSVC přes `vcvars64.bat` a spustí `cmake --build .`. Vytvoří `Debug/IthacaCore.exe`.
- **Spuštění**: Klikněte na tlačítko spustit (|> ) nebo stiskněte F5. Program se sestaví a spustí.
- **Výstup**:
  - Na konzoli: Úvodní zpráva "[i] Starting IthacaCore" a případně parametry.
  - V logu (`core_logger/core_logger.log`): Záznamy o inicializaci loggeru, načítání sample, vyhledávání (např. "Loaded: m108-vel7-f44.wav (MIDI: 108, Vel: 7, Freq: 44100 Hz, Duration: 2.5s, Channels: 2 (stereo), Frames: 110250, Format: interleaved, needs float conversion)").
- **Čištění**: Smažte složku `build` a `core_logger` pro reset.

**Poznámka**: Cesta k `vcvars64.bat` v tasks.json je pro VS 2022 Build Tools. Pokud máte Community, upravte na `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`. Pro PowerShell execution policy: `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser`.

## Použití v vlastním projektu
Pro integraci do vašeho C++ projektu zahrňte hlavičky a linkujte libsndfile. Příklad minimálního použití (v main.cpp):
```cpp
#include "core_logger.h"
#include "sampler.h"
int main() {
    Logger logger("./"); // Cesta k logům
    int result = runSampler(logger); // Spustí celý workflow
    return result;
}
```
Pro vlastní logiku použijte třídu `SamplerIO` (pro IO operace). Vždy inicializujte `Logger` pro logování.

### Použití třídy SamplerIO
`SamplerIO` pro čisté IO operace (načítání, vyhledávání, přístup k metadatům). Vytvořte instanci a předejte logger.

| Metoda | Parametry | Příklad | Komentář | Návratový typ | Příklad návratu / Chyba |
|--------|-----------|---------|----------|---------------|-------------------------|
| `SamplerIO()` | - | `SamplerIO io;` | Konstruktor: Inicializuje prázdný seznam `sampleList`. Žádné logování v konstruktoru. | `void` (konstruktor) | - (žádný návratový kód, úspěšná inicializace) |
| `~SamplerIO()` | - | Automatický | Destruktor: Žádné speciální akce (seznam se uvolní automaticky). | `void` (destruktor) | - (automatické uvolnění) |
| `loadSamples(const std::string& directoryPath, Logger& logger)` | `directoryPath` – cesta k adresáři se WAV soubory (např. `"./samples"`), `logger` – reference na Logger | `io.loadSamples(R"(c:\samples)", logger);` | Načte samples z adresáře. Deleguje parsování názvů (regex), načte metadata (libsndfile). **NOVÉ**: Validuje konzistenci frekvence mezi názvem a souborem. **NOVÉ**: Detekuje formát (interleaved) a potřebu konverze (PCM→float). Loguje info/warn/error. Chyby (neexistující adresář, nekonzistentní frekvence): zaloguje a `std::exit(1)`. | `void` | - (úspěch: načte data; chyba: exit(1) po logu) |
| `findSampleInSampleList(uint8_t midi_note, uint8_t velocity) const` | `midi_note` (0-127), `velocity` (0-7) | `int idx = io.findSampleInSampleList(60, 5);` | Vyhledá index sample v interním seznamu. Vrátí -1, pokud nenalezeno. Lineární prohledávání, žádné logování. | `int` | `0` (index první shody); `-1` (nenalezeno, není chyba) |
| `getLoadedSampleList() const` | - | `const auto& list = io.getLoadedSampleList();` | Vrátí konstantní referenci na vektor `SampleInfo` pro čtení metadat. Žádné logování. | `const std::vector<SampleInfo>&` | Reference na vektor (prázdný, pokud není načteno) |
| `getFilename(int index, Logger& logger) const` | `index` – index v seznamu, `logger` – reference na Logger | `const char* fn = io.getFilename(idx, logger);` | Vrátí cestu k souboru. Kontroluje platnost indexu (>=0 a < velikost); při chybě: zaloguje error a `std::exit(1)`. | `const char*` | Cesta k souboru (např. `"C:\\samples\\m60-vel5-f44.wav"`); chyba: exit(1) po logu |
| `getMidiNote(int index, Logger& logger) const` | `index` – index v seznamu, `logger` – reference na Logger | `uint8_t note = io.getMidiNote(idx, logger);` | Vrátí MIDI notu. Stejná kontrola indexu jako výše. | `uint8_t` | MIDI nota (např. `60` pro C4); chyba: exit(1) po logu |
| `getMidiNoteVelocity(int index, Logger& logger) const` | `index` – index v seznamu, `logger` – reference na Logger | `uint8_t vel = io.getMidiNoteVelocity(idx, logger);` | Vrátí velocity. Stejná kontrola indexu jako výše. | `uint8_t` | Velocity (např. `5`); chyba: exit(1) po logu |
| `getFrequency(int index, Logger& logger) const` | `index` – index v seznamu, `logger` – reference na Logger | `int freq = io.getFrequency(idx, logger);` | Vrátí frekvenci (Hz). Stejná kontrola indexu jako výše. | `int` | Frekvence (např. `44100`); chyba: exit(1) po logu |
| `getSampleCount(int index, Logger& logger) const` | `index` – index v seznamu, `logger` – reference na Logger | `sf_count_t frames = io.getSampleCount(idx, logger);` | Vrátí počet vzorků (frames) ze souboru. Kontrola indexu jako u ostatních getterů. | `sf_count_t` | Počet vzorků (např. `110250`); chyba: exit(1) po logu |
| `getDurationSeconds(int index, Logger& logger) const` | `index` – index v seznamu, `logger` – reference na Logger | `double dur = io.getDurationSeconds(idx, logger);` | Vrátí délku v sekundách (vypočteno jako frames/samplerate). Kontrola indexu jako u ostatních. | `double` | Délka v sekundách (např. `2.5`); chyba: exit(1) po logu |
| `getChannelCount(int index, Logger& logger) const` | `index` – index v seznamu, `logger` – reference na Logger | `int ch = io.getChannelCount(idx, logger);` | Vrátí počet kanálů ze souboru. Kontrola indexu jako u ostatních. | `int` | Počet kanálů (např. `2` pro stereo, `1` pro mono); chyba: exit(1) po logu |
| `getIsStereo(int index, Logger& logger) const` | `index` – index v seznamu, `logger` – reference na Logger | `bool stereo = io.getIsStereo(idx, logger);` | Vrátí true pokud stereo (channels >= 2). Kontrola indexu jako u ostatních. | `bool` | `true` (stereo), `false` (mono); chyba: exit(1) po logu |
| **NOVÉ** `getInterleavedFormat(int index, Logger& logger) const` | `index` – index v seznamu, `logger` – reference na Logger | `bool interleaved = io.getInterleavedFormat(idx, logger);` | **NOVÝ GETTER**: Vrátí true pokud interleaved formát (standard pro WAV). Kontrola indexu jako u ostatních. | `bool` | `true` (interleaved), `false` (non-interleaved); chyba: exit(1) po logu |
| **NOVÉ** `getNeedsConversion(int index, Logger& logger) const` | `index` – index v seznamu, `logger` – reference na Logger | `bool needsConv = io.getNeedsConversion(idx, logger);` | **NOVÝ GETTER**: Vrátí true pokud potřebuje konverzi do float (16-bit PCM → float). Kontrola indexu jako u ostatních. | `bool` | `true` (potřebuje konverzi), `false` (již float); chyba: exit(1) po logu |

**Příklad plného použití s novými gettery**:
```cpp
Logger logger("./");
SamplerIO io;  // Prázdný sampler
std::string dir = R"(c:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)";
io.loadSamples(dir, logger);  // Načte a zaloguje (s validací frekvence a detekce formátu)
int idx = io.findSampleInSampleList(108, 7);  // Vyhledá index
if (idx != -1) {
    const char* filename = io.getFilename(idx, logger);
    int freq = io.getFrequency(idx, logger);
    uint8_t note = io.getMidiNote(idx, logger);
    uint8_t vel = io.getMidiNoteVelocity(idx, logger);
    
    // Rozšířená metadata
    sf_count_t frames = io.getSampleCount(idx, logger);
    double duration = io.getDurationSeconds(idx, logger);
    int channels = io.getChannelCount(idx, logger);
    bool isStereo = io.getIsStereo(idx, logger);
    
    // NOVÁ metadata pro audio processing
    bool isInterleaved = io.getInterleavedFormat(idx, logger);
    bool needsConversion = io.getNeedsConversion(idx, logger);
    
    // Použij data: filename, freq, note, vel, duration, channels, formát, konverze atd.
}
```

### Funkce runSampler
- **Signatura**: `int runSampler(Logger& logger)`
- **Příklad**: `runSampler(logger);`
- **Komentář**: Spustí kompletní workflow (deleguje načítání do SamplerIO, vyhledá příklad MIDI 108/vel 7, použije gettery pro metadata včetně nových). Loguje všechny kroky včetně detailních informací o délce, kanálech, stereo módu, **formátu a potřebě konverze**. Vrátí 0 při úspěchu (chyby končí `std::exit(1)`). Použijte pro standalone test.

## Struktura projektu
### Klíčové soubory
- **CMakeLists.txt**: Definuje projekt, přidává libsndfile, linkuje soubory (`main.cpp`, `sampler/*.cpp`) a include cesty.
- **main.cpp**: Hlavní vstup – inicializuje logger, zpracuje argumenty a volá `runSampler`.
- **sampler/core_logger.h/cpp**: Implementace třídy `Logger`.
- **sampler/sampler.h/cpp**: Deklarace a implementace funkce `runSampler` a třídy `SamplerIO` (po refaktoru sloučeno pro jednoduchost).
- **.vscode/**: Konfigurace pro VS Code (build tasky, launch, settings pro terminal a file associations).
- **README.md**: Tento soubor.

### Struktury a třídy
#### Struktura `SampleInfo` (v `sampler.h`) - **ROZŠÍŘENÁ**
Uchovává metadata o WAV samplu:
```cpp
struct SampleInfo {
    char filename[256];             // Cesta k souboru (max 256 znaků)
    uint8_t midi_note;              // MIDI nota (0-127)
    uint8_t midi_note_velocity;     // Velocity (0-7)
    int frequency;                  // Frekvence vzorkování (Hz, z WAV headeru)
    sf_count_t sample_count;        // Celkový počet vzorků (frames)
    double duration_seconds;        // Délka v sekundách
    int channels;                   // Počet kanálů (1=mono, 2=stereo, atd.)
    bool is_stereo;                 // True pokud stereo (channels >= 2)
    bool interleaved_format;        // NOVÉ: True pokud interleaved formát (standard pro WAV)
    bool needs_conversion;          // NOVÉ: True pokud potřebuje konverzi do float (16-bit PCM → float)
};
```
- Používá se pro ukládání dat v `std::vector<SampleInfo>` v `SamplerIO`.
- **Rozšířeno o nová pole** pro kompletní metadata z WAV souborů a analýzu formátu.

#### Třída `Logger` (v `core_logger.h/cpp`)
- **Konstruktor**: `Logger(const std::string& path)` – Vytvoří složku `core_logger`, smaže starý log, otevře nový soubor. Selhání vede k `std::exit(1)`.
- **Metoda `log`**: `void log(const std::string& component, const std::string& severity, const std::string& message)` – Zaloguje zprávu ve formátu `[timestamp] [component] [severity]: message`. Thread-safe díky `std::mutex`.
- **Destruktor**: `~Logger()` – Uzavře soubor.
- **Příklad instancování a volání** (z `main.cpp`):
  ```cpp
  Logger logger("./");  // Cesta k aktuálnímu adresáři
  logger.log("SamplerIO/loadSamples", "info", "Loading samples from: /path/to/dir");
  ```

## Řešení problémů
- **Execution Policy**: Spusťte `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser` v PowerShell.
- **libsndfile chybí**: Stáhněte z [libsndfile GitHub](https://github.com/libsndfile/libsndfile) a upravte CMakeLists.txt.
- **Linkovací chyba LNK1104**: Ukončete všechny procesy IthacaCore.exe, smažte build složku a sestavte znovu.
- **Logy**: Sledujte `core_logger/core_logger.log`. Pro real-time: `Get-Content -Path "core_logger/core_logger.log" -Tail 10 -Wait`.
- **Rozšíření**: Upravte cestu v `sampler.cpp` pro jiné samples. Pro plné načítání audio dat rozšiřte `SamplerIO` o `sf_readf_float` z libsndfile.
- **Nová chyba - nekonzistence frekvence**: Program se ukončí s chybou, pokud frekvence v názvu souboru neodpovídá frekvenci v WAV headeru.
- **Nepodporovaný subformát**: Program končí s chybou při detekci nepodporovaného audio formátu.

## Poznámky
- **Názvosloví souborů**: Samples musí mít formát `mXXX-velY-fZZ.wav` (XXX = MIDI nota 0-127, Y = velocity 0-7, ZZ = zkrácená frekvence s 'f' prefixem: f8, f11, f16, f22, f44, f48, f88, f96, f176, f192). Jinak se ignorují s warningem.
- **NOVÁ validace**: Frekvence v názvu (fZZ) musí odpovídat frekvenci v WAV headeru, jinak program končí s chybou.
- **NOVÁ detekce formátu**: Program analyzuje, zda je WAV interleaved (standard) a jaký má subformát (16-bit PCM, float, atd.).
- **Podporované formáty**: 16-bit PCM, 24-bit PCM, 32-bit PCM, 32-bit float, 64-bit double.
- **Optimální formát**: 32-bit float (žádná konverze potřebná).
- **Chyby**: Selhání inicializace (adresář, přístup, neplatný index v getterech, nekonzistentní frekvence, nepodporovaný formát) vede k logu a `std::exit(1)`.
- **Thread-safety**: Pouze logování je chráněno mutexem; sampler není navržen pro multi-threading.
- **Rozšířená metadata**: Program nyní načítá a poskytuje přístup k délce, počtu kanálů, stereo detekci, počtu vzorků, formátu a potřebě konverze.
- Projekt je přenositelný; pro složitější aplikace přidejte JUCE nebo další knihovny.

## Grab-Files
```
PowerShell.exe -ExecutionPolicy Bypass -File .\Grab-Files.ps1
```

## Watch log

```
Get-Content -Path "./core_logger/core_logger.log" -Tail 10 -Wait
```