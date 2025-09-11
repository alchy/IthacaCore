### README.md pro třídu Envelope

#### Úvod
Třída `Envelope` slouží k RT-safe (real-time safe) vyplnění bufferu gainy pro ADSR obálky (attack a release) v audio sampleru. Data jsou načtena z pre-computed const arrays v `envelopes.h` (generováno Pythonem). Třída podporuje dynamickou změnu sample rate (44100/48000 Hz) a přímou absolutní indexaci do polí pro rychlý lookup. Logování probíhá v konstruktoru a při změně frekvence. RT-safe: Žádné alokace nebo složité výpočty v runtime metodách.

Třída se integruje do `VoiceManager` (instanciace) a `Voice` (volání v `calculateBlockGains` pro nahrazení lineárního release).

#### Tabulka metod třídy Envelope

| Metoda                          | Popis                                                                 | Parametry                                                                 | Návratový typ |
|---------------------------------|-----------------------------------------------------------------------|---------------------------------------------------------------------------|---------------|
| `Envelope(Logger& logger)`      | Konstruktor: Načte data pro attack/release z envelopes.h, loguje informace o načtených datech (128 MIDI, max len pro rates), validuje délku pro MIDI 127. Nenastavuje frekvenci. | `logger`: Reference na Logger pro logování.                               | `void` (konstruktor) |
| `void setEnvelopeFrequency(int freq, Logger& logger)` | Nastaví frekvenci (bitrate) a odpovídající index (0/1), loguje změnu bitrate a index. Validuje jen 44100/48000 Hz, jinak error a exit(1). Nastaví private `bitrate_` a `sample_rate_index_`. | `freq`: Frekvence v Hz (44100 nebo 48000).<br>`logger`: Reference na Logger pro logování. | `void` |
| `void getGainBufferAttack(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed) const noexcept` | Vyplní `gainBuffer` gainy pro attack fázi: Přímá indexace z attack_data_[midi], clamp na 0..len-1. Pro MIDI 0 (len=1): Vyplní 1.0f. Fallback na 0.0f při chybě. | `midi`: MIDI hodnota (0-127).<br>`gainBuffer`: Buffer k vyplnění.<br>`numSamples`: Počet samples (např. 512).<br>`start_elapsed`: Start samples od začátku attack. | `void` |
| `void getGainBufferRelease(uint8_t midi, float* gainBuffer, int numSamples, sf_count_t start_elapsed) const noexcept` | Vyplní `gainBuffer` gainy pro release fázi: Přímá indexace z release_data_[midi], clamp na 0..len-1. Pro MIDI 0 (len=1): Vyplní 0.0f. Fallback na 0.0f při chybě. | `midi`: MIDI hodnota (0-127).<br>`gainBuffer`: Buffer k vyplnění.<br>`numSamples`: Počet samples (např. 512).<br>`start_elapsed`: Start samples od začátku release. | `void` |

#### Příklady volání

##### 1. Instanciace v VoiceManager (v konstruktoru nebo initializeAll)
```cpp
// V voice_manager.cpp, v konstruktoru VoiceManager
Envelope::Envelope(Logger& logger) : envelope_(logger) {  // envelope_ je člen třídy VoiceManager
    // Log: "Loaded attack/release data: 128 MIDI layers, max len 576001 samples for 44100 Hz and 48000 Hz. Frequency not set (bitrate=0, index=-1)."
    // Log: "Data validation OK - ready for setEnvelopeFrequency"
}

// Pak nastav frekvenci
envelope_.setEnvelopeFrequency(sample_rate_, logger);  // Např. sample_rate_ = 44100
// Log: "Bitrate changed to 44100 Hz (index=0)"
```

##### 2. Volání v programu (v calculateBlockGains Voice třídy)
Předpokládáme, že `Envelope& envelope_` je předán do Voice z VoiceManager (např. v konstruktoru Voice: `Voice(..., Envelope& envelope) : envelope_(envelope) {}`).

```cpp
// Upravená metoda v voice.cpp (nahrazuje lineární release)
bool Voice::calculateBlockGains(float* gainBuffer, int numSamples) noexcept {
    if (state_ == VoiceState::Idle || !gainBuffer || numSamples <= 0) {
        return false;
    }
    
    switch (state_) {
        case VoiceState::Sustaining:
        case VoiceState::Attacking: {
            // Constant gain for sustain/attack (nebo uprav pro attack envelope)
            const float constantGain = envelope_gain_;
            for (int i = 0; i < numSamples; ++i) {
                gainBuffer[i] = constantGain;
            }
            return true;
        }
        
        case VoiceState::Releasing: {
            // NOVÉ: Načti envelope gain buffer pro celý blok (absolutní indexace, MIDI parametr)
            const sf_count_t releaseElapsed = position_ - releaseStartPosition_;
            envelope_.getGainBufferRelease(release_midi_, gainBuffer, numSamples, releaseElapsed);  // release_midi_ = např. 64
            
            // Aplikovat targetGain min (0.001f) na konec, pokud potřeba
            constexpr float targetGain = 0.001f;
            for (int i = 0; i < numSamples; ++i) {
                gainBuffer[i] = std::max(gainBuffer[i], targetGain);
            }
            
            // Update envelope_gain_ to last calculated value for getter consistency
            envelope_gain_ = gainBuffer[numSamples - 1];
            
            // Check if release is complete
            return envelope_gain_ > targetGain * 1.1f;
        }
        
        default:
            return false;
    }
}
```

#### Postup změn ve stávajícím kódu
1. **Přidání souborů**: Zkopíruj `envelope.h` a `envelope.cpp` do složky `sampler/`. Přidej do `CMakeLists.txt`: `sampler/envelope.cpp sampler/envelope.h`.
2. **Generování envelopes.h**: Spusť Python `generate_envelopes.py` pro vytvoření `envelopes.h` (include do envelope.h).
3. **Integrace do VoiceManager**: V `voice_manager.h` přidej člen `Envelope envelope_;`. V konstruktoru: `envelope_(logger);`. V `initializeAll`: `envelope_.setEnvelopeFrequency(sample_rate_, logger);`.
4. **Integrace do Voice**: V `voice.h` přidej `Envelope& envelope_;` (reference). V konstruktoru Voice: `Voice(..., Envelope& envelope) : envelope_(envelope) {}`. V `VoiceManager::setNoteState`: Předej envelope do voice.
5. **Úprava calculateBlockGains**: Nahraď lineární release voláním `getGainBufferRelease` (viz příklad výše). Pro attack: Nahraď constant voláním `getGainBufferAttack`, pokud chceš exponenciální nárůst.
6. **Build a test**: Sestav projekt (`cmake --build build`), spusť a sleduj logy v `core_logger/core_logger.log` (např. "Loaded attack/release data...").

Toto umožní snadnou integraci bez velkých změn. Pokud potřebuješ upravit pro decay/sustain, navrhni.