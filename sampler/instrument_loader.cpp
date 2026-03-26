#include "instrument_loader.h"
#include "sine_wave_generator.h"
#include "sample_rate_converter.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sndfile.h>
#include <set>

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
    validateSamplerReference(sampler, logger);

    sampler_ = &sampler;
    logger_ = &logger;

    logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Info,
              "Starting loadInstrumentData with targetSampleRate " +
              std::to_string(targetSampleRate) + " Hz");

    // Detect which rate is actually available; may activate resampling fallback
    int sourceRate = detectAvailableSampleRate(sampler, targetSampleRate, logger);
    bool needsResampling = (sourceRate != targetSampleRate);

    // Find closest alternative rate for per-note fallback (handles incomplete cache).
    // If some f44 files exist but not all, notes missing from cache fall back to this
    // rate and are resampled on-the-fly rather than being silently skipped.
    int altRate = targetSampleRate; // default: no alternative
    {
        const auto& list = sampler.getLoadedSampleList();
        int bestDiff = INT_MAX;
        for (const auto& s : list) {
            if (s.frequency != targetSampleRate) {
                int diff = std::abs(s.frequency - targetSampleRate);
                if (diff < bestDiff) {
                    bestDiff = diff;
                    altRate = s.frequency;
                }
            }
        }
    }

    if (actual_samplerate_ != 0) {
        logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Info,
                  "Clearing previous data (previous sampleRate: " +
                  std::to_string(actual_samplerate_) + " Hz)");
        clear(logger);
    }

    actual_samplerate_ = targetSampleRate;

    logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Info,
              "InstrumentLoader initialized: sourceRate=" + std::to_string(sourceRate) +
              " Hz, targetRate=" + std::to_string(targetSampleRate) + " Hz" +
              (needsResampling ? " [RESAMPLING ACTIVE]" : " [exact match]"));

    logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Info,
              "Prepared array for " + std::to_string(MIDI_NOTE_MAX + 1) +
              " MIDI notes with " + std::to_string(velocityLayerCount_) + " velocity layers");

    logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Info,
              "All samples will be stored as stereo interleaved float [L,R,L,R...]");

    int foundSamples = 0;
    int missingSamples = 0;
    totalLoadedSamples_ = 0;
    monoSamplesCount_ = 0;
    stereoSamplesCount_ = 0;

    // Count total available samples for progress reporting
    int totalAvailable = static_cast<int>(sampler.getLoadedSampleList().size());

    for (int midi = MIDI_NOTE_MIN; midi <= MIDI_NOTE_MAX; midi++) {
        // Progress trace every 10 MIDI notes
        if (midi % 10 == 0) {
            int pct = totalAvailable > 0
                      ? (foundSamples * 100 / totalAvailable)
                      : 0;
            logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Info,
                      "Progress: MIDI " + std::to_string(midi) +
                      "/127, loaded " + std::to_string(foundSamples) +
                      "/" + std::to_string(totalAvailable) +
                      " samples (" + std::to_string(pct) + "%)");
        }

        for (int vel = 0; vel < velocityLayerCount_; vel++) {
            // Phase 1: try target rate (cached f44, or exact match)
            int index = sampler.findSampleInSampleList(
                static_cast<uint8_t>(midi), static_cast<uint8_t>(vel), targetSampleRate);
            int effectiveSourceRate = targetSampleRate;

            // Phase 2: cache miss — fall back to alt rate and resample
            if (index == -1 && altRate != targetSampleRate) {
                index = sampler.findSampleInSampleList(
                    static_cast<uint8_t>(midi), static_cast<uint8_t>(vel), altRate);
                if (index != -1) {
                    effectiveSourceRate = altRate;
                    logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Warning,
                              "Cache miss MIDI " + std::to_string(midi) +
                              "/vel" + std::to_string(vel) +
                              " — falling back to " + std::to_string(altRate) +
                              " Hz with resampling");
                }
            }

            if (index != -1) {
                if (loadSampleToBuffer(index, static_cast<uint8_t>(vel), static_cast<uint8_t>(midi),
                                       effectiveSourceRate, targetSampleRate, logger)) {
                    foundSamples++;
                    totalLoadedSamples_++;
                }
            } else {
                instruments_[midi].sample_ptr_sampleInfo[vel] = nullptr;
                instruments_[midi].sample_ptr_velocity[vel] = nullptr;
                instruments_[midi].velocityExists[vel] = false;
                instruments_[midi].frame_count_stereo[vel] = 0;
                instruments_[midi].total_samples_stereo[vel] = 0;
                instruments_[midi].was_originally_mono[vel] = false;

                logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Warning,
                          "Sample for MIDI " + std::to_string(midi) +
                          " velocity " + std::to_string(vel) +
                          " not found at " + std::to_string(sourceRate) + " Hz");

                missingSamples++;
            }
        }

        // Vynulovat nepoužité layers (pokud velocityLayerCount_ < MAX_VELOCITY_LAYERS)
        for (int vel = velocityLayerCount_; vel < MAX_VELOCITY_LAYERS; vel++) {
            instruments_[midi].sample_ptr_sampleInfo[vel] = nullptr;
            instruments_[midi].sample_ptr_velocity[vel] = nullptr;
            instruments_[midi].velocityExists[vel] = false;
            instruments_[midi].frame_count_stereo[vel] = 0;
            instruments_[midi].total_samples_stereo[vel] = 0;
            instruments_[midi].was_originally_mono[vel] = false;
        }
    }

    // Summary log s mono/stereo statistikami
    int totalSlots = (MIDI_NOTE_MAX + 1) * velocityLayerCount_;
    logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Info, 
              "Loading completed. Found: " + std::to_string(foundSamples) + 
              ", Missing: " + std::to_string(missingSamples) + 
              ", Total slots: " + std::to_string(totalSlots));
    
    logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Info, 
              "Successfully loaded " + std::to_string(totalLoadedSamples_) + 
              " samples into memory as 32-bit stereo float buffers");
              
    logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Info, 
              "Channel distribution: " + std::to_string(monoSamplesCount_) + 
              " originally mono, " + std::to_string(stereoSamplesCount_) + " originally stereo/multi-channel");
    
    // Validace stereo konzistence po načtení
    logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Info, 
              "Starting stereo consistency validation...");
    validateStereoConsistency(logger);
    
    logger.log("InstrumentLoader/loadInstrumentData", LogSeverity::Info,
              "InstrumentLoader data loading completed successfully");
}

/**
 * @brief Load sine wave data for all MIDI notes (fallback when no sample bank)
 * @param targetSampleRate Target sample rate (44100 or 48000 Hz)
 * @param logger Reference to Logger for logging
 */
void InstrumentLoader::loadSineWaveData(int targetSampleRate, Logger& logger) {
    // Validate targetSampleRate
    validateTargetSampleRate(targetSampleRate, logger);

    logger.log("InstrumentLoader/loadSineWaveData", LogSeverity::Info,
              "=== SINE WAVE MODE: Starting initialization ===");
    logger.log("InstrumentLoader/loadSineWaveData", LogSeverity::Info,
              "Target sample rate: " + std::to_string(targetSampleRate) + " Hz");

    // Clear previous data if any
    if (actual_samplerate_ != 0) {
        logger.log("InstrumentLoader/loadSineWaveData", LogSeverity::Info,
                  "Clearing previous data (previous sampleRate: " +
                  std::to_string(actual_samplerate_) + " Hz)");
        clear(logger);
    }

    // Set sample rate
    actual_samplerate_ = targetSampleRate;

    logger.log("InstrumentLoader/loadSineWaveData", LogSeverity::Info,
              "Generating sine waves for " + std::to_string(MIDI_NOTE_MAX + 1) +
              " MIDI notes with " + std::to_string(velocityLayerCount_) + " velocity layers");

    // Counters
    int generatedSamples = 0;
    totalLoadedSamples_ = 0;
    monoSamplesCount_ = 0;
    stereoSamplesCount_ = 0;

    // Generate sine waves for all MIDI notes
    for (int midi = MIDI_NOTE_MIN; midi <= MIDI_NOTE_MAX; midi++) {
        // Generate for all velocity layers (1-indexed in generator, 0-indexed in array)
        for (int vel = 0; vel < velocityLayerCount_; vel++) {
            // Generate stereo sine wave
            std::vector<float> sineData = SineWaveGenerator::generateStereoSine(
                static_cast<uint8_t>(midi),
                vel + 1,  // Velocity layer 1-8 (generator uses 1-indexed)
                velocityLayerCount_,
                targetSampleRate,
                2.0f  // 2 second duration
            );

            // Calculate metadata
            int totalSamples = static_cast<int>(sineData.size());
            int stereoFrames = totalSamples / 2;  // Interleaved L/R

            // Allocate permanent buffer (same as WAV loader)
            size_t bufferSize = totalSamples * sizeof(float);
            float* permanentBuffer = static_cast<float*>(malloc(bufferSize));

            if (!permanentBuffer) {
                logger.log("InstrumentLoader/loadSineWaveData", LogSeverity::Error,
                          "Memory allocation error for MIDI " + std::to_string(midi) +
                          " velocity " + std::to_string(vel) + ": " +
                          std::to_string(bufferSize) + " bytes");
                std::exit(1);
            }

            // Copy sine data to permanent buffer
            std::memcpy(permanentBuffer, sineData.data(), bufferSize);

            // Store in instruments array
            instruments_[midi].sample_ptr_velocity[vel] = permanentBuffer;
            instruments_[midi].velocityExists[vel] = true;
            instruments_[midi].frame_count_stereo[vel] = stereoFrames;
            instruments_[midi].total_samples_stereo[vel] = totalSamples;
            instruments_[midi].was_originally_mono[vel] = false;  // Sine is always stereo

            // Note: sample_ptr_sampleInfo[vel] stays nullptr (no WAV file metadata)

            generatedSamples++;
            totalLoadedSamples_++;
            stereoSamplesCount_++;  // Count as stereo
        }
    }

    // Summary log
    int totalSlots = (MIDI_NOTE_MAX + 1) * velocityLayerCount_;
    logger.log("InstrumentLoader/loadSineWaveData", LogSeverity::Info,
              "Sine wave generation completed. Generated: " + std::to_string(generatedSamples) +
              " / " + std::to_string(totalSlots) + " slots");

    logger.log("InstrumentLoader/loadSineWaveData", LogSeverity::Info,
              "Successfully loaded " + std::to_string(totalLoadedSamples_) +
              " sine wave samples into memory as 32-bit stereo float buffers");

    logger.log("InstrumentLoader/loadSineWaveData", LogSeverity::Info,
              "All sine waves are stereo with slight phase offset for width");

    // Validate stereo consistency
    logger.log("InstrumentLoader/loadSineWaveData", LogSeverity::Info,
              "Starting stereo consistency validation...");
    validateStereoConsistency(logger);

    logger.log("InstrumentLoader/loadSineWaveData", LogSeverity::Info,
              "=== SINE WAVE MODE: Initialization completed successfully ===");
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
        // Prochází všechny velocity layers
        for (int vel = 0; vel < MAX_VELOCITY_LAYERS; vel++) {
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
    
    logger.log("InstrumentLoader/clear", LogSeverity::Info, 
              "Memory freed for " + std::to_string(freedCount) + " stereo buffers");
    logger.log("InstrumentLoader/clear", LogSeverity::Info, 
              "InstrumentLoader data cleared and reset to uninitialized state");
}

/**
 * @brief Vyčistí všechna načtená data bez loggingu
 * Používá se v destruktoru kde logger nemusí být dostupný.
 */
void InstrumentLoader::clearWithoutLogging() {
    // Prochází všechny MIDI noty 0-127
    for (int midi = MIDI_NOTE_MIN; midi <= MIDI_NOTE_MAX; midi++) {
        // Prochází všechny velocity layers
        for (int vel = 0; vel < MAX_VELOCITY_LAYERS; vel++) {
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
bool InstrumentLoader::loadSampleToBuffer(int sampleIndex, uint8_t velocity, uint8_t midi_note,
                                          int sourceRate, int targetRate, Logger& logger) {
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
        logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Error, 
                   "Memory allocation error for temporary buffer: " + 
                   std::to_string(tempBufferSize) + " bytes");
        sf_close(sndfile);
        std::exit(1);
    }
    
    // Krok 3: Načtení dat pomocí sf_readf_float (automatická PCM->float konverze)
    int framesRead = sf_readf_float(sndfile, tempBuffer, frameCount);
    sf_close(sndfile);
    
    if (framesRead != frameCount) {
        logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Error, 
                   "Data reading error from file " + std::string(filename) + 
                   ": expected " + std::to_string(frameCount) + 
                   " frames, read " + std::to_string(framesRead));
        free(tempBuffer);
        std::exit(1);
    }
    
    // Logování konverze (vždy, i když neproběhla)
    if (needsConversion) {
        logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Info, 
                   "PCM to 32-bit float conversion performed for file: " + std::string(filename));
    } else {
        logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Info, 
                   "File already in 32-bit float format, no conversion needed: " + std::string(filename));
    }
    
    // Krok 4: NOVÉ - Alokace permanent bufferu VÝDY pro stereo (frameCount * 2 * sizeof(float))
    size_t stereoBufferSize = frameCount * 2 * sizeof(float);
    float* permanentBuffer = static_cast<float*>(malloc(stereoBufferSize));
    
    if (!permanentBuffer) {
        logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Error, 
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
        logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Info, 
                   "Mono to stereo conversion performed (L=R duplication): " + std::string(filename));
        
    } else if (channelCount == 2) {
        // STEREO → STEREO: přímé kopírování (již správný formát)
        if (isInterleaved) {
            memcpy(permanentBuffer, tempBuffer, tempBufferSize);
            logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Info, 
                       "Stereo data already in interleaved format, direct copy: " + 
                       std::string(filename));
        } else {
            // Non-interleaved → interleaved konverze
            for (int frame = 0; frame < frameCount; frame++) {
                permanentBuffer[frame * 2] = tempBuffer[frame];                    // L kanál
                permanentBuffer[frame * 2 + 1] = tempBuffer[frameCount + frame];   // R kanál
            }
            logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Info, 
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
        logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Info, 
                   "Multi-channel to stereo conversion performed (using L+R channels): " + 
                   std::string(filename) + " (" + std::to_string(channelCount) + " → 2 channels)");
    }
    
    // Krok 6: Uvolnění temporary bufferu (stereo data jsou v permanentBuffer)
    free(tempBuffer);

    // Krok 6b: Resampling (pouze pokud sourceRate != targetRate)
    int finalFrameCount = frameCount;
    float* finalBuffer = permanentBuffer;

    if (sourceRate != targetRate) {
        logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Info,
                   "Resampling MIDI " + std::to_string(midi_note) +
                   "/vel" + std::to_string(velocity) +
                   " from " + std::to_string(sourceRate) +
                   " Hz to " + std::to_string(targetRate) + " Hz");

        int resampledFrames = 0;
        float* resampledBuffer = SampleRateConverter::resampleStereo(
            permanentBuffer, frameCount, sourceRate, targetRate, resampledFrames, logger);

        free(permanentBuffer);  // Release pre-resample stereo buffer

        if (!resampledBuffer) {
            logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Error,
                       "Resampling failed for MIDI " + std::to_string(midi_note) +
                       "/vel" + std::to_string(velocity) + " — skipping sample");
            return false;
        }

        finalBuffer     = resampledBuffer;
        finalFrameCount = resampledFrames;

        logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Info,
                   "Resampling complete: " + std::to_string(frameCount) +
                   " -> " + std::to_string(finalFrameCount) + " frames");

        // Cache resampled file next to the originals so future loads skip resampling
        const std::string srcPath = sampler_->getFilename(sampleIndex, logger);
        const std::string dstPath = SampleRateConverter::buildResampledPath(srcPath, sourceRate, targetRate);
        if (!dstPath.empty()) {
            SampleRateConverter::saveWav(dstPath, finalBuffer, finalFrameCount, targetRate, logger);
        } else {
            logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Warning,
                       "Could not build cache path for: " + srcPath + " — cache skipped");
        }
    }

    // Krok 7: Přiřazení finálního bufferu a metadat
    instruments_[midi_note].sample_ptr_velocity[velocity] = finalBuffer;
    instruments_[midi_note].velocityExists[velocity] = true;

    instruments_[midi_note].frame_count_stereo[velocity]    = finalFrameCount;
    instruments_[midi_note].total_samples_stereo[velocity]  = finalFrameCount * 2;
    instruments_[midi_note].was_originally_mono[velocity]   = wasOriginallyMono;
    
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
        logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Error, 
                   "Invalid sampleIndex " + std::to_string(sampleIndex) + 
                   " for SampleInfo pointer assignment");
        std::exit(1);
    }
    
    std::string resampleNote = (sourceRate != targetRate)
        ? " [resampled " + std::to_string(sourceRate) + "->" + std::to_string(targetRate) + " Hz]"
        : "";
    std::string originalFormat = wasOriginallyMono ? "originally mono" : "originally stereo";
    logger.log("InstrumentLoader/loadSampleToBuffer", LogSeverity::Info,
               "Buffer assigned for MIDI " + std::to_string(midi_note) +
               "/vel" + std::to_string(velocity) + ": " +
               std::to_string(finalFrameCount) + " frames, " +
               std::to_string(finalFrameCount * 2) + " floats, " +
               originalFormat + resampleNote);
    
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
        logger.log("InstrumentLoader/openSampleFile", LogSeverity::Error, 
                   "File opening error " + std::string(filename) + 
                   ": " + sf_strerror(nullptr));
        std::exit(1);
    }
    
    logger.log("InstrumentLoader/openSampleFile", LogSeverity::Info, 
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
    int sineWaveSamples = 0;
    int wavSamples = 0;

    for (int midi = MIDI_NOTE_MIN; midi <= MIDI_NOTE_MAX; midi++) {
        const Instrument& inst = instruments_[midi];

        for (int vel = 0; vel < MAX_VELOCITY_LAYERS; vel++) {
            if (inst.velocityExists[vel]) {
                validatedSamples++;

                // Kontrola 1: Buffer pointer nesmí být null
                if (inst.sample_ptr_velocity[vel] == nullptr) {
                    logger.log("InstrumentLoader/validateStereoConsistency", LogSeverity::Error,
                              "NULL audio buffer for MIDI " + std::to_string(midi) +
                              " velocity " + std::to_string(vel) +
                              " despite velocityExists=true");
                    validationErrors++;
                    continue;
                }

                // Detekce sine wave mode: sampleInfo je nullptr pro sine waves
                bool isSineWaveMode = (inst.sample_ptr_sampleInfo[vel] == nullptr);

                if (isSineWaveMode) {
                    // SINE WAVE MODE: Přeskočit sampleInfo-dependent kontroly
                    sineWaveSamples++;

                    // Kontrola 3: Frame count musí být > 0
                    if (inst.frame_count_stereo[vel] <= 0) {
                        logger.log("InstrumentLoader/validateStereoConsistency", LogSeverity::Error,
                                  "Invalid frame_count_stereo " + std::to_string(inst.frame_count_stereo[vel]) +
                                  " for MIDI " + std::to_string(midi) + " velocity " + std::to_string(vel));
                        validationErrors++;
                    }

                    // Kontrola 4: Total samples musí být frame_count * 2
                    int expectedTotalSamples = inst.frame_count_stereo[vel] * 2;
                    if (inst.total_samples_stereo[vel] != expectedTotalSamples) {
                        logger.log("InstrumentLoader/validateStereoConsistency", LogSeverity::Error,
                                  "Inconsistent total_samples_stereo for MIDI " + std::to_string(midi) +
                                  " velocity " + std::to_string(vel) +
                                  ": expected " + std::to_string(expectedTotalSamples) +
                                  ", got " + std::to_string(inst.total_samples_stereo[vel]));
                        validationErrors++;
                    }
                } else {
                    // WAV FILE MODE: Provést všechny kontroly včetně sampleInfo
                    wavSamples++;

                    // Kontrola 3: Frame count musí být > 0
                    if (inst.frame_count_stereo[vel] <= 0) {
                        logger.log("InstrumentLoader/validateStereoConsistency", LogSeverity::Error,
                                  "Invalid frame_count_stereo " + std::to_string(inst.frame_count_stereo[vel]) +
                                  " for MIDI " + std::to_string(midi) + " velocity " + std::to_string(vel));
                        validationErrors++;
                    }

                    // Kontrola 4: Total samples musí být frame_count * 2
                    int expectedTotalSamples = inst.frame_count_stereo[vel] * 2;
                    if (inst.total_samples_stereo[vel] != expectedTotalSamples) {
                        logger.log("InstrumentLoader/validateStereoConsistency", LogSeverity::Error,
                                  "Inconsistent total_samples_stereo for MIDI " + std::to_string(midi) +
                                  " velocity " + std::to_string(vel) +
                                  ": expected " + std::to_string(expectedTotalSamples) +
                                  ", got " + std::to_string(inst.total_samples_stereo[vel]));
                        validationErrors++;
                    }

                    // Kontrola 5: Konzistence s původními metadata (SampleInfo.sample_count)
                    // Přeskočit pokud byl resampling aktivní — frame count se zákonitě liší
                    SampleInfo* sampleInfo = inst.sample_ptr_sampleInfo[vel];
                    bool wasResampled = (actual_samplerate_ != sampleInfo->frequency);
                    if (!wasResampled && inst.frame_count_stereo[vel] != sampleInfo->sample_count) {
                        logger.log("InstrumentLoader/validateStereoConsistency", LogSeverity::Error,
                                  "Frame count mismatch for MIDI " + std::to_string(midi) +
                                  " velocity " + std::to_string(vel) +
                                  ": stereo_frame_count=" + std::to_string(inst.frame_count_stereo[vel]) +
                                  ", original_sample_count=" + std::to_string(sampleInfo->sample_count));
                        validationErrors++;
                    }

                    // Kontrola 6: Logická konzistence was_originally_mono vs původní channels
                    bool expectedMono = (sampleInfo->channels == 1);
                    if (inst.was_originally_mono[vel] != expectedMono) {
                        logger.log("InstrumentLoader/validateStereoConsistency", LogSeverity::Error,
                                  "Mono flag inconsistency for MIDI " + std::to_string(midi) +
                                  " velocity " + std::to_string(vel) +
                                  ": was_originally_mono=" + (inst.was_originally_mono[vel] ? "true" : "false") +
                                  ", original_channels=" + std::to_string(sampleInfo->channels));
                        validationErrors++;
                    }
                }
            }
        }
    }

    // Summary validace
    logger.log("InstrumentLoader/validateStereoConsistency", LogSeverity::Info,
              "Stereo consistency validation completed. Validated " + std::to_string(validatedSamples) +
              " samples (" + std::to_string(sineWaveSamples) + " sine waves, " +
              std::to_string(wavSamples) + " WAV files), found " + std::to_string(validationErrors) + " errors");

    if (validationErrors == 0) {
        logger.log("InstrumentLoader/validateStereoConsistency", LogSeverity::Info,
                  "✓ All stereo buffers are consistent and valid");
    } else {
        logger.log("InstrumentLoader/validateStereoConsistency", LogSeverity::Error,
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
    if (velocity >= MAX_VELOCITY_LAYERS) {
        logger.log("InstrumentLoader/" + std::string(functionName), LogSeverity::Error,
                   "Invalid velocity " + std::to_string(velocity) +
                   " outside range 0-" + std::to_string(MAX_VELOCITY_LAYERS - 1));
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
        logger.log("InstrumentLoader/" + std::string(functionName), LogSeverity::Error, 
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
        std::string errorMsg = "InstrumentLoader not initialized - call loadInstrumentData() first";
        if (logger_) {
            logger_->log("InstrumentLoader/" + std::string(functionName), LogSeverity::Error, errorMsg);
        }
        std::cerr << "[FATAL] InstrumentLoader/" << functionName << ": " << errorMsg << std::endl;
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
        logger.log("InstrumentLoader/validateSamplerReference", LogSeverity::Error, 
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
    if (targetSampleRate <= 0) {
        logger.log("InstrumentLoader/validateTargetSampleRate", LogSeverity::Critical,
                  "Invalid targetSampleRate " + std::to_string(targetSampleRate) +
                  " Hz — must be a positive value");
        std::exit(1);
    }
}

/**
 * @brief Nastavit počet velocity layers
 * @param count Počet layers (1-8)
 */
void InstrumentLoader::setVelocityLayerCount(int count) {
    if (count < 1 || count > MAX_VELOCITY_LAYERS) {
        if (logger_) {
            logger_->log("InstrumentLoader/setVelocityLayerCount", LogSeverity::Warning,
                        "Invalid velocity layer count " + std::to_string(count) +
                        ", using default 8");
        }
        velocityLayerCount_ = 8;
        return;
    }
    velocityLayerCount_ = count;

    if (logger_) {
        logger_->log("InstrumentLoader/setVelocityLayerCount", LogSeverity::Info,
                    "Velocity layer count set to " + std::to_string(velocityLayerCount_));
    }
}

/**
 * @brief Detect which sample rate is actually available in the sample bank.
 *
 * First checks if targetSampleRate has any files. If yes, returns it directly
 * (no resampling). If not, collects all distinct rates present and picks the
 * closest one, logging a Warning with full diagnostic info.
 * If the sample bank is completely empty → Critical log + std::exit(1).
 */
int InstrumentLoader::detectAvailableSampleRate(SamplerIO& sampler, int targetSampleRate, Logger& logger) {
    const auto& sampleList = sampler.getLoadedSampleList();

    if (sampleList.empty()) {
        logger.log("InstrumentLoader/detectAvailableSampleRate", LogSeverity::Critical,
                   "Sample bank is empty — no files were scanned. Cannot load instrument.");
        std::exit(1);
    }

    // Count files at the exact target rate
    int exactCount = 0;
    for (const auto& s : sampleList) {
        if (s.frequency == targetSampleRate) exactCount++;
    }

    if (exactCount > 0) {
        logger.log("InstrumentLoader/detectAvailableSampleRate", LogSeverity::Info,
                   "Exact sample rate match: " + std::to_string(targetSampleRate) +
                   " Hz (" + std::to_string(exactCount) + " files). No resampling needed.");
        return targetSampleRate;
    }

    // Build set of all distinct rates present
    std::set<int> availableRates;
    for (const auto& s : sampleList) {
        availableRates.insert(s.frequency);
    }

    // Log all available rates for diagnostics
    std::string rateList;
    for (int r : availableRates) {
        if (!rateList.empty()) rateList += ", ";
        rateList += std::to_string(r) + " Hz";
    }
    logger.log("InstrumentLoader/detectAvailableSampleRate", LogSeverity::Warning,
               "Target rate " + std::to_string(targetSampleRate) +
               " Hz not available. Available rates in bank: [" + rateList + "]");

    // Pick rate closest to targetSampleRate
    int bestRate = *availableRates.begin();
    int bestDiff = std::abs(bestRate - targetSampleRate);
    for (int rate : availableRates) {
        int diff = std::abs(rate - targetSampleRate);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestRate = rate;
        }
    }

    // Count files at chosen rate
    int bestCount = 0;
    for (const auto& s : sampleList) {
        if (s.frequency == bestRate) bestCount++;
    }

    logger.log("InstrumentLoader/detectAvailableSampleRate", LogSeverity::Warning,
               "Resampling fallback activated: " + std::to_string(bestRate) +
               " Hz -> " + std::to_string(targetSampleRate) +
               " Hz. Source files: " + std::to_string(bestCount) +
               ". Ratio: " + std::to_string(static_cast<double>(targetSampleRate) / bestRate).substr(0, 6));

    return bestRate;
}