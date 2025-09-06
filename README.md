# IthacaCore Audio Sampler

## Popis projektu
IthacaCore je profesionální audio sampler engine napsaný v C++17. Systém je modulární, s podporou logování (Logger), načítání WAV sample souborů (SamplerIO) a koordinací celého workflow (Sampler). Navržen pro snadnou integraci do větších projektů, jako je audio plugin nebo standalone aplikace. Používá libsndfile pro práci s WAV soubory a podporuje parsování nástupů souborů ve formátu `mXXX-velY-ZZ.wav` (kde XXX je MIDI nota 0-127, Y je velocity 0-7).

Klíčové vlastnosti:
- Thread-safe logování do souboru.
- Načítání metadat (frekvence, MIDI nota, velocity) z WAV souborů.
- Vyhledávání sample podle MIDI noty a velocity.
- Žádné konzolové výstupy v modulech – vše logováno.

## Požadavky
- Visual Studio Code
- Visual Studio 2022 Community Edition (s C++ komponentami)
- CMake (verze 3.10+, přidejte do PATH)
- Rozšíření VS Code: CMake Tools a C/C++ od Microsoftu
- libsndfile (stáhnete v CMakeLists.txt přes add_subdirectory, nebo nainstalujte externě)
- C++17 kompatibilní kompilátor (MSVC/GCC/Clang)

## Postup nastavení
1. Naklonujte nebo vytvořte složku projektu `IthacaCore`.
2. Uložte soubory (CMakeLists.txt, main.cpp, sampler/ moduly) do kořenové složky.
3. Vytvořte složku `.vscode` a přidejte tasks.json, launch.json, settings.json.
4. Otevřete projekt v VS Code (File → Open Folder).
5. Spusťte CMake: Configure (Ctrl+Shift+P → CMake: Configure).
6. Sestavte: Ctrl+Shift+P → Tasks: Run Task → build.

## Sestavení a spuštění
- **Sestavení**: Spusťte úlohu "build" (Ctrl+Shift+P → Tasks: Run Task → build). Vytvoří `Debug/IthacaCore.exe`.
- **Spuštění**: Klikněte na tlačítko "Spustit" (F5) nebo spusťte `.\Debug\IthacaCore.exe` v terminálu.
- **Výstup**: Program vypíše banner na konzoli, inicializuje logger a spustí sampler. Logy v `core_logger/core_logger.log`.
- **Čištění**: Smažte složku `build` pro rebuild. Použijte target `clean-logs` pro čištění logů.

**Poznámka**: Cesta k `vcvars64.bat` v tasks.json je pro VS 2022 Community. Pokud se liší (např. BuildTools), upravte ji. Pro PowerShell execution policy: `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser`.

## Použití v vlastním projektu
Pro integraci do vašeho C++ projektu zahrňte hlavičky a linkujte libsndfile. Příklad minimálního použití (v main.cpp):

```cpp
#include "core_logger.h"
#include "sampler.h"

int main() {
    Logger logger("./");  // Cesta k logům
    int result = runSampler(logger);  // Spustí celý workflow
    return result;
}
```

Pro vlastní logiku použijte třídy `Sampler` (koordinátor) nebo `SamplerIO` (pouze IO). Vždy inicializujte `Logger` pro logování.

### Použití třídy Sampler
`Sampler` řídí celý systém (načítání, vyhledávání). Vytvořte instanci s loggerem.

| Metoda | Parametry | Příklad | Komentář |
|--------|-----------|---------|----------|
| `Sampler(Logger& logger)` | `logger` – reference na Logger | `Sampler sampler(logger);` | Konstruktor: Inicializuje SamplerIO. Loguje start. |
| `~Sampler()` | - | Automatický | Destruktor: Loguje ukončení a cleanup modulů. |
| `loadSamples(const std::string& directoryPath)` | `directoryPath` – cesta k adresáři se WAV soubory (např. `"./samples"`) | `sampler.loadSamples(R"(c:\samples)");` | Načte samples z adresáře. Deleguje do SamplerIO. Chyby končí exit(1). |
| `findSample(uint8_t midi_note, uint8_t velocity)` | `midi_note` (0-127), `velocity` (0-7) | `int idx = sampler.findSample(60, 5);` | Vyhledá index sample v seznamu. Vrátí -1, pokud nenalezeno. Loguje výsledek. |
| `getSampleList()` | - | `const auto& list = sampler.getSampleList();` | Vrátí konstantní referenci na vektor `SampleInfo`. Pro přístup k metadatům (filename, freq atd.). |

**Příklad plného použití**:
```cpp
Logger logger("./");
Sampler sampler(logger);
sampler.loadSamples("./samples");
int idx = sampler.findSample(60, 0);  // Middle C, min velocity
if (idx != -1) {
    const auto& sample = sampler.getSampleList()[idx];
    // Použijte sample.filename atd.
}
```

### Použití třídy SamplerIO
`SamplerIO` pro čisté IO operace (bez koordinátoru). Používejte, pokud nepotřebujete další moduly.

| Metoda | Parametry | Příklad | Komentář |
|--------|-----------|---------|----------|
| `SamplerIO(Logger& logger)` | `logger` – reference na Logger | `SamplerIO io(logger);` | Konstruktor: Vytvoří prázdný seznam. Loguje start. |
| `~SamplerIO()` | - | Automatický | Destruktor: Loguje finální počet samples. |
| `loadSamples(const std::string& directoryPath)` | `directoryPath` – cesta k WAV souborům | `io.loadSamples("./samples");` | Prochází adresář, parsuje názvy, načte metadata (libsndfile). Loguje statistiky. Chyby: exit(1). |
| `findSample(uint8_t midi_note, uint8_t velocity)` | `midi_note` (0-127), `velocity` (0-7) | `int idx = io.findSample(60, 5);` | Lineární vyhledávání. Vrátí -1 při chybě. Loguje hledání. |
| `getSampleList()` | - | `const auto& list = io.getSampleList();` | Vrátí vektor `SampleInfo`. Loguje velikost. |

**Příklad použití**:
```cpp
Logger logger("./");
SamplerIO io(logger);
io.loadSamples(R"(c:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)");
int idx = io.findSample(108, 7);
if (idx != -1) {
    printf("Sample: %s\n", io.getSampleList()[idx].filename);
}
```

### Funkce runSampler
- **Signatura**: `int runSampler(Logger& logger)`
- **Příklad**: `runSampler(logger);`
- **Komentář**: Spustí kompletní workflow (načtení, test vyhledávání). Použijte pro standalone test. Vrátí 0 při úspěchu.

## Struktura souborů
- `main.cpp`: Hlavní vstup, inicializuje logger a sampler.
- `sampler/`: Moduly (core_logger.h/cpp, sampler_io.h/cpp, sampler.h/cpp).
- `CMakeLists.txt`: Konfigurace build (linkuje libsndfile).
- `.vscode/`: VS Code konfigurace (tasks, launch, settings).

## Řešení problémů
- **Execution Policy**: Spusťte `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser` v PowerShell.
- **libsndfile chybí**: Stáhněte z [libsndfile GitHub](https://github.com/libsndfile/libsndfile) a upravte CMakeLists.txt.
- **Logy**: Sledujte `core_logger/core_logger.log`. Pro real-time: `Get-Content -Path "core_logger/core_logger.log" -Tail 10 -Wait`.
- **Rozšíření**: Přidejte efekty/sequencer do Sampler (komentáře v sampler.h).


## Grab-Files
```
PowerShell.exe -ExecutionPolicy Bypass -File .\Grab-Files.ps1
```