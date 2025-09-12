/*
THIS FILE IS LOCKED, IT IS FUNCTIONAL AND WILL NOT BE CHANGED
*/

#ifndef INSTRUMENT_LOADER_H  // Include guard pro prevenci duplikátního includu
#define INSTRUMENT_LOADER_H

#include <cstdint>      // Pro uint8_t
#include <sndfile.h>    // Pro SNDFILE*, SF_INFO, int
#include "sampler.h"    // Pro SamplerIO, SampleInfo, Logger
#include "core_logger.h" // Pro Logger (explicitní include pro jasnost)

// Globální konstanty pro MIDI rozsah
#define MIDI_NOTE_MIN 0
#define MIDI_NOTE_MAX 127
#define VELOCITY_LAYERS 8  // Velocity 0-7

/**
 * @struct Instrument
 * @brief Reprezentuje jeden MIDI note (index 0-127) s vrstvami pro velocity 0-7.
 * 
 * Struktura uchovává pointery na metadata (SampleInfo) a načtená float data (RAM buffery)
 * pro všechny velocity vrstvy jedné MIDI noty. Index v poli instruments[] odpovídá MIDI notě,
 * takže není potřeba ukládat midi_note hodnotu v struktuře.
 * 
 * DŮLEŽITÉ: Všechny buffery jsou VÝDY uloženy jako stereo interleaved 32-bit float formát!
 * I původně mono samples jsou konvertovány na stereo (L=R duplikace).
 * Buffer formát: [L1,R1,L2,R2,L3,R3,...] pro přímou JUCE kompatibilitu.
 */
struct Instrument {
    // Pointery na SampleInfo struktury z SamplerIO pro velocity 0-7
    // Tyto pointery odkazují na data vlastněná SamplerIO (nesmí se uvolňovat)
    // POZOR: SampleInfo.channels obsahuje PŮVODNÍ počet kanálů (1=mono, 2=stereo)
    SampleInfo* sample_ptr_sampleInfo[VELOCITY_LAYERS];
    
    // Pointery na načtená float data v RAM pro velocity 0-7
    // Buffery jsou alokované pomocí malloc a obsahují VÝDY stereo interleaved 32-bit float
    // Velikost: frame_count_stereo * 2 * sizeof(float) (vždy stereo)
    // Formát: [L1,R1,L2,R2,...] i pro původně mono samples (L=R)
    float* sample_ptr_velocity[VELOCITY_LAYERS];
    
    // Indikátory existence samplu pro velocity 0-7
    // true = sample byl nalezen a načten jako stereo, false = sample neexistuje
    bool velocityExists[VELOCITY_LAYERS];
    
    // NOVÉ METADATA - stereo informace (nezávislé na SampleInfo)
    // Počet stereo frame párů (každý frame = L+R sample)
    int frame_count_stereo[VELOCITY_LAYERS];
    
    // Celkový počet float hodnot v bufferu (frame_count_stereo * 2)
    int total_samples_stereo[VELOCITY_LAYERS];
    
    // Indikátor původního formátu před konverzí (true = byl mono, false = byl stereo)
    bool was_originally_mono[VELOCITY_LAYERS];
    
    /**
     * @brief Konstruktor - inicializuje všechny pointery na nullptr a flags na false
     */
    Instrument() {
        for (int i = 0; i < VELOCITY_LAYERS; i++) {
            sample_ptr_sampleInfo[i] = nullptr;
            sample_ptr_velocity[i] = nullptr;
            velocityExists[i] = false;
            frame_count_stereo[i] = 0;
            total_samples_stereo[i] = 0;
            was_originally_mono[i] = false;
        }
    }
    
    /**
     * @brief Getter pro začátek stereo bufferu
     * @param velocity Velocity vrstva (0-7)
     * @return Pointer na začátek stereo float dat [L1,R1,L2,R2,...]
     * Při neplatném velocity nebo neexistujícím samplu: nullptr
     */
    float* get_sample_begin_pointer(uint8_t velocity) const {
        if (velocity >= VELOCITY_LAYERS || !velocityExists[velocity]) {
            return nullptr;
        }
        return sample_ptr_velocity[velocity];
    }
    
    /**
     * @brief Getter pro počet stereo frame párů
     * @param velocity Velocity vrstva (0-7)
     * @return Počet stereo frame párů (každý frame = L+R sample)
     * Při neplatném velocity nebo neexistujícím samplu: 0
     */
    int get_frame_count(uint8_t velocity) const {
        if (velocity >= VELOCITY_LAYERS || !velocityExists[velocity]) {
            return 0;
        }
        return frame_count_stereo[velocity];
    }
    
    /**
     * @brief Getter pro celkový počet float hodnot
     * @param velocity Velocity vrstva (0-7)
     * @return Celkový počet float hodnot v bufferu (frame_count * 2)
     * Při neplatném velocity nebo neexistujícím samplu: 0
     */
    int get_total_sample_count(uint8_t velocity) const {
        if (velocity >= VELOCITY_LAYERS || !velocityExists[velocity]) {
            return 0;
        }
        return total_samples_stereo[velocity];
    }
    
    /**
     * @brief Getter pro informaci o původním formátu
     * @param velocity Velocity vrstva (0-7)
     * @return true pokud byl původně mono (před konverzí), false pokud stereo
     * Při neplatném velocity nebo neexistujícím samplu: false
     */
    bool get_was_originally_mono(uint8_t velocity) const {
        if (velocity >= VELOCITY_LAYERS || !velocityExists[velocity]) {
            return false;
        }
        return was_originally_mono[velocity];
    }
};

/**
 * @class InstrumentLoader
 * @brief Centralizuje načítání WAV samplů z SamplerIO do paměti jako 32-bit float buffery.
 * 
 * Třída automaticky konvertuje z PCM na float (podle getNeedsConversion) a na interleaved
 * formát (podle getIsInterleavedFormat), s kompletním logováním všech konverzí.
 * Načítá pouze samples s požadovanou frekvencí vzorkování (targetSampleRate).
 * 
 * Inicializuje struktury pro všechny MIDI noty (0-127) na základě výstupu scanSampleDirectory,
 * včetně not bez samplů (null pointery + warning log).
 * 
 * PAMĚŤOVÁ SPRÁVA:
 * - Alokuje buffery pomocí malloc (C-style pro konzistenci)
 * - Automaticky uvolňuje při novém načítání nebo v destruktoru
 * - SampleInfo pointery patří SamplerIO (neuvolňují se)
 * 
 * BEZPEČNOST:
 * - Všechny gettery kontrolují rozsah indexů a inicializaci
 * - Chyby vedou k error logu a std::exit(1)
 * - Thread-safety není implementována (single-threaded design)
 * 
 * NOVÝ DESIGN:
 * - Prázdný konstruktor, data se načítají pomocí loadInstrumentData()
 * - Možnost opakovaného načítání s různými targetSampleRate
 * - Automatické vyčištění při novém načítání
 */
class InstrumentLoader {
public:
    /**
     * @brief Prázdný konstruktor InstrumentLoader
     * 
     * Inicializuje pole instruments[128] na nullptr/false.
     * Nastaví actual_samplerate_ na 0 (indikuje neinicializovaný stav).
     * Pro načítání dat je třeba zavolat loadInstrumentData().
     */
    InstrumentLoader();

    /**
     * @brief Destruktor - uvolní alokovanou paměť
     * 
     * Prochází všechny MIDI noty 0-127 a velocity 0-7:
     * - Pokud velocityExists[vel] == true, uvolní free(sample_ptr_velocity[vel])
     * - NEUVOLŇUJE SampleInfo pointery (patří SamplerIO)
     * - Zaloguje počet uvolněných samplů (pokud je logger dostupný)
     */
    ~InstrumentLoader();

    /**
     * @brief Hlavní metoda pro načítání dat instrumentů
     * @param sampler Reference na SamplerIO pro přístup k sample metadatům
     * @param targetSampleRate Požadovaná frekvence vzorkování (44100 nebo 48000 Hz)
     * @param logger Reference na Logger pro zaznamenávání
     * 
     * Validuje parametry:
     * - Kontroluje platnost reference na sampler (není nullptr)
     * - Kontroluje targetSampleRate (povoleno jen 44100 nebo 48000)
     * - Při chybě loguje error a volá std::exit(1)
     * 
     * Pokud už jsou data načtená, automaticky je vyčistí voláním clear().
     * Poté načte všechny MIDI noty 0-127 stejným algoritmem jako původní loadInstrument().
     * 
     * Algoritmus:
     * 1. Validace parametrů
     * 2. Automatické vyčištění předchozích dat (pokud existují)
     * 3. Pro každou MIDI notu (MIDI_NOTE_MIN to MIDI_NOTE_MAX)
     * 4. Pro každou velocity 0-7:
     *    a) Vyhledá index = sampler.findSampleInSampleList(midi, vel, targetSampleRate)
     *    b) Pokud index != -1:
     *       - Zavolá loadSampleToBuffer(index, vel, midi) pro načtení
     *       - Nastaví instruments[midi].velocityExists[vel] = true
     *    c) Pokud nenalezeno:
     *       - Nastaví null pointery, velocityExists[vel] = false
     *       - Warning log "Sample pro MIDI [midi] velocity [vel] nenalezen"
     * 
     * Loguje progress info a summary na konci.
     * Uloží targetSampleRate do actual_samplerate_.
     */
    void loadInstrumentData(SamplerIO& sampler, int targetSampleRate, Logger& logger);

    /**
     * @brief Getter pro přístup k Instrument struktuře podle MIDI noty
     * @param midi_note MIDI nota (0-127)
     * @return Reference na Instrument strukturu
     * 
     * Kontroluje inicializaci (actual_samplerate_ != 0).
     * Kontroluje platnost rozsahu MIDI noty.
     * Při neinicializaci nebo neplatném rozsahu: log error a std::exit(1).
     */
    Instrument& getInstrumentNote(uint8_t midi_note);

    /**
     * @brief Const getter pro read-only přístup k Instrument struktuře
     * @param midi_note MIDI nota (0-127)
     * @return Const reference na Instrument strukturu
     */
    const Instrument& getInstrumentNote(uint8_t midi_note) const;

    /**
     * @brief Getter pro získání aktuální sample rate
     * @return Nastavenou actual sample rate v Hz (0 = neinicializováno)
     */
    int getActualSampleRate() const { return actual_samplerate_; }

    /**
     * @brief Getter pro celkový počet načtených samplů
     * @return Počet úspěšně načtených samplů
     * Kontroluje inicializaci - při neinicializaci loguje error a volá std::exit(1).
     */
    int getTotalLoadedSamples() const;

    /**
     * @brief Getter pro počet mono samplů
     * @return Počet načtených mono samplů
     * Kontroluje inicializaci - při neinicializaci loguje error a volá std::exit(1).
     */
    int getMonoSamplesCount() const;

    /**
     * @brief Getter pro počet stereo samplů  
     * @return Počet načtených stereo samplů
     * Kontroluje inicializaci - při neinicializaci loguje error a volá std::exit(1).
     */
    int getStereoSamplesCount() const;

    /**
     * @brief Validace, že všechny načtené buffery jsou skutečně stereo
     * Kontroluje konzistenci mezi metadaty a skutečnými buffery.
     * Při selhání validace: zaloguje chyby a volá std::exit(1).
     * Kontroluje inicializaci - při neinicializaci loguje error a volá std::exit(1).
     */
    void validateStereoConsistency(Logger& logger);

private:
    // Aktuální frekvence vzorkování (0 = neinicializováno)
    int actual_samplerate_;
    
    // Pointery na SamplerIO a Logger (uložené z posledního volání loadInstrumentData)
    SamplerIO* sampler_;
    Logger* logger_;
    
    // Pole Instrument struktur pro MIDI noty 0-127
    // Index pole odpovídá MIDI notě
    Instrument instruments_[MIDI_NOTE_MAX + 1];
    
    // Počítadlo úspěšně načtených samplů
    int totalLoadedSamples_;
    
    // Počítadla mono/stereo samplů pro diagnostiku
    int monoSamplesCount_;
    int stereoSamplesCount_;

    /**
     * @brief Vyčistí všechna načtená data
     * @param logger Reference na Logger pro zaznamenávání
     * 
     * Uvolní všechny alokované float buffery pomocí free().
     * Vynuluje všechny pointery a počítadla.
     * Resetuje actual_samplerate_ na 0.
     * Loguje informace o uvolňování.
     * 
     * Volá se automaticky při novém načítání.
     */
    void clear(Logger& logger);

    /**
     * @brief Vyčistí všechna načtená data bez loggingu
     * 
     * Stejná funkcionalita jako clear(), ale bez loggingu.
     * Používá se v destruktoru kde logger nemusí být dostupný.
     */
    void clearWithoutLogging();

    /**
     * @brief Načte jeden sample do bufferu
     * @param sampleIndex Index samplu v SamplerIO
     * @param velocity Velocity vrstva (0-7)
     * @param midi_note MIDI nota (0-127)
     * @param logger Reference na Logger pro zaznamenávání
     * @return true při úspěchu, false při chybě (s error logem a exit)
     * 
     * Algoritmus stejný jako v původní verzi:
     * 1. Otevře soubor pomocí openSampleFile()
     * 2. Alokuje temporary buffer pro načtení
     * 3. Načte data pomocí sf_readf_float (automatická PCM->float konverze)
     * 4. Alokuje permanent buffer pro interleaved data
     * 5. Zkopíruje/konvertuje na interleaved formát (pokud potřeba)
     * 6. Uvolní temporary buffer
     * 7. Přiřadí permanent buffer a SampleInfo pointer do instruments_
     * 
     * Loguje všechny konverze (i když neproběhly) a úspěšné přiřazení.
     * Při chybě alokace/načtení: log error a std::exit(1).
     */
    bool loadSampleToBuffer(int sampleIndex, uint8_t velocity, uint8_t midi_note, Logger& logger);

    /**
     * @brief Otevře sample soubor pro čtení
     * @param sampleIndex Index samplu v SamplerIO
     * @param sndfile Reference na SNDFILE pointer (výstup)
     * @param sfinfo Reference na SF_INFO strukturu (výstup)
     * @param logger Reference na Logger pro zaznamenávání
     * @return true při úspěchu, false při chybě (s error logem a exit)
     * 
     * Použije getFilename(sampleIndex) pro získání cesty.
     * Otevře soubor pomocí sf_open() v SFM_READ módu.
     * Při úspěchu: log info "Soubor [filename] otevřen úspěšně"
     * Při chybě: log error s sf_strerror() a std::exit(1)
     */
    bool openSampleFile(int sampleIndex, SNDFILE*& sndfile, SF_INFO& sfinfo, Logger& logger);

    /**
     * @brief Validuje velocity parametr
     * @param velocity Velocity hodnota k validaci
     * @param functionName Název funkce pro error log
     * @param logger Reference na Logger pro error log
     * 
     * Kontroluje rozsah 0-7. Při neplatné hodnotě: log error a std::exit(1).
     */
    void validateVelocity(uint8_t velocity, const char* functionName, Logger& logger) const;

    /**
     * @brief Validuje MIDI note parametr
     * @param midi_note MIDI nota k validaci
     * @param functionName Název funkce pro error log
     * @param logger Reference na Logger pro error log
     * 
     * Kontroluje rozsah MIDI_NOTE_MIN-MIDI_NOTE_MAX. Při neplatné hodnotě: log error a std::exit(1).
     */
    void validateMidiNote(uint8_t midi_note, const char* functionName, Logger& logger) const;

    /**
     * @brief Kontroluje inicializaci objektu
     * @param functionName Název funkce pro error log
     * 
     * Kontroluje actual_samplerate_ != 0. Při neinicializaci: log error a std::exit(1).
     */
    void checkInitialization(const char* functionName) const;

    /**
     * @brief Validuje referenci na SamplerIO
     * @param sampler Reference na SamplerIO k validaci
     * @param logger Reference na Logger pro error log
     * 
     * Kontroluje, že reference není nullptr (teoreticky nemožné u referencí,
     * ale pro jistotu). Při chybě: log error a std::exit(1).
     */
    void validateSamplerReference(SamplerIO& sampler, Logger& logger);

    /**
     * @brief Validuje targetSampleRate
     * @param targetSampleRate Sample rate k validaci
     * @param logger Reference na Logger pro error log
     * 
     * Kontroluje, že je 44100 nebo 48000. Při chybě: log error a std::exit(1).
     */
    void validateTargetSampleRate(int targetSampleRate, Logger& logger);
};

#endif  // INSTRUMENT_LOADER_H