# Envelope Class - Developer Documentation

## Přehled

Třída `Envelope` poskytuje RT-safe správu ADSR obálek (Attack, Decay, Sustain, Release) pro audio syntézu v IthacaPlayer projektu. Obálky jsou předpočítané při inicializaci pro vzorkovací frekvence 44100 Hz a 48000 Hz, což zajišťuje vysoký výkon během real-time audio zpracování.

## Klíčové vlastnosti

- **RT-Safe operace**: Všechny metody pro získávání hodnot obálek jsou navržené pro real-time audio zpracování
- **Předpočítané obálky**: Exponenciální křivky generované při startu aplikace
- **Dual sample rate**: Podpora 44100 Hz a 48000 Hz vzorkovacích frekvencí
- **MIDI mapping**: Přímé mapování MIDI hodnot (0-127) na parametry obálek
- **Kritické chyby**: Automatické ukončení programu při nesprávném použití
- **Podrobné logování**: Kompletní logování pro debugging a monitoring

## Základní workflow

```cpp
// 1. Inicializace třídy
Envelope envelope;
Logger logger;

// 2. Generování obálek
if (!envelope.initialize(logger)) {
    // Kritická chyba - program se ukončí automaticky
}

// 3. Nastavení vzorkovací frekvence
if (!envelope.setEnvelopeFrequency(44100, logger)) {
    // Neplatná frekvence
}

// 4. Konfigurace RT módu pro audio zpracování
Envelope::setRTMode(true);

// 5. Runtime použití (RT-safe)
envelope.setAttackMIDI(64);    // Střední attack
envelope.setReleaseMIDI(96);   // Pomalejší release
envelope.setSustainLevelMIDI(100); // 78% sustain

// 6. Získávání hodnot během audio zpracování
float gainBuffer[512];
bool continues = envelope.getAttackGains(gainBuffer, 512, currentPosition);
```

## API Reference

### Konstruktor

```cpp
Envelope();
```
Vytvoří prázdnou instanci třídy. **Povinné**: Před použitím musí být zavolána metoda `initialize()`.

### Inicializační metody

#### initialize()
```cpp
bool initialize(Logger& logger);
```
**Popis**: Generuje předpočítané obálky pro všechny MIDI hodnoty (0-127) a obě vzorkovací frekvence.

**Parametry**:
- `logger` - Reference na Logger pro logování procesu

**Návratová hodnota**: `true` při úspěchu, program se ukončí při chybě

**Non-RT Safe**: Alokuje paměť, volá pouze při startu aplikace

**Příklad**:
```cpp
Envelope envelope;
Logger logger;
if (!envelope.initialize(logger)) {
    // Tato situace se nikdy nevyskytne - program se ukončí při chybě
}
```

#### setEnvelopeFrequency()
```cpp
bool setEnvelopeFrequency(int sampleRate, Logger& logger);
```
**Popis**: Nastaví vzorkovací frekvenci pro následné operace.

**Parametry**:
- `sampleRate` - Vzorkovací frekvence (44100 nebo 48000 Hz)
- `logger` - Reference na Logger

**Návratová hodnota**: `true` při úspěchu, `false` při neplatné frekvenci

**Non-RT Safe**: Loguje změnu frekvence

**Příklad**:
```cpp
if (!envelope.setEnvelopeFrequency(48000, logger)) {
    std::cerr << "Unsupported sample rate!" << std::endl;
}
```

### RT-Safe metody pro získávání hodnot

#### getAttackGains()
```cpp
bool getAttackGains(float* gain_buffer, int num_samples, int envelope_attack_position);
```
**Popis**: Získá hodnoty attack obálky pro daný blok vzorků.

**Parametry**:
- `gain_buffer` - Ukazatel na buffer pro výstupní hodnoty (0.0f - 1.0f)
- `num_samples` - Počet požadovaných vzorků
- `envelope_attack_position` - Aktuální pozice v obálce (offset)

**Návratová hodnota**: `true` pokud obálka pokračuje, `false` při dosažení konce

**RT-Safe**: Ano, bez alokací paměti

**Kritické chyby**: Program se ukončí pokud není nastavena frekvence nebo nejsou inicializována data

**Příklad**:
```cpp
float gains[256];
int currentPos = 0;

while (voice.isActive()) {
    bool continues = envelope.getAttackGains(gains, 256, currentPos);
    
    // Aplikuj gains na audio
    for (int i = 0; i < 256; ++i) {
        audioBuffer[i] *= gains[i];
    }
    
    currentPos += 256;
    
    if (!continues) {
        // Attack dokončen, přejdi na sustain
        voice.setState(VoiceState::Sustaining);
        break;
    }
}
```

#### getReleaseGains()
```cpp
bool getReleaseGains(float* gain_buffer, int num_samples, int envelope_release_position);
```
**Popis**: Získá hodnoty release obálky pro daný blok vzorků.

**Parametry a chování**: Stejné jako `getAttackGains()`, ale pro release fázi

**Příklad**:
```cpp
float gains[128];
int releasePos = 0;

bool continues = envelope.getReleaseGains(gains, 128, releasePos);
if (!continues && gains[127] <= 0.001f) {
    // Release dokončen, voice je připraven k ukončení
    voice.setState(VoiceState::Idle);
}
```

### RT-Safe konfigurační metody

#### setAttackMIDI()
```cpp
void setAttackMIDI(uint8_t midi_value);
```
**Popis**: Nastaví MIDI hodnotu pro attack obálku.

**Parametry**:
- `midi_value` - MIDI hodnota (0-127), automaticky omezena na platný rozsah

**RT-Safe**: Ano

**Kritické chyby**: Program se ukončí pokud není inicializováno

**Příklad**:
```cpp
// MIDI CC 73 (Attack Time)
envelope.setAttackMIDI(midiCC73Value);
```

#### setReleaseMIDI()
```cpp
void setReleaseMIDI(uint8_t midi_value);
```
**Popis**: Nastaví MIDI hodnotu pro release obálku.

**Parametry a chování**: Stejné jako `setAttackMIDI()`

#### setSustainLevelMIDI()
```cpp
void setSustainLevelMIDI(uint8_t midi_value);
```
**Popis**: Nastaví sustain úroveň na základě MIDI hodnoty.

**Parametry**:
- `midi_value` - MIDI hodnota (0-127) → lineárně mapováno na (0.0f-1.0f)

**RT-Safe**: Ano

**Příklad**:
```cpp
// MIDI CC 74 (Sustain Level)
envelope.setSustainLevelMIDI(100); // ~78% sustain
```

#### getSustainLevel()
```cpp
float getSustainLevel() const;
```
**Popis**: Vrátí aktuální sustain úroveň.

**Návratová hodnota**: Sustain úroveň (0.0f-1.0f)

**RT-Safe**: Ano

### Informativní metody

#### getAttackLength()
```cpp
float getAttackLength() const;
```
**Popis**: Vrátí délku attack obálky v milisekundách na základě aktuální MIDI hodnoty a vzorkovací frekvence.

**Návratová hodnota**: Délka v ms

**RT-Safe**: Ano

**Kritické chyby**: Program se ukončí pokud není inicializováno

**Příklad**:
```cpp
float attackMs = envelope.getAttackLength();
std::cout << "Attack time: " << attackMs << " ms" << std::endl;
```

#### getReleaseLength()
```cpp
float getReleaseLength() const;
```
**Popis**: Vrátí délku release obálky v milisekundách.

**Chování**: Stejné jako `getAttackLength()`

### Statické metody

#### setRTMode()
```cpp
static void setRTMode(bool enabled);
```
**Popis**: Nastaví RT mód pro všechny instance třídy.

**Parametry**:
- `enabled` - `true` pro RT mód (bez logování), `false` pro non-RT mód

**Použití**:
```cpp
// Při startu aplikace
Envelope::setRTMode(false);  // Povolí logování během inicializace

// Před spuštěním audio zpracování
Envelope::setRTMode(true);   // Vypne logování pro RT operace
```

## Integrace s Voice třídou

```cpp
class VoiceManager {
private:
    Envelope envelope_;
    std::vector<Voice> voices_;
    
public:
    bool initialize(Logger& logger, int sampleRate) {
        // 1. Inicializuj Envelope
        if (!envelope_.initialize(logger)) {
            return false;
        }
        
        if (!envelope_.setEnvelopeFrequency(sampleRate, logger)) {
            return false;
        }
        
        // 2. Inicializuj hlasy s referencí na Envelope
        for (auto& voice : voices_) {
            voice.initialize(instrument, sampleRate, envelope_, logger);
        }
        
        // 3. Přepni na RT mód
        Envelope::setRTMode(true);
        return true;
    }
    
    void handleNoteOn(uint8_t midiNote, uint8_t velocity, 
                     uint8_t attackCC, uint8_t releaseCC, uint8_t sustainCC) {
        // RT-SAFE: Nastav envelope parametry
        envelope_.setAttackMIDI(attackCC);
        envelope_.setReleaseMIDI(releaseCC);
        envelope_.setSustainLevelMIDI(sustainCC);
        
        // Spusť notu
        Voice* voice = findFreeVoice();
        if (voice) {
            voice->setNoteState(true, velocity);
        }
    }
};
```

## MIDI mapování

| MIDI Hodnota | Účinek |
|-------------|---------|
| 0 | Minimální čas (okamžitá změna) |
| 64 | Střední čas (~2.4s pro attack/release) |
| 127 | Maximální čas (~12s pro attack/release) |

**Sustain mapování**:
- MIDI 0 → 0.0f (tichý sustain)
- MIDI 64 → 0.5f (50% sustain) 
- MIDI 127 → 1.0f (plný sustain)

## Doporučené MIDI CC mapování

| CC | Parametr | Metoda |
|----|----------|---------|
| 73 | Attack Time | `setAttackMIDI()` |
| 75 | Release Time | `setReleaseMIDI()` |
| 74 | Sustain Level | `setSustainLevelMIDI()` |

## Error handling

Třída používá "fail-fast" přístup - při kritických chybách automaticky ukončuje program pomocí `std::exit(1)` po zalogování chyby:

**Kritické chyby vedoucí k ukončení programu**:
- Volání `getAttackGains`/`getReleaseGains` před `setEnvelopeFrequency`
- Volání `setAttackMIDI`/`setReleaseMIDI` před `initialize`
- Selhání inicializace obálek
- Neplatná vzorkovací frekvence v `generateEnvelopeForSampleRate`

**Non-kritické chyby**:
- `setEnvelopeFrequency` s neplatnou frekvencí → vrací `false`

## Výkonnostní doporučení

1. **Inicializace**: Volej pouze jednou při startu aplikace
2. **RT mód**: Zapni před audio zpracováním pro eliminaci logování
3. **Buffer size**: Použij buffer velikosti kompatibilní s audio driver blokem
4. **MIDI změny**: Aplikuj změny parametrů mimo audio thread pokud možno

## Příklad kompletního použití

```cpp
#include "envelope.h"
#include "core_logger.h"

int main() {
    Logger logger;
    Envelope envelope;
    
    // Inicializace
    if (!envelope.initialize(logger)) {
        return 1; // Nedosažitelné - program se ukončí automaticky
    }
    
    if (!envelope.setEnvelopeFrequency(44100, logger)) {
        std::cerr << "Failed to set sample rate" << std::endl;
        return 1;
    }
    
    // Konfigurace
    envelope.setAttackMIDI(80);     // Rychlejší attack
    envelope.setReleaseMIDI(40);    // Rychlejší release  
    envelope.setSustainLevelMIDI(90); // Vysoký sustain
    
    std::cout << "Attack length: " << envelope.getAttackLength() << " ms" << std::endl;
    std::cout << "Release length: " << envelope.getReleaseLength() << " ms" << std::endl;
    std::cout << "Sustain level: " << envelope.getSustainLevel() << std::endl;
    
    // Přepnutí na RT mód před audio zpracováním
    Envelope::setRTMode(true);
    
    // Simulace audio zpracování
    float gainBuffer[256];
    int position = 0;
    
    while (true) {
        bool continues = envelope.getAttackGains(gainBuffer, 256, position);
        
        // Zde by se aplikovaly gains na audio data
        // processAudio(gainBuffer, 256);
        
        position += 256;
        
        if (!continues) {
            std::cout << "Attack phase completed" << std::endl;
            break;
        }
    }
    
    return 0;
}
```

## Závěr

Třída `Envelope` poskytuje robustní a výkonné řešení pro správu ADSR obálek s důrazem na RT-safe operace a bezpečnost. Kritické chyby jsou ošetřeny automatickým ukončením programu, což zajišťuje konzistentní stav aplikace.