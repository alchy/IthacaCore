### README.md pro třídu Envelope (Dynamické generování) - Voice integrace

#### Úvod
Třída `Envelope` slouží k RT-safe (real-time safe) vyplnění bufferu gainy pro ADSR obálky (attack a release) v audio sampleru. **Data se generují dynamicky za běhu** místo načítání z pre-computed arrays, což šetří paměť a velikost binárního souboru. Třída podporuje dynamickou změnu sample rate (44100/48000 Hz) a efektivní výpočet exponenciálních křivek v reálném čase. Logování probíhá v konstruktoru a při změně frekvence. RT-safe: Žádné alokace v runtime metodách.

Třída se integruje do `VoiceManager` (instanciace) a `Voice` (volání v `calculateBlockGains` pro nahrazení lineárního release).

#### Tabulka metod třídy Envelope

| Metoda                          | Popis                                                                 | Parametry                                                                 | Návratový typ |
|---------------------------------|-----------------------------------------------------------------------|---------------------------------------------------------------------------|---------------|
| `Envelope(Logger& logger)`      | Konstruktor: Inicializuje parametry pro dynamické generování attack/release dat. Loguje informace o připravenosti (128 MIDI, podporované rates). Nenastavuje frekvenci. | `logger`: Reference na Logger pro logování.                               | `void` (konstruktor) |
| `void setEnvelopeFrequency(int freq, Logger& logger)` | Nastaví frekvenci (bitrate) a odpovídající index (0/1), loguje změnu bitrate a index. Validuje jen 44100/48000 Hz, jinak error a exit(1). Nastaví private `bitrate_` a `sample_rate_index_`. | `freq`: Frekvence v Hz (44100 nebo 48000).<br>`logger`: Reference na Logger pro logování. | `void` |
| `void getGainBufferAttack(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed, Logger& logger) const noexcept` | Dynamicky vypočítá a vyplní `gainBuffer` gainy pro attack fázi s debug logováním: Exponenciální křivka `1 - exp(-t/tau)`. Pro MIDI 0: Konstantní 1.0f. Automaticky loguje na úrovni "debug". | `midi`: MIDI hodnota (0-127).<br>`gainBuffer`: Buffer k vyplnění.<br>`numSamples`: Počet samples (např. 512).<br>`start_elapsed`: Start samples od začátku attack.<br>`logger`: Reference na Logger pro debug výstup. | `void` |
| `void getGainBufferRelease(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed, Logger& logger) const noexcept` | Dynamicky vypočítá a vyplní `gainBuffer` gainy pro release fázi s debug logováním: Exponenciální křivka `exp(-t/tau)`. Pro MIDI 0: Konstantní 0.0f. Automaticky loguje na úrovni "debug". | `midi`: MIDI hodnota (0-127).<br>`gainBuffer`: Buffer k vyplnění.<br>`numSamples`: Počet samples (např. 512).<br>`start_elapsed`: Start samples od začátku release.<br>`logger`: Reference na Logger pro debug výstup. | `void` |

#### Voice integrace - Refaktorované použití

**Konstanty pro envelope MIDI hodnoty:**
```cpp
// V Voice třídě - fixní MIDI hodnoty pro obálky
static constexpr uint8_t ATTACK_ENVELOPE_MIDI = 8;   // Rychlejší attack
static constexpr uint8_t RELEASE_ENVELOPE_MIDI = 64; // Střední release
static constexpr float SUSTAIN_LEVEL = 1.0f;         // Fixní sustain level
```

**Refaktorované calculateBlockGains:**
```cpp
bool Voice::calculateBlockGains(float* gainBuffer, int numSamples, Logger& logger) noexcept {
    if (state_ == VoiceState::Idle || !gainBuffer || numSamples <= 0) {
        return false;
    }
    
    switch (state_) {
        case VoiceState::Attacking: {
            // NOVÉ: Používá fixní MIDI 8 pro attack envelope
            envelope_->getGainBufferAttack(ATTACK_ENVELOPE_MIDI, gainBuffer, numSamples, 
                                         envelope_attack_position_, logger);
            
            envelope_attack_position_ += numSamples;
            envelope_gain_ = gainBuffer[numSamples - 1];
            
            // Zkontroluj dokončení attack
            if (envelope_gain_ >= 0.99f) {
                state_ = VoiceState::Sustaining;
            }
            return true;
        }
        
        case VoiceState::Sustaining: {
            // NOVÉ: Fixní sustain level místo envelope lookup
            for (int i = 0; i < numSamples; ++i) {
                gainBuffer[i] = SUSTAIN_LEVEL;
            }
            envelope_gain_ = SUSTAIN_LEVEL;
            return true;
        }
        
        case VoiceState::Releasing: {
            // NOVÉ: Používá fixní MIDI 64 pro release envelope
            envelope_->getGainBufferRelease(RELEASE_ENVELOPE_MIDI, gainBuffer, numSamples, 
                                          envelope_release_position_, logger);
            
            envelope_release_position_ += numSamples;
            envelope_gain_ = gainBuffer[numSamples - 1];
            
            // Zkontroluj dokončení release
            if (envelope_gain_ <= 0.001f) {
                state_ = VoiceState::Idle;
                return false;
            }
            return true;
        }
        
        default:
            return false;
    }
}
```

#### VoiceManager integrace

**Konstruktor:**
```cpp
VoiceManager::VoiceManager(const std::string& sampleDir, Logger& logger)
    : envelope_(logger),  // Stack allocated s logger referencí
      currentSampleRate_(0),
      // ... ostatní inicializace
{
    // Envelope je nyní inicializovaná v konstruktoru
}
```

**Initialize metoda:**
```cpp
void Voice::initialize(const Instrument& instrument, int sampleRate, 
                      const Envelope& envelope, Logger& logger) {
    // ... existující kód ...
    envelope_ = &envelope;  // Uložit referenci na envelope
    
    logger.log("Voice/initialize", "info", 
               "Voice initialized for MIDI " + std::to_string(midiNote_) + 
               " with dynamic envelope system (Attack MIDI: " + std::to_string(ATTACK_ENVELOPE_MIDI) + 
               ", Release MIDI: " + std::to_string(RELEASE_ENVELOPE_MIDI) + ")");
}
```

#### Logger API integrace

Všechny Voice metody které volají envelope nyní předávají logger:

```cpp
// V Voice::calculateBlockGains() - s předáním loggeru
bool Voice::calculateBlockGains(float* gainBuffer, int numSamples, Logger& logger) noexcept {
    // ... volání envelope s logger parametrem
}

// V Voice::processBlock() - aktualizovaný podpis
bool Voice::processBlock(float* outputLeft, float* outputRight, int samplesPerBlock, Logger& logger) noexcept {
    // Pre-calculate envelope gains for the block
    if (!calculateBlockGains(gainBuffer_.data(), samplesToProcess, logger)) {
        state_ = VoiceState::Idle;
        return false;
    }
    // ... zbytek kódu
}
```

#### Výhody refaktoringu

- **Jednoduché fixní envelope parametry**: Žádné složité mapování MIDI not na envelope
- **Konzistentní chování**: Všechny voices používají stejné envelope křivky
- **RT-safe s logováním**: Debug logy pro monitorování envelope behavior
- **Flexibilní budoucnost**: Snadné rozšíření pro dynamické envelope MIDI hodnoty

#### Matematický model

- **Attack envelope**: MIDI 8 → rychlejší attack (tau = 8/127 * 12/5 ≈ 0.15s)
- **Release envelope**: MIDI 64 → střední release (tau = 64/127 * 12/5 ≈ 1.2s)  
- **Sustain level**: Konstantní 1.0f (100% hlasitost)

Tento přístup poskytuje předvídatelné envelope chování s možností budoucího rozšíření pro per-note envelope control.