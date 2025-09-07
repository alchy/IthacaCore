#include "instrument_loader.h"
#include <cstdlib>      // Pro malloc, free, std::exit
#include <cstring>      // Pro memcpy
#include <sndfile.h>    // Pro sf_open, sf_readf_float, sf_close

/**
 * @brief Konstruktor InstrumentLoader
 * Inicializuje všechny reference a pole instruments na výchozí hodnoty.
 * @param sampler Reference na SamplerIO pro přístup k sample metadatům
 * @param targetSampleRate Požadovaná frekvence vzorkování (např. 44100 Hz)
 * @param logger Reference na Logger pro zaznamenávání
 */
InstrumentLoader::InstrumentLoader(SamplerIO& sampler, int targetSampleRate, Logger& logger)
    : sampler_(sampler), targetSampleRate_(targetSampleRate), logger_(logger), 
      totalLoadedSamples_(0), monoSamplesCount_(0), stereoSamplesCount_(0) {
    
    // Inicializace pole instruments - konstruktory Instrument se zavolají automaticky
    // (všechny pointery na nullptr, velocityExists na false)
    
    logger_.log("InstrumentLoader/constructor", "info", 
                "InstrumentLoader initialized with targetSampleRate " + 
                std::to_string(targetSampleRate_) + " Hz");
    
    logger_.log("InstrumentLoader/constructor", "info", 
                "Prepared array for " + std::to_string(MIDI_NOTE_MAX + 1) + 
                " MIDI notes with " + std::to_string(VELOCITY_LAYERS) + " velocity layers");
                
    logger_.log("InstrumentLoader/constructor", "info", 
                "All samples will be converted to stereo interleaved format [L,R,L,R...]");
}

/**
 * @brief Destruktor - uvolní všechnu alokovanou paměť
 * Prochází všechny MIDI noty a velocity vrstvy a uvolňuje float buffery.
 * SampleInfo pointery se neuvolňují (patří SamplerIO).
 */
InstrumentLoader::~InstrumentLoader() {
    int freedCount = 0;
    
    // Prochází všechny MIDI noty 0-127
    for (int midi = MIDI_NOTE_MIN; midi <= MIDI_NOTE_MAX; midi++) {
        // Prochází všechny velocity 0-7
        for (int vel = 0; vel < VELOCITY_LAYERS; vel++) {
            if (instruments_[midi].velocityExists[vel] && 
                instruments_[midi].sample_ptr_velocity[vel] != nullptr) {
                
                // Uvolnění float bufferu
                free(instruments_[midi].sample_ptr_velocity[vel]);
                instruments_[midi].sample_ptr_velocity[vel] = nullptr;
                instruments_[midi].velocityExists[vel] = false;
                
                // SampleInfo pointery se NEUVOLŇUJÍ - patří SamplerIO
                instruments_[midi].sample_ptr_sampleInfo[vel] = nullptr;
                
                freedCount++;
            }
        }
    }
    
    logger_.log("InstrumentLoader/destructor", "info", 
                "Memory freed for " + std::to_string(freedCount) + " stereo buffers");
    
    logger_.log("InstrumentLoader/destructor", "info", 
                "InstrumentLoader destructor completed");
}

/**
 * @brief Hlavní metoda pro načtení všech instrumentů
 * Prochází všechny MIDI noty a velocity, vyhledává samples a načítá je do bufferů.
 */
void InstrumentLoader::loadAllInstruments() {
    logger_.log("InstrumentLoader/loadAllInstruments", "info", 
                "Starting loading of all instruments for targetSampleRate " + 
                std::to_string(targetSampleRate_) + " Hz");
    
    int foundSamples = 0;
    int missingSamples = 0;
    totalLoadedSamples_ = 0;
    
    // Cyklus pro všechny MIDI noty 0-127
    for (int midi = MIDI_NOTE_MIN; midi <= MIDI_NOTE_MAX; midi++) {
        // Cyklus pro všechny velocity 0-7
        for (int vel = 0; vel < VELOCITY_LAYERS; vel++) {
            // Vyhledání indexu v SamplerIO
            int index = sampler_.findSampleInSampleList(
                static_cast<uint8_t>(midi), 
                static_cast<uint8_t>(vel), 
                targetSampleRate_
            );
            
            if (index != -1) {
                // Sample nalezen - načtení do bufferu
                logger_.log("InstrumentLoader/loadAllInstruments", "info", 
                           "Sample found for MIDI " + std::to_string(midi) + 
                           " velocity " + std::to_string(vel) + " at index " + std::to_string(index));
                
                // Načtení samplu do bufferu
                if (loadSampleToBuffer(index, static_cast<uint8_t>(vel), static_cast<uint8_t>(midi))) {
                    foundSamples++;
                    totalLoadedSamples_++;
                }
                
            } else {
                // Sample nenalezen - nastavení null a warning log
                instruments_[midi].sample_ptr_sampleInfo[vel] = nullptr;
                instruments_[midi].sample_ptr_velocity[vel] = nullptr;
                instruments_[midi].velocityExists[vel] = false;
                instruments_[midi].frame_count_stereo[vel] = 0;
                instruments_[midi].total_samples_stereo[vel] = 0;
                instruments_[midi].was_originally_mono[vel] = false;
                
                logger_.log("InstrumentLoader/loadAllInstruments", "warn", 
                           "Sample for MIDI " + std::to_string(midi) + 
                           " velocity " + std::to_string(vel) + 
                           " not found at frequency " + std::to_string(targetSampleRate_) + " Hz");
                
                missingSamples++;
            }
        }
    }
    
    // Summary log s mono/stereo statistikami
    int totalSlots = (MIDI_NOTE_MAX + 1) * VELOCITY_LAYERS;
    logger_.log("InstrumentLoader/loadAllInstruments", "info", 
                "Loading completed. Found: " + std::to_string(foundSamples) + 
                ", Missing: " + std::to_string(missingSamples) + 
                ", Total slots: " + std::to_string(totalSlots));
    
    logger_.log("InstrumentLoader/loadAllInstruments", "info", 
                "Successfully loaded " + std::to_string(totalLoadedSamples_) + 
                " samples into memory as 32-bit stereo float buffers");
                
    logger_.log("InstrumentLoader/loadAllInstruments", "info", 
                "Channel distribution: " + std::to_string(monoSamplesCount_) + 
                " originally mono, " + std::to_string(stereoSamplesCount_) + " originally stereo/multi-channel");
    
    // NOVÉ: Validace stereo konzistence po načtení
    logger_.log("InstrumentLoader/loadAllInstruments", "info", 
                "Starting stereo consistency validation...");
    validateStereoConsistency();
}

/**
 * @brief Načte jeden sample do bufferu
 * Kompletní pipeline: otevření souboru, alokace, načtení, konverze, přiřazení.
 * @param sampleIndex Index samplu v SamplerIO
 * @param velocity Velocity vrstva (0-7)
 * @param midi_note MIDI nota (0-127)
 * @return true při úspěchu, false při chybě (s error logem a exit)
 */
bool InstrumentLoader::loadSampleToBuffer(int sampleIndex, uint8_t velocity, uint8_t midi_note) {
    // Validace parametrů
    validateVelocity(velocity, "loadSampleToBuffer");
    validateMidiNote(midi_note, "loadSampleToBuffer");
    
    // Krok 1: Otevření souboru
    SNDFILE* sndfile = nullptr;
    SF_INFO sfinfo;
    
    if (!openSampleFile(sampleIndex, sndfile, sfinfo)) {
        return false; // Chyba už je zalogována v openSampleFile
    }
    
    // Získání metadat přes SamplerIO gettery
    sf_count_t frameCount = sampler_.getSampleCount(sampleIndex, logger_);
    int channelCount = sampler_.getChannelCount(sampleIndex, logger_);
    bool needsConversion = sampler_.getNeedsConversion(sampleIndex, logger_);
    bool isInterleaved = sampler_.getIsInterleavedFormat(sampleIndex, logger_);
    const char* filename = sampler_.getFilename(sampleIndex, logger_);
    
    // Krok 2: Alokace temporary bufferu pro načtení (původní velikost)
    size_t tempBufferSize = frameCount * channelCount * sizeof(float);
    float* tempBuffer = static_cast<float*>(malloc(tempBufferSize));
    
    if (!tempBuffer) {
        logger_.log("InstrumentLoader/loadSampleToBuffer", "error", 
                   "Memory allocation error for temporary buffer: " + 
                   std::to_string(tempBufferSize) + " bytes");
        sf_close(sndfile);
        std::exit(1);
    }
    
    // Krok 3: Načtení dat pomocí sf_readf_float (automatická PCM->float konverze)
    sf_count_t framesRead = sf_readf_float(sndfile, tempBuffer, frameCount);
    sf_close(sndfile);
    
    if (framesRead != frameCount) {
        logger_.log("InstrumentLoader/loadSampleToBuffer", "error", 
                   "Data reading error from file " + std::string(filename) + 
                   ": expected " + std::to_string(frameCount) + 
                   " frames, read " + std::to_string(framesRead));
        free(tempBuffer);
        std::exit(1);
    }
    
    // Logování konverze (vždy, i když neproběhla)
    if (needsConversion) {
        logger_.log("InstrumentLoader/loadSampleToBuffer", "info", 
                   "PCM to 32-bit float conversion performed for file: " + std::string(filename));
    } else {
        logger_.log("InstrumentLoader/loadSampleToBuffer", "info", 
                   "File already in 32-bit float format, no conversion needed: " + std::string(filename));
    }
    
    // Krok 4: NOVÉ - Alokace permanent bufferu VŽDY pro stereo (frameCount * 2 * sizeof(float))
    size_t stereoBufferSize = frameCount * 2 * sizeof(float);
    float* permanentBuffer = static_cast<float*>(malloc(stereoBufferSize));
    
    if (!permanentBuffer) {
        logger_.log("InstrumentLoader/loadSampleToBuffer", "error", 
                   "Memory allocation error for permanent stereo buffer: " + 
                   std::to_string(stereoBufferSize) + " bytes");
        free(tempBuffer);
        std::exit(1);
    }
    
    // Krok 5: NOVÉ - Konverze na stereo formát (mono→stereo duplikace nebo stereo kopírování)
    bool wasOriginallyMono = (channelCount == 1);
    
    if (channelCount == 1) {
        // MONO → STEREO konverze: duplikace mono dat do obou kanálů (L=R)
        for (sf_count_t frame = 0; frame < frameCount; frame++) {
            permanentBuffer[frame * 2] = tempBuffer[frame];     // L kanál
            permanentBuffer[frame * 2 + 1] = tempBuffer[frame]; // R kanál (duplikace)
        }
        logger_.log("InstrumentLoader/loadSampleToBuffer", "info", 
                   "Mono to stereo conversion performed (L=R duplication): " + std::string(filename));
        
    } else if (channelCount == 2) {
        // STEREO → STEREO: přímé kopírování (již správný formát)
        if (isInterleaved) {
            memcpy(permanentBuffer, tempBuffer, tempBufferSize);
            logger_.log("InstrumentLoader/loadSampleToBuffer", "info", 
                       "Stereo data already in interleaved format, direct copy: " + 
                       std::string(filename));
        } else {
            // Non-interleaved → interleaved konverze
            for (sf_count_t frame = 0; frame < frameCount; frame++) {
                permanentBuffer[frame * 2] = tempBuffer[frame];                    // L kanál
                permanentBuffer[frame * 2 + 1] = tempBuffer[frameCount + frame];   // R kanál
            }
            logger_.log("InstrumentLoader/loadSampleToBuffer", "info", 
                       "Non-interleaved to interleaved stereo conversion performed: " + 
                       std::string(filename));
        }
        
    } else {
        // Multi-channel (>2) → STEREO: použijeme jen první 2 kanály
        if (isInterleaved) {
            // Interleaved multi-channel: [L1,R1,C1,L2,R2,C2...] → [L1,R1,L2,R2...]
            for (sf_count_t frame = 0; frame < frameCount; frame++) {
                permanentBuffer[frame * 2] = tempBuffer[frame * channelCount];     // L
                permanentBuffer[frame * 2 + 1] = tempBuffer[frame * channelCount + 1]; // R
            }
        } else {
            // Non-interleaved multi-channel: [L...][R...][C...] → [L1,R1,L2,R2...]
            for (sf_count_t frame = 0; frame < frameCount; frame++) {
                permanentBuffer[frame * 2] = tempBuffer[frame];                    // L kanál
                permanentBuffer[frame * 2 + 1] = tempBuffer[frameCount + frame];   // R kanál
            }
        }
        logger_.log("InstrumentLoader/loadSampleToBuffer", "info", 
                   "Multi-channel to stereo conversion performed (using L+R channels): " + 
                   std::string(filename) + " (" + std::to_string(channelCount) + " → 2 channels)");
    }
    
    // Krok 6: Uvolnění temporary bufferu
    free(tempBuffer);
    
    // Krok 7: AKTUALIZACE - Přiřazení permanent bufferu, SampleInfo pointeru a NOVÝCH stereo metadat
    instruments_[midi_note].sample_ptr_velocity[velocity] = permanentBuffer;
    instruments_[midi_note].velocityExists[velocity] = true;
    
    // NOVÉ: Uložení stereo metadat
    instruments_[midi_note].frame_count_stereo[velocity] = frameCount;              // počet stereo frame párů
    instruments_[midi_note].total_samples_stereo[velocity] = frameCount * 2;       // celkový počet float hodnot
    instruments_[midi_note].was_originally_mono[velocity] = wasOriginallyMono;     // původní formát
    
    // Počítání mono/stereo statistik (podle původního formátu)
    if (wasOriginallyMono) {
        monoSamplesCount_++;
    } else {
        stereoSamplesCount_++;
    }
    
    // Získání a přiřazení SampleInfo pointeru (pointer na data v SamplerIO)
    // Pozor: Toto je trochu hacknuté, ale v kontextu současného API je to nejjednodušší
    // V ideálním případě by SamplerIO mělo getter getSampleInfo(index)
    const std::vector<SampleInfo>& sampleList = sampler_.getLoadedSampleList();
    if (sampleIndex >= 0 && static_cast<size_t>(sampleIndex) < sampleList.size()) {
        // Const cast kvůli designu - pointer na data v SamplerIO
        instruments_[midi_note].sample_ptr_sampleInfo[velocity] = 
            const_cast<SampleInfo*>(&sampleList[sampleIndex]);
    } else {
        logger_.log("InstrumentLoader/loadSampleToBuffer", "error", 
                   "Invalid sampleIndex " + std::to_string(sampleIndex) + 
                   " for SampleInfo pointer assignment");
        std::exit(1);
    }
    
    // Úspěšné přiřazení - detailní log s novými stereo metadaty
    logger_.log("InstrumentLoader/loadSampleToBuffer", "info", 
               "Stereo buffer assigned for MIDI " + std::to_string(midi_note) + 
               " velocity " + std::to_string(velocity) + ": " + 
               std::to_string(frameCount) + " frames, " +
               std::to_string(frameCount * 2) + " total samples, " +
               std::to_string(stereoBufferSize) + " bytes, format: stereo interleaved [L,R,L,R...]");
    
    std::string originalFormat = wasOriginallyMono ? "originally mono" : "originally stereo";
    logger_.log("InstrumentLoader/loadSampleToBuffer", "info", 
               "Buffer for MIDI " + std::to_string(midi_note) + 
               "/velocity " + std::to_string(velocity) + 
               " allocated and loaded successfully (" + originalFormat + ")");
    
    return true;
}

/**
 * @brief Otevře sample soubor pro čtení
 * @param sampleIndex Index samplu v SamplerIO
 * @param sndfile Reference na SNDFILE pointer (výstup)
 * @param sfinfo Reference na SF_INFO strukturu (výstup)
 * @return true při úspěchu, false při chybě (s error logem a exit)
 */
bool InstrumentLoader::openSampleFile(int sampleIndex, SNDFILE*& sndfile, SF_INFO& sfinfo) {
    // Získání cesty k souboru přes SamplerIO getter
    const char* filename = sampler_.getFilename(sampleIndex, logger_);
    
    // Inicializace SF_INFO struktury
    memset(&sfinfo, 0, sizeof(sfinfo));
    
    // Otevření souboru pro čtení
    sndfile = sf_open(filename, SFM_READ, &sfinfo);
    
    if (!sndfile) {
        logger_.log("InstrumentLoader/openSampleFile", "error", 
                   "File opening error " + std::string(filename) + 
                   ": " + sf_strerror(nullptr));
        std::exit(1);
    }
    
    logger_.log("InstrumentLoader/openSampleFile", "info", 
               "File " + std::string(filename) + " opened successfully");
    
    return true;
}

/**
 * @brief Getter pro přístup k Instrument struktuře podle MIDI noty
 * @param midi_note MIDI nota (0-127)
 * @return Reference na Instrument strukturu
 */
Instrument& InstrumentLoader::getInstrument(uint8_t midi_note) {
    validateMidiNote(midi_note, "getInstrument");
    return instruments_[midi_note];
}

/**
 * @brief Const getter pro read-only přístup k Instrument struktuře
 * @param midi_note MIDI nota (0-127)
 * @return Const reference na Instrument strukturu
 */
const Instrument& InstrumentLoader::getInstrument(uint8_t midi_note) const {
    validateMidiNote(midi_note, "getInstrument const");
    return instruments_[midi_note];
}

/**
 * @brief Validace, že všechny načtené buffery jsou skutečně stereo
 * Kontroluje konzistenci mezi metadaty a skutečnými buffery.
 * Při selhání validace: zaloguje chyby a volá std::exit(1).
 */
void InstrumentLoader::validateStereoConsistency() {
    int validatedSamples = 0;
    int validationErrors = 0;
    
    for (int midi = MIDI_NOTE_MIN; midi <= MIDI_NOTE_MAX; midi++) {
        const Instrument& inst = instruments_[midi];
        
        for (int vel = 0; vel < VELOCITY_LAYERS; vel++) {
            if (inst.velocityExists[vel]) {
                validatedSamples++;
                
                // Kontrola 1: Buffer pointer nesmí být null
                if (inst.sample_ptr_velocity[vel] == nullptr) {
                    logger_.log("InstrumentLoader/validateStereoConsistency", "error", 
                              "NULL audio buffer for MIDI " + std::to_string(midi) + 
                              " velocity " + std::to_string(vel) + 
                              " despite velocityExists=true");
                    validationErrors++;
                    continue;
                }
                
                // Kontrola 2: SampleInfo pointer nesmí být null
                if (inst.sample_ptr_sampleInfo[vel] == nullptr) {
                    logger_.log("InstrumentLoader/validateStereoConsistency", "error", 
                              "NULL sampleInfo for MIDI " + std::to_string(midi) + 
                              " velocity " + std::to_string(vel) + 
                              " despite velocityExists=true");
                    validationErrors++;
                    continue;
                }
                
                // Kontrola 3: Frame count musí být > 0
                if (inst.frame_count_stereo[vel] <= 0) {
                    logger_.log("InstrumentLoader/validateStereoConsistency", "error", 
                              "Invalid frame_count_stereo " + std::to_string(inst.frame_count_stereo[vel]) + 
                              " for MIDI " + std::to_string(midi) + " velocity " + std::to_string(vel));
                    validationErrors++;
                }
                
                // Kontrola 4: Total samples musí být frame_count * 2
                sf_count_t expectedTotalSamples = inst.frame_count_stereo[vel] * 2;
                if (inst.total_samples_stereo[vel] != expectedTotalSamples) {
                    logger_.log("InstrumentLoader/validateStereoConsistency", "error", 
                              "Inconsistent total_samples_stereo for MIDI " + std::to_string(midi) + 
                              " velocity " + std::to_string(vel) + 
                              ": expected " + std::to_string(expectedTotalSamples) + 
                              ", got " + std::to_string(inst.total_samples_stereo[vel]));
                    validationErrors++;
                }
                
                // Kontrola 5: Konzistence s původními metadata (SampleInfo.sample_count)
                SampleInfo* sampleInfo = inst.sample_ptr_sampleInfo[vel];
                if (inst.frame_count_stereo[vel] != sampleInfo->sample_count) {
                    logger_.log("InstrumentLoader/validateStereoConsistency", "error", 
                              "Frame count mismatch for MIDI " + std::to_string(midi) + 
                              " velocity " + std::to_string(vel) + 
                              ": stereo_frame_count=" + std::to_string(inst.frame_count_stereo[vel]) + 
                              ", original_sample_count=" + std::to_string(sampleInfo->sample_count));
                    validationErrors++;
                }
                
                // Kontrola 6: Logická konzistence was_originally_mono vs původní channels
                bool expectedMono = (sampleInfo->channels == 1);
                if (inst.was_originally_mono[vel] != expectedMono) {
                    logger_.log("InstrumentLoader/validateStereoConsistency", "error", 
                              "Mono flag inconsistency for MIDI " + std::to_string(midi) + 
                              " velocity " + std::to_string(vel) + 
                              ": was_originally_mono=" + (inst.was_originally_mono[vel] ? "true" : "false") + 
                              ", original_channels=" + std::to_string(sampleInfo->channels));
                    validationErrors++;
                }
            }
        }
    }
    
    // Summary validace
    logger_.log("InstrumentLoader/validateStereoConsistency", "info", 
              "Stereo consistency validation completed. Validated " + std::to_string(validatedSamples) + 
              " samples, found " + std::to_string(validationErrors) + " errors");
    
    if (validationErrors == 0) {
        logger_.log("InstrumentLoader/validateStereoConsistency", "info", 
                  "✓ All stereo buffers are consistent and valid");
    } else {
        logger_.log("InstrumentLoader/validateStereoConsistency", "error", 
                  "✗ Stereo consistency validation FAILED with " + 
                  std::to_string(validationErrors) + " errors - terminating");
        std::exit(1);
    }
}

/**
 * @brief Validuje velocity parametr
 * @param velocity Velocity hodnota k validaci
 * @param functionName Název funkce pro error log
 */
void InstrumentLoader::validateVelocity(uint8_t velocity, const char* functionName) const {
    if (velocity >= VELOCITY_LAYERS) {
        logger_.log("InstrumentLoader/" + std::string(functionName), "error", 
                   "Invalid velocity " + std::to_string(velocity) + 
                   " outside range 0-" + std::to_string(VELOCITY_LAYERS - 1));
        std::exit(1);
    }
}

/**
 * @brief Validuje MIDI note parametr
 * @param midi_note MIDI nota k validaci
 * @param functionName Název funkce pro error log
 */
void InstrumentLoader::validateMidiNote(uint8_t midi_note, const char* functionName) const {
    if (midi_note < MIDI_NOTE_MIN || midi_note > MIDI_NOTE_MAX) {
        logger_.log("InstrumentLoader/" + std::string(functionName), "error", 
                   "Invalid MIDI note " + std::to_string(midi_note) + 
                   " outside range " + std::to_string(MIDI_NOTE_MIN) + 
                   "-" + std::to_string(MIDI_NOTE_MAX));
        std::exit(1);
    }
}