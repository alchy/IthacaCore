#ifndef SAMPLER_H       // Include guard pro prevenci duplicitního includu
#define SAMPLER_H

#include <cstdint>      // Pro uint8_t a size_t
#include <cstddef>      // Pro size_t (pokud není v cstdint)
#include <string>       // Pro std::string
#include <vector>       // Pro std::vector
#include <sndfile.h>    // Pro sf_count_t typ

#include "core_logger.h"  // Pro Logger (pro předání reference)

// Struktura pro metadata o samplu (načtená z WAV souboru a názvu)
// Uchovává základní informace pro vyhledávání a správu sample
struct SampleInfo {
    char filename[256];             // Celé jméno souboru (včetně cesty, max 256 znaků)
    uint8_t midi_note;              // MIDI nota (z názvu, rozsah 0-127, ale podporuje až 999)
    uint8_t midi_note_velocity;     // Velocity (z názvu, rozsah 0-7)
    int frequency;                  // Frekvence samplu (načtená z WAV souboru, musí odpovídat názvu)
    sf_count_t sample_count;        // Celkový počet vzorků (frames)
    double duration_seconds;        // Délka v sekundách
    int channels;                   // Počet kanálů (1=mono, 2=stereo, atd.)
    bool is_stereo;                 // True pokud stereo (channels >= 2)
    bool interleaved_format;        // NOVÉ: True pokud je WAV v interleaved formátu (standard)
    bool needs_conversion;          // NOVÉ: True pokud potřebuje konverzi do float (16-bit PCM -> float)
};

// Hlavní třída pro IO operace se WAV samplami
// Centralizuje načítání metadat z adresáře pomocí libsndfile a vyhledávání
class SamplerIO {
public:
    // Konstruktor: Inicializuje prázdný seznam sample
    SamplerIO();

    // Destruktor: Loguje ukončení instance
    ~SamplerIO();

    // REF: Metoda pro prohledání adresáře s WAV soubory a naplnění seznamu metadat
    // Vstup: Cesta k adresáři (string), reference na Logger pro logování
    // Chování: Prochází adresář, parsuje názvy podle patternu mXXX-velY-fZZ.wav,
    // načte freq z WAV headeru pomocí libsndfile; loguje info/warn/error
    // NOVÉ: Detekuje interleaved formát a potřebu konverze do float
    // Validace konzistence: Kontroluje, zda frekvence v názvu odpovídá frekvenci v souboru
    // Při chybě (např. neexistující adresář, nekonzistentní frekvence): Zaloguje error a volá std::exit(1)
    void scanSampleDirectory(const std::string& directoryPath, Logger& logger);

    // REF: Metoda pro vyhledání indexu sample v interním seznamu podle MIDI noty a velocity
    // Vstup: uint8_t midi_note, uint8_t velocity
    // Výstup: Index v seznamu (int, -1 pokud nenalezeno)
    // Chování: Lineární prohledávání, vrací první shodu
    int findSampleInSampleList(uint8_t midi_note, uint8_t velocity) const;

    // REF: Getter pro přístup k načtenému seznamu (volitelný)
    // Vrátí konstantní referenci na vektor SampleInfo pro čtení dat
    const std::vector<SampleInfo>& getLoadedSampleList() const;

    // REF: Gettery pro přístup k metadatům na základě indexu
    // Tyto metody kontrolují platnost indexu; při neplatném: logují error a exit(1)
    // Vstup: int index (musí být >=0 a < velikost seznamu)
    // Výstup: Hodnota z SampleInfo na daném indexu

    // Getter pro název souboru (cesta včetně)
    const char* getFilename(int index, Logger& logger) const;

    // Getter pro MIDI notu
    uint8_t getMidiNote(int index, Logger& logger) const;

    // Getter pro velocity MIDI noty
    uint8_t getMidiNoteVelocity(int index, Logger& logger) const;

    // Getter pro frekvenci samplu (Hz)
    int getFrequency(int index, Logger& logger) const;

    // Rozšířené gettery pro metadata
    
    // Getter pro počet vzorků (frames)
    sf_count_t getSampleCount(int index, Logger& logger) const;

    // Getter pro délku v sekundách (přejmenováno pro jasnost)
    double getDurationInSeconds(int index, Logger& logger) const;

    // Getter pro počet kanálů
    int getChannelCount(int index, Logger& logger) const;

    // Getter pro stereo flag (true pokud channels >= 2)
    bool getIsStereo(int index, Logger& logger) const;

    // NOVÉ gettery pro rozšířené atributy
    
    // Getter pro interleaved formát flag (přejmenováno pro konzistenci s bool konvencí)
    bool getIsInterleavedFormat(int index, Logger& logger) const;

    // Getter pro potřebu konverze do float
    bool getNeedsConversion(int index, Logger& logger) const;

private:
    std::vector<SampleInfo> sampleList;  // Interní seznam načtených sample

    // NOVÉ pomocné metody pro detekci formátu
    
    // Detekce, zda je WAV v interleaved formátu (pro standardní WAV vždy true)
    // Parametry: cesta k souboru, reference na logger
    // Vrácí: true pokud interleaved, false jinak (vzácné případy)
    // Při chybě: loguje error a volá std::exit(1)
    bool detectInterleavedFormat(const char* filename, Logger& logger) const;

    // Detekce subformátu a potřeby konverze do float
    // Parametry: cesta k souboru, reference na logger  
    // Vrácí: true pokud 16-bit PCM (potřebuje konverzi), false pokud již float
    // Při chybě nebo nepodporovaném formátu: loguje error a volá std::exit(1)
    bool detectFloatConversionNeed(const char* filename, Logger& logger) const;
};

// REF: Deklarace funkce pro řízení sampleru
// Volána z main.cpp; obsahuje logiku načítání a vyhledávání s loggerem
// @param logger Reference na Logger pro logování
// @return 0 při úspěchu, 1 při chybě (chyby jsou řešeny ukončením programu)
int runSampler(Logger& logger);

#endif  // SAMPLER_H