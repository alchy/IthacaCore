# IthacaCore: C++ Audio Sampler Engine

## Popis projektu
IthacaCore je profesionální audio sampler engine napsaný v C++17, navržený pro efektivní zpracování WAV souborů a polyfonní přehrávání s ADSR obálkou. Projekt je modulární, podporuje thread-safe logování, načítání WAV souborů do paměti jako stereo float buffery a koordinaci polyfonního přehrávání. Klíčové moduly zahrnují `Logger`, `SamplerIO`, `InstrumentLoader`, `Voice`, `VoiceManager`, `Envelope` a `WavExporter`. Systém je optimalizován pro integraci do audio pluginů nebo standalone aplikací, s důrazem na RT-safe operace a JUCE-ready formát.

**Klíčové vlastnosti:**
- Thread-safe logování do souboru (`core_logger/core_logger.log`).
- Prohledávání WAV souborů ve formátu `mXXX-velY-fZZ.wav` (XXX = MIDI nota 0-127, Y = velocity 0-7, ZZ = zkrácená frekvence, např. f44 pro 44100 Hz).
- Načítání WAV souborů do paměti jako stereo interleaved float buffery `[L1,R1,L2,R2...]`.
- Automatická mono→stereo konverze (L=R) pro jednotný formát.
- Podpora 16-bit PCM, 24-bit PCM, 32-bit PCM, 32-bit float a 64-bit double formátů.
- Polyfonní přehrávání s ADSR obálkou (attack, decay, sustain, release) a stavy hlasu (Idle, Attacking, Sustaining, Releasing).
- RT-safe zpracování obálek s předpočítanými křivkami pro 44100 Hz a 48000 Hz.
- Export WAV souborů pro testování (Pcm16/float formát).
- Validace konzistence frekvence, formátu a stereo dat.
- Simulace JUCE AudioBuffer pro snadnou integraci s JUCE.

## Požadavky
- Visual Studio Code s rozšířeními CMake Tools a C/C++ od Microsoftu.
- Visual Studio 2022 Build Tools nebo Community Edition (C++ komponenty).
- CMake 3.10+ (v PATH).
- `libsndfile` (automaticky integrováno přes `add_subdirectory` v `CMakeLists.txt`).
- C++17 kompatibilní kompilátor (MSVC/GCC/Clang).

## Postup nastavení
1. Uložte všechny soubory do kořenové složky projektu (např. `IthacaCore`).
2. Vytvořte složku `.vscode` a uložte do ní `tasks.json`, `launch.json` a `settings.json`.
3. Otevřete projekt v VS Code (File → Open Folder).
4. Spusťte CMake: Configure (Ctrl+Shift+P → CMake: Configure) pro generování build souborů ve složce `build`.
5. (Volitelné) Stáhněte `libsndfile` do složky `libsndfile`, pokud není automaticky integrováno.

## Sestavení a spuštění
- **Sestavení**: Spusťte úlohu "build" (Ctrl+Shift+P → Tasks: Run Task → build). Tím se nastaví MSVC prostředí (`vcvars64.bat`) a spustí `cmake --build .`. Výstup: `Debug/IthacaCore.exe`.
- **Spuštění**: Klikněte na tlačítko spustit (|>) nebo stiskněte F5. Program inicializuje logger, načte samples, inicializuje hlasy a testuje polyfonní přehrávání.
- **Výstup**:
  - Konzole: Zprávy o inicializaci a stavu.
  - Log: `core_logger/core_logger.log` obsahuje detaily o načítání, validaci, inicializaci obálek a hlasů.
  - Testovací exporty: WAV soubory ve složce `./exports/tests/`.
- **Čištění**: Smažte složky `build` a `core_logger` pro reset, nebo použijte `cmake --build . --target clean-all`.

**Poznámka**: Upravte cestu k `vcvars64.bat` v `tasks.json`, pokud používáte Visual Studio Community: `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat`. Pro PowerShell povolte skripty: `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser`.

## Použití v projektu
Pro integraci do vlastního projektu zahrňte hlavičky a linkujte `libsndfile`. Minimální příklad:

<xaiArtifact>
```cpp
#include "core_logger.h"
#include "sampler.h"

int main() {
    Logger logger("./");
    int result = runSampler(logger); // Spustí workflow (SamplerIO, InstrumentLoader, VoiceManager, Envelope)
    return result;
}
```

### Třída `SamplerIO`
Spravuje IO operace, prohledávání a metadata WAV souborů.

| Metoda | Parametry | Popis | Návratový typ |
|--------|-----------|-------|---------------|
| `SamplerIO()` | - | Inicializuje prázdný seznam `sampleList`. | `void` |
| `~SamplerIO()` | - | Uvolní seznam automaticky. | `void` |
| `scanSampleDirectory(const std::string& directoryPath, Logger& logger)` | `directoryPath` (cesta k WAV souborům), `logger` | Prohledá adresář, parsuje názvy (`mXXX-velY-fZZ.wav`), validuje frekvenci a formát. Chyby vedou k `std::exit(1)`. | `void` |
| `findSampleInSampleList(uint8_t midi_note, uint8_t velocity, int sampleRate) const` | `midi_note` (0-127), `velocity` (0-7), `sampleRate` | Vyhledá index sample v seznamu. | `int` (-1 pokud nenalezeno) |
| `getLoadedSampleList() const` | - | Vrátí konstantní referenci na vektor `SampleInfo`. | `const std::vector<SampleInfo>&` |

**Příklad**:
<xaiArtifact>
```cpp
Logger logger("./");
SamplerIO sampler;
sampler.scanSampleDirectory(R"(c:\samples)", logger);
int idx = sampler.findSampleInSampleList(60, 5, 44100);
if (idx != -1) {
    const auto& list = sampler.getLoadedSampleList();
    logger.log("demo", "info", "Found sample: MIDI=" + std::to_string(list[idx].midi_note));
}
```

### Třída `InstrumentLoader`
Načítá WAV soubory do paměti jako stereo float buffery, zajišťuje mono→stereo konverzi a validaci.

| Metoda | Parametry | Popis | Návratový typ |
|--------|-----------|-------|---------------|
| `InstrumentLoader(SamplerIO& sampler, int targetSampleRate, Logger& logger)` | `sampler`, `targetSampleRate` (např. 44100), `logger` | Inicializuje pole pro MIDI noty 0-127. | `void` |
| `loadInstrument()` | - | Načte samples do paměti, provede mono→stereo konverzi, validuje konzistenci. | `void` |
| `getInstrumentNote(uint8_t midi_note)` | `midi_note` (0-127) | Vrátí `Instrument` pro danou MIDI notu. Chybný index: `std::exit(1)`. | `Instrument&` |
| `getTotalLoadedSamples()` | - | Vrátí celkový počet načtených samples. | `int` |
| `getMonoSamplesCount()` | - | Vrátí počet původně mono samples. | `int` |
| `getStereoSamplesCount()` | - | Vrátí počet původně stereo samples. | `int` |
| `getTargetSampleRate()` | - | Vrátí nastavenou vzorkovací frekvenci. | `int` |

**Struktura `Instrument`**:
<xaiArtifact>
```cpp
struct Instrument {
    SampleInfo* sample_ptr_sampleInfo[8];                   // Pointery na SampleInfo
    float* sample_ptr_velocity[8];                          // Stereo float buffery [L,R,L,R...]
    bool velocityExists[8];                                 // Indikátory existence
    int frame_count_stereo[8];                              // Počet stereo frame párů
    int total_samples_stereo[8];                            // Celkový počet float hodnot
    bool was_originally_mono[8];                            // Původní formát
    float* get_sample_begin_pointer(uint8_t velocity);      // Pointer na stereo data
    int get_frame_count(uint8_t velocity);                  // Počet frame párů
    int get_total_sample_count(uint8_t velocity);           // Celkový počet floatů
    bool get_was_originally_mono(uint8_t velocity);         // Původní formát info
};
```

**Příklad**:
<xaiArtifact>
```cpp
Logger logger("./");
SamplerIO sampler;
sampler.scanSampleDirectory(R"(c:\samples)", logger);
InstrumentLoader loader(sampler, 44100, logger);
loader.loadInstrument();
Instrument& inst = loader.getInstrumentNote(108);
if (inst.velocityExists[7]) {
    float* stereoData = inst.get_sample_begin_pointer(7);
    int frames = inst.get_frame_count(7);
    logger.log("demo", "info", "Loaded MIDI 108, vel 7, frames: " + std::to_string(frames));
}
```

### Třída `Envelope`
Spravuje per-voice ADSR obálku, deleguje těžká data na `EnvelopeStaticData`.

| Metoda | Parametry | Popis | Návratový typ |
|--------|-----------|-------|---------------|
| `Envelope()` | - | Inicializuje výchozí hodnoty (attack=64, release=64, sustain=0.7). | `void` |
| `setAttackMIDI(uint8_t midi_value)` | `midi_value` (0-127) | Nastaví MIDI hodnotu pro attack (RT-safe). | `void` |
| `setReleaseMIDI(uint8_t midi_value)` | `midi_value` (0-127) | Nastaví MIDI hodnotu pro release (RT-safe). | `void` |
| `setSustainLevelMIDI(uint8_t midi_value)` | `midi_value` (0-127) | Nastaví sustain úroveň (0.0-1.0, RT-safe). | `void` |
| `getSustainLevel() const` | - | Vrátí sustain úroveň (RT-safe). | `float` |
| `getAttackGains(float* gain_buffer, int num_samples, int envelope_attack_position, int sample_rate) const` | `gain_buffer`, `num_samples`, `envelope_attack_position`, `sample_rate` | Získá hodnoty attack obálky (RT-safe). | `bool` |
| `getReleaseGains(float* gain_buffer, int num_samples, int envelope_release_position, int sample_rate) const` | `gain_buffer`, `num_samples`, `envelope_release_position`, `sample_rate` | Získá hodnoty release obálky (RT-safe). | `bool` |
| `getAttackLength(int sample_rate) const` | `sample_rate` | Vrátí délku attack v ms (RT-safe). | `float` |
| `getReleaseLength(int sample_rate) const` | `sample_rate` | Vrátí délku release v ms (RT-safe). | `float` |

**Příklad**:
<xaiArtifact>
```cpp
Logger logger("./");
Envelope envelope;
envelope.setAttackMIDI(80);
envelope.setReleaseMIDI(40);
envelope.setSustainLevelMIDI(90);
float gains[256];
bool continues = envelope.getAttackGains(gains, 256, 0, 44100);
logger.log("demo", "info", "Attack length: " + std::to_string(envelope.getAttackLength(44100)) + " ms");
```

### Třída `EnvelopeStaticData`
Spravuje předpočítaná data obálek pro všechny MIDI hodnoty a vzorkovací frekvence.

| Metoda | Parametry | Popis | Návratový typ |
|--------|-----------|-------|---------------|
| `initialize(Logger& logger)` | `logger` | Generuje obálky pro 44100/48000 Hz (non-RT). | `bool` |
| `cleanup()` | - | Uvolní data (non-RT). | `void` |
| `getAttackGains(float* gainBuffer, int numSamples, int position, uint8_t midiValue, int sampleRate)` | `gainBuffer`, `numSamples`, `position`, `midiValue`, `sampleRate` | Získá hodnoty attack obálky (RT-safe). | `bool` |
| `getReleaseGains(float* gainBuffer, int numSamples, int position, uint8_t midiValue, int sampleRate)` | `gainBuffer`, `numSamples`, `position`, `midiValue`, `sampleRate` | Získá hodnoty release obálky (RT-safe). | `bool` |
| `getAttackLength(uint8_t midiValue, int sampleRate)` | `midiValue`, `sampleRate` | Vrátí délku attack v ms (RT-safe). | `float` |
| `getReleaseLength(uint8_t midiValue, int sampleRate)` | `midiValue`, `sampleRate` | Vrátí délku release v ms (RT-safe). | `float` |
| `isInitialized()` | - | Kontroluje inicializaci dat (RT-safe). | `bool` |

**Příklad**:
<xaiArtifact>
```cpp
Logger logger("./");
EnvelopeStaticData::initialize(logger);
float gains[256];
EnvelopeStaticData::getAttackGains(gains, 256, 0, 64, 44100);
logger.log("demo", "info", "Attack length: " + std::to_string(EnvelopeStaticData::getAttackLength(64, 44100)) + " ms");
EnvelopeStaticData::cleanup();
```

### Třída `Voice`
Spravuje jednu hlasovou jednotku s ADSR obálkou a stavy (Idle, Attacking, Sustaining, Releasing).

| Metoda | Parametry | Popis | Návratový typ |
|--------|-----------|-------|---------------|
| `Voice()` | - | Default konstruktor pro pool. | `void` |
| `Voice(uint8_t midiNote, Logger& logger, Envelope& envelope)` | `midiNote`, `logger`, `envelope` | Inicializuje hlas s MIDI notou a obálkou. | `void` |
| `initialize(const Instrument& instrument, int sampleRate)` | `instrument`, `sampleRate` | Inicializuje hlas s instrumentem a frekvencí. | `void` |
| `setNoteState(bool isOn, uint8_t velocity)` | `isOn`, `velocity` | Nastaví note-on/off, přepíná stavy. | `void` |
| `processBlock(AudioBuffer& outputBuffer, int numSamples)` | `outputBuffer`, `numSamples` | Zpracuje blok s aplikací ADSR obálky. | `bool` |
| `isActive() const` | - | Vrátí true, pokud není Idle. | `bool` |
| `getState() const` | - | Vrátí aktuální stav (Idle, Attacking, Sustaining, Releasing). | `VoiceState` |
| `getFinalGain() const` | - | Vrátí kombinovaný gain (velocity * envelope * master). | `float` |

### Třída `VoiceManager`
Spravuje pool hlasů pro polyfonní přehrávání.

| Metoda | Parametry | Popis | Návratový typ |
|--------|-----------|-------|---------------|
| `VoiceManager(int maxVoices, Logger& logger)` | `maxVoices`, `logger` | Inicializuje pool hlasů. | `void` |
| `initializeAll(InstrumentLoader& loader, int sampleRate)` | `loader`, `sampleRate` | Inicializuje všechny hlasy s instrumenty. | `void` |
| `setNoteState(uint8_t midiNote, bool isOn, uint8_t velocity)` | `midiNote`, `isOn`, `velocity` | Nastaví note-on/off pro MIDI notu. | `void` |
| `processBlock(AudioBuffer& outputBuffer, int numSamples)` | `outputBuffer`, `numSamples` | Zpracuje blok pro všechny aktivní hlasy. | `bool` |
| `getActiveVoicesCount() const` | - | Vrátí počet aktivních hlasů. | `int` |

**Příklad**:
<xaiArtifact>
```cpp
Logger logger("./");
SamplerIO sampler;
sampler.scanSampleDirectory(R"(c:\samples)", logger);
InstrumentLoader loader(sampler, 44100, logger);
loader.loadInstrument();
Envelope envelope;
EnvelopeStaticData::initialize(logger);
VoiceManager manager(128, logger);
manager.initializeAll(loader, 44100);
manager.setNoteState(60, true, 100);
AudioBuffer output(512);
manager.processBlock(output, 512);
manager.setNoteState(60, false, 0);
```

### Třída `WavExporter`
Exportuje audio data do WAV souborů pro testování.

| Metoda | Parametry | Popis | Návratový typ |
|--------|-----------|-------|---------------|
| `WavExporter(const std::string& outputDir, Logger& logger, ExportFormat exportFormat)` | `outputDir`, `logger`, `exportFormat` (Pcm16/Float) | Inicializuje exportér. | `void` |
| `wavFileCreate(const std::string& filename, int frequency, int bufferSize, bool stereo, bool dummy_write)` | `filename`, `frequency`, `bufferSize`, `stereo`, `dummy_write` | Vytvoří WAV soubor a alokuje buffer. | `float*` |
| `wavFileWriteBuffer(float* buffer_ptr, int buffer_size)` | `buffer_ptr`, `buffer_size` | Zapíše buffer do souboru. | `bool` |
| `~WavExporter()` | - | Uzavře soubor a uvolní buffery. | `void` |

**Příklad**:
<xaiArtifact>
```cpp
Logger logger("./");
WavExporter exporter("./exports", logger);
float* buffer = exporter.wavFileCreate("test.wav", 44100, 512, true, true);
std::fill(buffer, buffer + 512 * 2, 0.5f); // Naplní stereo buffer
exporter.wavFileWriteBuffer(buffer, 512);
```

### Funkce `runSampler`
- **Signatura**: `int runSampler(Logger& logger)`
- **Popis**: Spustí workflow: inicializuje `SamplerIO`, `InstrumentLoader`, `Envelope`, `VoiceManager`, načte samples, nastaví testovací notu (např. MIDI 108, vel 7), procesuje blok s ADSR obálkou a exportuje WAV. Loguje všechny kroky. Vrátí 0 při úspěchu, jinak `std::exit(1)`.

## Struktura projektu
- **CMakeLists.txt**: Definuje projekt, linkuje `libsndfile`, obsahuje všechny zdrojové soubory.
- **main.cpp**: Vstupní bod, volá `runSampler`.
- **sampler/core_logger.h/cpp**: Thread-safe logování.
- **sampler/sampler.h/cpp**: Funkce `runSampler` a třída `SamplerIO`.
- **sampler/instrument_loader.h/cpp**: Načítání samples do paměti.
- **sampler/voice.h/cpp**: Správa jedné hlasové jednotky.
- **sampler/voice_manager.h/cpp**: Polyfonní management hlasů.
- **sampler/envelopes/envelope.h/cpp**: Per-voice ADSR obálka.
- **sampler/envelopes/envelope_static_data.h/cpp**: Předpočítaná data obálek.
- **sampler/wav_file_exporter.h/cpp**: Export WAV souborů.
- **.vscode/**: Konfigurace VS Code (build, launch, settings).
- **README.md**: Tento soubor.

## Řešení problémů
- **Execution Policy**: `Set-ExecutionPolicy RemoteSigned -Scope CurrentUser`.
- **libsndfile chybí**: Stáhněte z [libsndfile GitHub](https://github.com/libsndfile/libsndfile).
- **Linkovací chyba LNK1104**: Ukončete procesy `IthacaCore.exe`, smažte `build` složku, znovu sestavte.
- **Logy**: Sledujte real-time: `Get-Content -Path "./core_logger/core_logger.log" -Tail 10 -Wait`.
- **Chyby validace**: Program ukončí s chybou při nekonzistenci frekvence, nepodporovaném formátu nebo neplatném indexu.
- **Envelope chyby**: Metody `getAttackGains`/`getReleaseGains` vyžadují inicializaci `EnvelopeStaticData`.

## Poznámky
- **Názvosloví souborů**: WAV soubory musí být ve formátu `mXXX-velY-fZZ.wav`. Jinak ignorovány s warningem.
- **Frekvence**: Musí odpovídat WAV headeru, jinak `std::exit(1)`.
- **Stereo formát**: Všechny buffery jsou interleaved `[L,R,L,R...]`.
- **JUCE integrace**: Buffery jsou připraveny pro JUCE AudioBuffer de-interleaving.
- **RT-safe operace**: Třída `Envelope` a `Voice` jsou optimalizovány pro real-time audio.
- **Paměť**: Předpočítané obálky (`EnvelopeStaticData`) šetří paměť pro více hlasů.
- **Chyby**: Kritické chyby (neinicializovaná data, neplatná frekvence) vedou k `std::exit(1)`.

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

