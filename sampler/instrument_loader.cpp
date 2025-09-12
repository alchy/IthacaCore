#include "instrument_loader.h"
#include <cstdlib>      // Pro malloc, free, std::exit
#include <cstring>      // Pro memcpy
#include <sndfile.h>    // Pro sf_open, sf_readf_float, sf_close

/**
 * @brief Prázdný konstruktor InstrumentLoader
 * Inicializuje všechny hodnoty na výchozí stav.
 */
InstrumentLoader::InstrumentLoader()
    : actual_samplerate_(0), sampler_(nullptr), logger_(nullptr),
      totalLoadedSamples_(0), monoSamplesCount_(0), stereoSamplesCount_(0) {
    
    // Inicializace pole instruments - konstruktory Instrument se zavolají automaticky
    // (všechny pointery na nullptr, velocityExists na false)
}

/**
 * @brief Destruktor - uvolní všechnu alokovanou paměť
 * Prochází všechny MIDI noty a velocity vrstvy a uvolňuje float buffery.
 * SampleInfo pointery se neuvolňují (patří SamplerIO).
 */
InstrumentLoader::~InstrumentLoader() {
    // Destruktor volá clear bez loggingu, protože nemáme garantovanou dostupnost loggeru
    clearWithoutLogging();
}

/**
 * @brief Hlavní metoda pro načítání dat instrumentů
 * @param sampler Reference na SamplerIO pro přístup k sample metadatům
 * @param targetSampleRate Požadovaná frekvence vzorkování (44100 nebo 48000 Hz)
 * @param logger Reference na Logger pro zaznamenávání
 */
void InstrumentLoader::loadInstrumentData(SamplerIO& sampler, int targetSampleRate, Logger& logger) {
    // Validace parametrů
    validateSamplerReference(sampler, logger);
    validateTargetSampleRate(targetSampleRate, logger);
    
    // Uložení referencí pro pozdější použití
    sampler_ = &sampler;
    logger_ = &logger;
    
    logger.log("InstrumentLoader/loadInstrumentData", "info", 
              "Starting loadInstrumentData with targetSampleRate " + 
              std::to_string(targetSampleRate) + " Hz");
    
    // Automatické vyčištění předchozích dat (pokud existují)
    if (actual_samplerate_ != 0) {
        logger.log("InstrumentLoader/loadInstrumentData", "info", 
                  "Clearing previous data (previous sampleRate: " + 
                  std::to_string(actual_samplerate_) + " Hz)");
        clear(logger);
    }
    
    // Nastavení nové target sample rate
    actual_samplerate_ = targetSampleRate;
    
    logger.log("InstrumentLoader/loadInstrumentData", "info", 
              "InstrumentLoader initialized with targetSampleRate " + 
              std::to_string(actual_samplerate_) + " Hz");
    
    logger.log("InstrumentLoader/loadInstrumentData", "info", 
              "Prepared array for " + std::to_string(MIDI_NOTE_MAX + 1) + 
              " MIDI notes with " + std::to_string(VELOCITY_LAYERS) + " velocity layers");
              
    logger.log("InstrumentLoader/loadInstrumentData", "info", 
              "All samples will be converted to stereo interleaved format [L,R,L,R...]");
    
    // Hlavní loading loop - stejný algoritmus jako původní loadInstrument()
    logger.log("InstrumentLoader/loadInstrumentData", "info", 
              "Starting loading of all instruments for targetSampleRate " + 
              std::to_string(actual_samplerate_) + " Hz");
    
    int foundSamples = 0;
    int missingSamples = 0;
    totalLoadedSamples_ = 0;
    monoSamplesCount_ = 0;
    stereoSamplesCount_ = 0;
    
    // Cyklus pro všechny MIDI noty 0-127
    for (int midi = MIDI_NOTE_MIN; midi <= MIDI_NOTE_MAX; midi++) {
        // Cyklus pro všechny velocity 0-7
        for (int vel = 0; vel < VELOCITY_LAYERS; vel++) {
            // Vyhledání indexu v SamplerIO
            int index = sampler.findSampleInSampleList(
                static_cast<uint8_t>(midi), 
                static_cast<uint8_t>(vel), 
                actual_samplerate_
            );
            
            if (index != -1) {
                // Sample nalezen - načtení do bufferu
                logger.log("InstrumentLoader/loadInstrumentData", "info", 
                          "Sample found for MIDI " + std::to_string(midi) + 
                          " velocity " + std::to_string(vel) + " at index " + std::to_string(index));
                
                // Načtení samplu do bufferu
                if (loadSampleToBuffer(index, static_cast<uint8_t>(vel), static_cast<uint8_t>(midi), logger)) {
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
                
                logger.log("InstrumentLoader/loadInstrumentData", "warn", 
                          "Sample for MIDI " + std::to_string(midi) + 
                          " velocity " + std::to_string(vel) + 
                          " not found at frequency " + std::to_string(actual_samplerate_) + " Hz");
                
                missingSamples++;
            }
        }
    }
    
    // Summary log s mono/stereo statistikami
    int totalSlots = (MIDI_NOTE_MAX + 1) * VELOCITY_LAYERS;
    logger.log("InstrumentLoader/loadInstrumentData", "info", 
              "Loading completed. Found: " + std::to_string(foundSamples) + 
              ", Missing: " + std::to_string(missingSamples) + 
              ", Total slots: " + std::to_string(totalSlots));
    
    logger.log("InstrumentLoader/loadInstrumentData", "info", 
              "Successfully loaded " + std::to_string(totalLoadedSamples_) + 
              " samples into memory as 32-bit stereo float buffers");
              
    logger.log("InstrumentLoader/loadInstrumentData", "info", 
              "Channel distribution: " + std::to_string(monoSamplesCount_) + 
              " originally mono, " + std::to_string(stereoSamplesCount_) + " originally stereo/multi-channel");
    
    // Validace stereo konzistence po načtení
    logger.log("InstrumentLoader/loadInstrumentData", "info", 
              "Starting stereo consistency validation...");
    validateStereoConsistency(logger);
    
    logger.log("InstrumentLoader/loadInstrumentData", "info", 
              "InstrumentLoader data loading completed successfully");
}

/**
 * @brief Vyčistí všechna načtená data
 * @param logger Reference na Logger pro zaznamenávání
 * Uvolní všechny alokované float buffery a resetuje stav objektu.
 */
void InstrumentLoader::clear(Logger& logger) {
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
                
                // Reset metadat
                instruments_[midi].frame_count_stereo[vel] = 0;
                instruments_[midi].total_samples_stereo[vel] = 0;
                instruments_[midi].was_originally_mono[vel] = false;
                
                freedCount++;
            }
        }
    }
    
    // Reset počítadel a stavu
    totalLoadedSamples_ = 0;
    monoSamplesCount_ = 0;
    stereoSamplesCount_ = 0;
    actual_samplerate_ = 0;
    
    logger.log("InstrumentLoader/clear", "info", 
              "Memory freed for " + std::to_string(freedCount) + " stereo buffers");
    logger.log("InstrumentLoader/clear", "info", 
              "InstrumentLoader data cleared and reset to uninitialized state");
}

/**
 * @brief Vyčistí všechna načtená data bez loggingu
 * Používá se v destruktoru kde logger nemusí být dostupný.
 */
void InstrumentLoader::clearWithoutLogging() {
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
                
                // Reset metadat
                instruments_[midi].frame_count_stereo[vel] = 0;
                instruments_[midi].total_samples_stereo[vel] = 0;
                instruments_[midi].was_originally_mono[vel] = false;
            }
        }
    }
    
    // Reset počítadel a stavu
    totalLoadedSamples_ = 0;
    monoSamplesCount_ = 0;
    stereoSamplesCount_ = 0;
    actual_samplerate_ = 0;
}

/**
 * @brief Načte jeden sample do bufferu
 * Kompletní pipeline: otevření souboru, alokace, načtení, konverze, přiřazení.
 * @param sampleIndex Index samplu v SamplerIO
 * @param velocity Velocity vrstva (0-7)
 * @param midi_note MIDI nota (0-127)
 * @param logger Reference na Logger pro zaznamenávání
 * @return true při úspěchu, false při chybě (s error logem a exit)
 */
bool InstrumentLoader::loadSampleToBuffer(int sampleIndex, uint8_t velocity, uint8_t midi_note, Logger& logger) {
    // Validace parametrů
    validateVelocity(velocity, "loadSampleToBuffer", logger);
    validateMidiNote(midi_note, "loadSampleToBuffer", logger);
    
    // Krok 1: Otevření souboru
    SNDFILE* sndfile = nullptr;
    SF_INFO sfinfo;
    
    if (!openSampleFile(sampleIndex, sndfile, sfinfo, logger)) {
        return false; // Chyba už je zalogována v openSampleFile
    }
    
    // Získání metadat přes SamplerIO gettery
    int frameCount = sampler_->getSampleCount(sampleIndex, logger);
    int channelCount = sampler_->getChannelCount(sampleIndex, logger);
    bool needsConversion = sampler_->getNeedsConversion(sampleIndex, logger);
    bool isInterleaved = sampler_->getIsInterleavedFormat(sampleIndex, logger);
    const char* filename = sampler_->getFilename(sampleIndex, logger);
    
    // Krok 2: Alokace temporary bufferu pro načtení (původní velikost)
    size_t tempBufferSize = frameCount * channelCount * sizeof(float);
    float* tempBuffer = static_cast<float*>(malloc(tempBufferSize));
    
    if (!tempBuffer) {
        logger.log("InstrumentLoader/loadSampleToBuffer", "error", 
                   "Memory allocation error for temporary buffer: " + 
                   std::to_string(tempBufferSize) + " bytes");
        sf_close(sndfile);
        std::exit(1);
    }
    
    // Krok 3: Načtení dat pomocí sf_readf_float (automatická PCM->float konverze)
    int framesRead = sf_readf_float(sndfile, tempBuffer, frameCount);
    sf_close(sndfile);
    
    if (framesRead != frameCount) {
        logger.log("InstrumentLoader/loadSampleToBuffer", "error", 
                   "Data reading error from file " + std::string(filename) + 
                   ": expected " + std::to_string(frameCount) + 
                   " frames, read " + std::to_string(framesRead));
        free(tempBuffer);
        std::exit(1);
    }
    
    // Logování konverze (vždy, i když neproběhla)
    if (needsConversion) {
        logger.log("InstrumentLoader/loadSampleToBuffer", "info", 
                   "PCM to 32-bit float conversion performed for file: " + std::string(filename));
    } else {
        logger.log("InstrumentLoader/loadSampleToBuffer", "info", 
                   "File already in 32-bit float format, no conversion needed: " + std::string(filename));
    }
    
    // Krok 4: NOVÉ - Alokace permanent bufferu VÝDY pro stereo (frameCount * 2 * sizeof(float))
    size_t stereoBufferSize = frameCount * 2 * sizeof(float);
    float* permanentBuffer = static_cast<float*>(malloc(stereoBufferSize));
    
    if (!permanentBuffer) {
        logger.log("InstrumentLoader/loadSampleToBuffer", "error", 
                   "Memory allocation error for permanent stereo buffer: " + 
                   std::to_string(stereoBufferSize) + " bytes");
        free(tempBuffer);
        std::exit(1);
    }
    
    // Krok 5: NOVÉ - Konverze na stereo formát (mono→stereo duplikace nebo stereo kopírování)
    bool wasOriginallyMono = (channelCount == 1);
    
    if (channelCount == 1) {
        // MONO → STEREO konverze: duplikace mono dat do obou kanálů (L=R)
        for (int frame = 0; frame < frameCount; frame++) {
            permanentBuffer[frame * 2] = tempBuffer[frame];     // L kanál
            permanentBuffer[frame * 2 + 1] = tempBuffer[frame]; // R kanál (duplikace)
        }
        logger.log("InstrumentLoader/loadSampleToBuffer", "info", 
                   "Mono to stereo conversion performed (L=R duplication): " + std::string(filename));
        
    } else if (channelCount == 2) {
        // STEREO → STEREO: přímé kopírování (již správný formát)
        if (isInterleaved) {
            memcpy(permanentBuffer, tempBuffer, tempBufferSize);
            logger.log("InstrumentLoader/loadSampleToBuffer", "info", 
                       "Stereo data already in interleaved format, direct copy: " + 
                       std::string(filename));
        } else {
            // Non-interleaved → interleaved konverze
            for (int frame = 0; frame < frameCount; frame++) {
                permanentBuffer[frame * 2] = tempBuffer[frame];                    // L kanál
                permanentBuffer[frame * 2 + 1] = tempBuffer[frameCount + frame];   // R kanál
            }
            logger.log("InstrumentLoader/loadSampleToBuffer", "info", 
                       "Non-interleaved to interleaved stereo conversion performed: " + 
                       std::string(filename));
        }
        
    } else {
        // Multi-channel (>2) → STEREO: použijeme jen první 2 kanály
        if (isInterleaved) {
            // Interleaved multi-channel: [L1,R1,C1,L2,R2,C2...] → [L1,R1,L2,R2...]
            for (int frame = 0; frame < frameCount; frame++) {
                permanentBuffer[frame * 2] = tempBuffer[frame * channelCount];     // L
                permanentBuffer[frame * 2 + 1] = tempBuffer[frame * channelCount + 1]; // R
            }
        } else {
            // Non-interleaved multi-channel: [L...][R...][C...] → [L1,R1,L2,R2...]
            for (int frame = 0; frame < frameCount; frame++) {
                permanentBuffer[frame * 2] = tempBuffer[frame];                    // L kanál
                permanentBuffer[frame * 2 + 1] = tempBuffer[frameCount + frame];   // R kanál
            }
        }
        logger.log("InstrumentLoader/loadSampleToBuffer", "info", 
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
    const std::vector<SampleInfo>& sampleList = sampler_->getLoadedSampleList();
    if (sampleIndex >= 0 && static_cast<size_t>(sampleIndex) < sampleList.size()) {
        // Const cast kvůli designu - pointer na data v SamplerIO
        instruments_[midi_note].sample_ptr_sampleInfo[velocity] = 
            const_cast<SampleInfo*>(&sampleList[sampleIndex]);
    } else {
        logger.log("InstrumentLoader/loadSampleToBuffer", "error", 
                   "Invalid sampleIndex " + std::to_string(sampleIndex) + 
                   " for SampleInfo pointer assignment");
        std::exit(1);
    }
    
    // Úspěšné přiřazení - detailní log s novými stereo metadaty
    logger.log("InstrumentLoader/loadSampleToBuffer", "info", 
               "Stereo buffer assigned for MIDI " + std::to_string(midi_note) + 
               " velocity " + std::to_string(velocity) + ": " + 
               std::to_string(frameCount) + " frames, " +
               std::to_string(frameCount * 2) + " total samples, " +
               std::to_string(stereoBufferSize) + " bytes, format: stereo interleaved [L,R,L,R...]");
    
    std::string originalFormat = wasOriginallyMono ? "originally mono" : "originally stereo";
    logger.log("InstrumentLoader/loadSampleToBuffer", "info", 
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
 * @param logger Reference na Logger pro zaznamenávání
 * @return true při úspěchu, false při chybě (s error logem a exit)
 */
bool InstrumentLoader::openSampleFile(int sampleIndex, SNDFILE*& sndfile, SF_INFO& sfinfo, Logger& logger) {
    // Získání cesty k souboru přes SamplerIO getter
    const char* filename = sampler_->getFilename(sampleIndex, logger);
    
    // Inicializace SF_INFO struktury
    memset(&sfinfo, 0, sizeof(sfinfo));
    
    // Otevření souboru pro čtení
    sndfile = sf_open(filename, SFM_READ, &sfinfo);
    
    if (!sndfile) {
        logger.log("InstrumentLoader/openSampleFile", "error", 
                   "File opening error " + std::string(filename) + 
                   ": " + sf_strerror(nullptr));
        std::exit(1);
    }
    
    logger.log("InstrumentLoader/openSampleFile", "info", 
               "File " + std::string(filename) + " opened successfully");
    
    return true;
}

/**
 * @brief Getter pro přístup k Instrument struktuře podle MIDI noty
 * @param midi_note MIDI nota (0-127)
 * @return Reference na Instrument strukturu
 */
Instrument& InstrumentLoader::getInstrumentNote(uint8_t midi_note) {
    checkInitialization("getInstrumentNote");
    validateMidiNote(midi_note, "getInstrumentNote", *logger_);
    return instruments_[midi_note];
}

/**
 * @brief Const getter pro read-only přístup k Instrument struktuře
 * @param midi_note MIDI nota (0-127)
 * @return Const reference na Instrument strukturu
 */
const Instrument& InstrumentLoader::getInstrumentNote(uint8_t midi_note) const {
    checkInitialization("getInstrumentNote const");
    validateMidiNote(midi_note, "getInstrumentNote const", *logger_);
    return instruments_[midi_note];
}

/**
 * @brief Getter pro celkový počet načtených samplů
 * @return Počet úspěšně načtených samplů
 */
int InstrumentLoader::getTotalLoadedSamples() const {
    checkInitialization("getTotalLoadedSamples");
    return totalLoadedSamples_;
}

/**
 * @brief Getter pro počet mono samplů
 * @return Počet načtených mono samplů
 */
int InstrumentLoader::getMonoSamplesCount() const {
    checkInitialization("getMonoSamplesCount");
    return monoSamplesCount_;
}

/**
 * @brief Getter pro počet stereo samplů  
 * @return Počet načtených stereo samplů
 */
int InstrumentLoader::getStereoSamplesCount() const {
    checkInitialization("getStereoSamplesCount");
    return stereoSamplesCount_;
}

/**
 * @brief Validace, že všechny načtené buffery jsou skutečně stereo
 * @param logger Reference na Logger pro zaznamenávání
 * Kontroluje konzistenci mezi metadaty a skutečnými buffery.
 * Při selhání validace: zaloguje chyby a volá std::exit(1).
 */
void InstrumentLoader::validateStereoConsistency(Logger& logger) {
    checkInitialization("validateStereoConsistency");
    
    int validatedSamples = 0;
    int validationErrors = 0;
    
    for (int midi = MIDI_NOTE_MIN; midi <= MIDI_NOTE_MAX; midi++) {
        const Instrument& inst = instruments_[midi];
        
        for (int vel = 0; vel < VELOCITY_LAYERS; vel++) {
            if (inst.velocityExists[vel]) {
                validatedSamples++;
                
                // Kontrola 1: Buffer pointer nesmí být null
                if (inst.sample_ptr_velocity[vel] == nullptr) {
                    logger.log("InstrumentLoader/validateStereoConsistency", "error", 
                              "NULL audio buffer for MIDI " + std::to_string(midi) + 
                              " velocity " + std::to_string(vel) + 
                              " despite velocityExists=true");
                    validationErrors++;
                    continue;
                }
                
                // Kontrola 2: SampleInfo pointer nesmí být null
                if (inst.sample_ptr_sampleInfo[vel] == nullptr) {
                    logger.log("InstrumentLoader/validateStereoConsistency", "error", 
                              "NULL sampleInfo for MIDI " + std::to_string(midi) + 
                              " velocity " + std::to_string(vel) + 
                              " despite velocityExists=true");
                    validationErrors++;
                    continue;
                }
                
                // Kontrola 3: Frame count musí být > 0
                if (inst.frame_count_stereo[vel] <= 0) {
                    logger.log("InstrumentLoader/validateStereoConsistency", "error", 
                              "Invalid frame_count_stereo " + std::to_string(inst.frame_count_stereo[vel]) + 
                              " for MIDI " + std::to_string(midi) + " velocity " + std::to_string(vel));
                    validationErrors++;
                }
                
                // Kontrola 4: Total samples musí být frame_count * 2
                int expectedTotalSamples = inst.frame_count_stereo[vel] * 2;
                if (inst.total_samples_stereo[vel] != expectedTotalSamples) {
                    logger.log("InstrumentLoader/validateStereoConsistency", "error", 
                              "Inconsistent total_samples_stereo for MIDI " + std::to_string(midi) + 
                              " velocity " + std::to_string(vel) + 
                              ": expected " + std::to_string(expectedTotalSamples) + 
                              ", got " + std::to_string(inst.total_samples_stereo[vel]));
                    validationErrors++;
                }
                
                // Kontrola 5: Konzistence s původními metadata (SampleInfo.sample_count)
                SampleInfo* sampleInfo = inst.sample_ptr_sampleInfo[vel];
                if (inst.frame_count_stereo[vel] != sampleInfo->sample_count) {
                    logger.log("InstrumentLoader/validateStereoConsistency", "error", 
                              "Frame count mismatch for MIDI " + std::to_string(midi) + 
                              " velocity " + std::to_string(vel) + 
                              ": stereo_frame_count=" + std::to_string(inst.frame_count_stereo[vel]) + 
                              ", original_sample_count=" + std::to_string(sampleInfo->sample_count));
                    validationErrors++;
                }
                
                // Kontrola 6: Logická konzistence was_originally_mono vs původní channels
                bool expectedMono = (sampleInfo->channels == 1);
                if (inst.was_originally_mono[vel] != expectedMono) {
                    logger.log("InstrumentLoader/validateStereoConsistency", "error", 
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
    logger.log("InstrumentLoader/validateStereoConsistency", "info", 
              "Stereo consistency validation completed. Validated " + std::to_string(validatedSamples) + 
              " samples, found " + std::to_string(validationErrors) + " errors");
    
    if (validationErrors == 0) {
        logger.log("InstrumentLoader/validateStereoConsistency", "info", 
                  "✓ All stereo buffers are consistent and valid");
    } else {
        logger.log("InstrumentLoader/validateStereoConsistency", "error", 
                  "✗ Stereo consistency validation FAILED with " + 
                  std::to_string(validationErrors) + " errors - terminating");
        std::exit(1);
    }
}

/**
 * @brief Validuje velocity parametr
 * @param velocity Velocity hodnota k validaci
 * @param functionName Název funkce pro error log
 * @param logger Reference na Logger pro error log
 */
void InstrumentLoader::validateVelocity(uint8_t velocity, const char* functionName, Logger& logger) const {
    if (velocity >= VELOCITY_LAYERS) {
        logger.log("InstrumentLoader/" + std::string(functionName), "error", 
                   "Invalid velocity " + std::to_string(velocity) + 
                   " outside range 0-" + std::to_string(VELOCITY_LAYERS - 1));
        std::exit(1);
    }
}

/**
 * @brief Validuje MIDI note parametr
 * @param midi_note MIDI nota k validaci
 * @param functionName Název funkce pro error log
 * @param logger Reference na Logger pro error log
 */
void InstrumentLoader::validateMidiNote(uint8_t midi_note, const char* functionName, Logger& logger) const {
    if (midi_note < MIDI_NOTE_MIN || midi_note > MIDI_NOTE_MAX) {
        logger.log("InstrumentLoader/" + std::string(functionName), "error", 
                   "Invalid MIDI note " + std::to_string(midi_note) + 
                   " outside range " + std::to_string(MIDI_NOTE_MIN) + 
                   "-" + std::to_string(MIDI_NOTE_MAX));
        std::exit(1);
    }
}

/**
 * @brief Kontroluje inicializaci objektu
 * @param functionName Název funkce pro error log
 */
void InstrumentLoader::checkInitialization(const char* functionName) const {
    if (actual_samplerate_ == 0) {
        if (logger_) {
            logger_->log("InstrumentLoader/" + std::string(functionName), "error", 
                       "InstrumentLoader not initialized - call loadInstrumentData() first");
        }
        std::exit(1);
    }
}

/**
 * @brief Validuje referenci na SamplerIO
 * @param sampler Reference na SamplerIO k validaci
 * @param logger Reference na Logger pro error log
 */
void InstrumentLoader::validateSamplerReference(SamplerIO& sampler, Logger& logger) {
    // Pro C++ reference je teoreticky nemožné, aby byl nullptr,
    // ale pro jistotu můžeme zkontrolovat adresu
    SamplerIO* samplerPtr = &sampler;
    if (samplerPtr == nullptr) {
        logger.log("InstrumentLoader/validateSamplerReference", "error", 
                  "SamplerIO reference is null - cannot proceed");
        std::exit(1);
    }
}

/**
 * @brief Validuje targetSampleRate
 * @param targetSampleRate Sample rate k validaci
 * @param logger Reference na Logger pro error log
 */
void InstrumentLoader::validateTargetSampleRate(int targetSampleRate, Logger& logger) {
    if (targetSampleRate != 44100 && targetSampleRate != 48000) {
        logger.log("InstrumentLoader/validateTargetSampleRate", "error", 
                  "Invalid targetSampleRate " + std::to_string(targetSampleRate) + 
                  " Hz - only 44100 Hz and 48000 Hz are supported");
        std::exit(1);
    }
}