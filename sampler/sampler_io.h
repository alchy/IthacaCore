#ifndef SAMPLER_IO_H  // Include guard pro prevenci duplikátního includu
#define SAMPLER_IO_H

#include <cstdint>      // Pro uint8_t a size_t
#include <cstddef>      // Pro size_t (pokud není v cstdint)
#include <string>       // Pro std::string
#include <vector>       // Pro std::vector
#include "core_logger.h"  // Pro Logger

// Struktura pro metadata o samplu (načtená z WAV souboru a názvu)
// Uchovává základní informace pro vyhledávání a správu sample
struct SampleInfo {
    char filename[256];             // Celé jméno souboru (včetně cesty, max 256 znaků)
    uint8_t midi_note;              // MIDI nota (z názvu, rozsah 0-127, ale podporuje až 999)
    uint8_t midi_note_velocity;     // Velocity (z názvu, rozsah 0-7)
    int sample_frequency;           // Frekvence samplu (načtená z WAV souboru)
};

// Hlavní třída pro IO operace se WAV samplami
// Centralizuje načítání metadat z adresáře pomocí libsndfile a vyhledávání
// Používá logger pro všechny IO operace - ŽÁDNÉ výstupy na konzoli
class SamplerIO {
public:
    // Konstruktor: Inicializuje prázdný seznam sample a uloží referenci na logger
    // @param logger Reference na Logger pro logování všech operací
    explicit SamplerIO(Logger& logger);

    // Destruktor: Loguje ukončení instance
    ~SamplerIO();

    // Metoda pro načtení seznamu WAV souborů z adresáře a naplnění seznamu
    // Vstup: Cesta k adresáři (string)
    // Chování: Prochází adresář, parsuje názvy podle patternu mXXX-velY-ZZ.wav,
    // načte freq z WAV headeru pomocí libsndfile; loguje všechny operace
    // DŮLEŽITÉ: Žádné výstupy na konzoli - pouze do logu
    void loadSamples(const std::string& directoryPath);

    // Metoda pro vyhledání sample podle MIDI noty a velocity
    // Vstup: uint8_t midi_note, uint8_t velocity
    // Výstup: Index v seznamu (int, -1 pokud nenalezeno)
    // Chování: Lineární prohledávání, vrací první shodu, loguje operaci
    int findSample(uint8_t midi_note, uint8_t velocity) const;

    // Getter pro přístup k seznamu (volitelný)
    // Vrátí konstantní referenci na vektor SampleInfo pro čtení dat
    // Loguje informaci o počtu samples
    const std::vector<SampleInfo>& getSampleList() const;

private:
    std::vector<SampleInfo> sampleList;  // Interní seznam načtených sample
    Logger& logger_;                     // Reference na logger pro všechny operace
};

#endif  // SAMPLER_IO_H