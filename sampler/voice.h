 /*
 THIS FILE IS LOCKED, IT IS FUNCTIONAL AND WILL NOT BE CHANGED
 EXCEPT FOR REQUIRED UPDATES LIKE SAMPLE RATE PROPAGATION
 */

 #ifndef VOICE_H
 #define VOICE_H

 #include <cstdint>      // Pro uint8_t
 #include "instrument_loader.h"  // Pro Instrument a Logger
 #include "core_logger.h"        // Pro Logger

 // Simulace JUCE AudioBuffer pro stereo výstup (bez závislosti na JUCE)
 struct AudioData {
     float left;
     float right;
     AudioData() : left(0.0f), right(0.0f) {}
 };

 /**
  * @enum VoiceState
  * @brief Stavy voice pro řízení lifecycle (idle, attacking, sustaining, releasing).
  * Používá se pro aplikaci obálky (gain) a rozhodnutí o ukončení voice.
  */
 enum class VoiceState {
     Idle,       // Voice neaktivní, žádná note
     Attacking,  // Začátek note, attack fáze (zde zjednodušená na okamžitý start)
     Sustaining, // Note aktivní, sustain fáze (konstantní gain = 1.0)
     Releasing   // Note-off, release fáze (lineární útlum gainu na 0)
 };

 /**
  * @class Voice
  * @brief Jedna hlasová jednotka pro přehrávání sample s obálkou a stavy.
  * 
  * Spravuje MIDI notu, pointer na Instrument (pro přístup k stereo bufferům),
  * pozici ve sample, stav a obálku (jednoduchá: attack okamžitý, sustain konstantní,
  * release lineární 200 ms). Podporuje polyfonii přes VoiceManager.
  * 
  * NOVÉ: Přidán sampleRate pro výpočty obálky (např. releaseSamples = 0.2 * sampleRate).
  * 
  * Klíčové vlastnosti:
  * - Stereo výstup: Používá interleaved buffer z Instrument [L,R,L,R...].
  * - Obálka: Aplikuje gain v processBlock.
  * - Stavy: Řízení přes setNoteState (start/stop note).
  * - Simulace JUCE: processBlock zapisuje do AudioBuffer-like struktury.
  * 
  * PŘÍKLAD POUŽITÍ:
  * Voice voice(60, instrument, 44100, logger);
  * voice.setNoteState(true, 100);  // Start note
  * AudioData data; voice.getCurrentAudioData(data);  // Získání sample s gainem
  */
 class Voice {
 public:
     /**
      * @brief Default konstruktor pro pool v VoiceManager.
      * Inicializuje idle stav, midiNote = 0, sampleRate_ = 0 (musí být inicializován).
      */
     Voice();

     /**
      * @brief Konstruktor pro VoiceManager: Nastaví MIDI notu, logger; instrument později přes initialize.
      * @param midiNote MIDI nota (0-127).
      * @param logger Reference na Logger.
      */
     Voice(uint8_t midiNote, Logger& logger);

     /**
      * @brief Plný konstruktor: Inicializuje s instrumentem, sample rate a loggerem.
      * @param midiNote MIDI nota (0-127).
      * @param instrument Reference na Instrument (pro buffery).
      * @param sampleRate Frekvence vzorkování (např. 44100 Hz) pro obálku.
      * @param logger Reference na Logger.
      */
     Voice(uint8_t midiNote, const Instrument& instrument, int sampleRate, Logger& logger);

     /**
      * @brief Inicializuje voice s instrumentem a sample rate (pro pool v VoiceManager).
      * Uloží sample rate, validuje ho (>0), nastaví stav na idle.
      * @param instrument Reference na Instrument.
      * @param sampleRate Frekvence vzorkování (např. 44100 Hz).
      * @param logger Reference na Logger.
      */
     void initialize(const Instrument& instrument, int sampleRate, Logger& logger);

     /**
      * @brief Cleanup: Reset na idle stav, uvolní buffery (pokud potřeba, ale zde jen reset).
      */
     void cleanup();

     /**
      * @brief Reinicializuje s novým instrumentem a sample rate.
      * @param instrument Nový Instrument.
      * @param sampleRate Nový sample rate.
      * @param logger Logger.
      */
     void reinitialize(const Instrument& instrument, int sampleRate, Logger& logger);

     /**
      * @brief Nastaví stav note: true = startNote (attack/sustain), false = stopNote (release).
      * Při start: Nastaví attacking -> sustaining, vybere velocity layer (default 0, nebo podle param).
      * Při stop: Přejde do releasing, spustí release timer.
      * @param isOn True pro start, false pro stop.
      * @param velocity Velocity (0-127, mapováno na layer 0-7).
      */
     void setNoteState(bool isOn, uint8_t velocity = 0);

     /**
      * @brief Posune pozici ve sample o 1 frame (pro non-real-time processing).
      */
     void advancePosition();

     /**
      * @brief Získá aktuální stereo audio data s aplikovanou obálkou (gain).
      * @param data Reference na AudioData pro výstup (left, right).
      * @return True, pokud data jsou platná (voice aktivní).
      */
     bool getCurrentAudioData(AudioData& data) const;

     /**
      * @brief Hlavní metoda: Zpracuje audio blok (např. 512 samples), aplikuje obálku, zapisuje do bufferu.
      * Používá sampleRate_ pro výpočet release (lineární: gain = 1.0 - (position / releaseSamples)).
      * @param outputBuffer Simulovaný AudioBuffer pro výstup (stereo).
      * @param numSamples Počet samples k zpracování.
      * @return True, pokud voice je stále aktivní (pro VoiceManager).
      */
     bool processBlock(/* AudioBuffer& outputBuffer, int numSamples */);  // Placeholder - upravit podle potřeby

     // Gettery
     uint8_t getMidiNote() const { return midiNote_; }
     bool isActive() const;  // True pokud !Idle

 private:
     uint8_t midiNote_;              // MIDI nota (0-127)
     const Instrument* instrument_;  // Pointer na Instrument (nevolnit, patří Loader)
     int sampleRate_;                // NOVÉ: Frekvence vzorkování pro obálku (Hz)
     Logger* logger_;                // Pointer na Logger
     VoiceState state_;              // Aktuální stav
     sf_count_t position_;           // Pozice ve sample (frames)
     uint8_t currentVelocityLayer_;  // Aktuální velocity layer (0-7)
     float gain_;                    // Aktuální gain obálky (0.0-1.0)
     sf_count_t releaseStartPosition_; // Pozice startu release pro lineární útlum
     sf_count_t releaseSamples_;     // Délka release v samplech (např. 0.2 * sampleRate)

     // Pomocné: Výpočet releaseSamples na základě sampleRate (200 ms release)
     void calculateReleaseSamples();

     // Pomocné: Aplikovat gain na stereo sample
     void applyEnvelope(float& left, float& right) const;
 };

 #endif // VOICE_H