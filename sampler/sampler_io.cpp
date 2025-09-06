#define _CRT_SECURE_NO_WARNINGS  // Ignorování warningu pro strncpy v MSVC

#include "sampler_io.h"
#include <filesystem>   // Pro procházení adresáře
#include <regex>        // Pro parsování názvů souborů
#include <sndfile.h>    // Pro práci s WAV soubory
#include <algorithm>    // Pro std::min
#include <cstdlib>      // Pro std::exit

/**
 * @brief Konstruktor SamplerIO - inicializuje prázdný seznam samples
 * @param logger Reference na logger pro zaznamenávání
 */
SamplerIO::SamplerIO(Logger& logger) : logger_(logger) {
    logger_.log("SamplerIO/constructor", "info", "SamplerIO instance created");
}

/**
 * @brief Načte WAV soubory z adresáře a parsuje jejich metadata
 * @param directoryPath Cesta k adresáři se samples
 * DŮLEŽITÉ: Žádné výstupy na konzoli - pouze logování
 */
void SamplerIO::loadSamples(const std::string& directoryPath) {
    logger_.log("SamplerIO/loadSamples", "info", "Starting sample loading process");
    logger_.log("SamplerIO/loadSamples", "info", "Target directory: " + directoryPath);
    
    // Kontrola existence adresáře
    if (!std::filesystem::exists(directoryPath)) {
        logger_.log("SamplerIO/loadSamples", "error", "Directory does not exist: " + directoryPath);
        logger_.log("SamplerIO/loadSamples", "error", "Program termination due to missing directory");
        std::exit(1);
    }
    
    if (!std::filesystem::is_directory(directoryPath)) {
        logger_.log("SamplerIO/loadSamples", "error", "Path is not a directory: " + directoryPath);
        logger_.log("SamplerIO/loadSamples", "error", "Program termination due to invalid path");
        std::exit(1);
    }
    
    logger_.log("SamplerIO/loadSamples", "info", "Directory validation completed successfully");
    
    // Regex pattern pro parsování názvů souborů: mXXX-velY-ZZ.wav
    std::regex filenamePattern(R"(^m(\d+)-vel(\d+)-\d+\.wav$)", std::regex_constants::icase);
    std::smatch matches;
    
    int loadedCount = 0;
    int skippedCount = 0;
    int totalFiles = 0;
    
    try {
        logger_.log("SamplerIO/loadSamples", "info", "Starting directory scan for WAV files");
        
        // Procházení všech souborů v adresáři
        for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            
            totalFiles++;
            std::string filename = entry.path().filename().string();
            std::string fullPath = entry.path().string();
            
            // Pokus o parsování názvu souboru
            if (std::regex_match(filename, matches, filenamePattern)) {
                // Extrakce MIDI noty a velocity z názvu
                int midiNote = std::stoi(matches[1].str());
                int velocity = std::stoi(matches[2].str());
                
                // Validace rozsahů
                if (midiNote < 0 || midiNote > 127) {
                    logger_.log("SamplerIO/loadSamples", "warn", 
                              "Invalid MIDI note " + std::to_string(midiNote) + " in file: " + filename + " (valid range: 0-127)");
                    skippedCount++;
                    continue;
                }
                
                if (velocity < 0 || velocity > 7) {
                    logger_.log("SamplerIO/loadSamples", "warn", 
                              "Invalid velocity " + std::to_string(velocity) + " in file: " + filename + " (valid range: 0-7)");
                    skippedCount++;
                    continue;
                }
                
                // Načtení metadata z WAV souboru pomocí libsndfile
                SF_INFO sfInfo;
                memset(&sfInfo, 0, sizeof(sfInfo));
                
                SNDFILE* sndFile = sf_open(fullPath.c_str(), SFM_READ, &sfInfo);
                if (!sndFile) {
                    std::string errorMsg = sf_strerror(nullptr) ? sf_strerror(nullptr) : "Unknown error";
                    logger_.log("SamplerIO/loadSamples", "error", 
                              "Cannot open WAV file: " + fullPath + " - " + errorMsg);
                    skippedCount++;
                    continue;
                }
                
                // Vytvoření SampleInfo struktury
                SampleInfo sample;
                strncpy(sample.filename, fullPath.c_str(), sizeof(sample.filename) - 1);
                sample.filename[sizeof(sample.filename) - 1] = '\0';  // Zajištění null-termination
                sample.midi_note = static_cast<uint8_t>(midiNote);
                sample.midi_note_velocity = static_cast<uint8_t>(velocity);
                sample.sample_frequency = sfInfo.samplerate;
                
                // Přidání do seznamu
                sampleList.push_back(sample);
                loadedCount++;
                
                // Log pouze pro první few samples aby nedošlo k zahlcení logu
                if (loadedCount <= 10 || loadedCount % 100 == 0) {
                    logger_.log("SamplerIO/loadSamples", "info", 
                              "Loaded sample #" + std::to_string(loadedCount) + ": " + filename + 
                              " (MIDI: " + std::to_string(midiNote) + 
                              ", Vel: " + std::to_string(velocity) + 
                              ", Freq: " + std::to_string(sfInfo.samplerate) + " Hz)");
                }
                
                sf_close(sndFile);
            } else {
                // Pouze pro .wav soubory které neodpovídají patternu
                if (filename.length() >= 4 && 
                    (filename.substr(filename.length() - 4) == ".wav" || 
                     filename.substr(filename.length() - 4) == ".WAV")) {
                    logger_.log("SamplerIO/loadSamples", "warn", 
                              "WAV file doesn't match pattern mXXX-velY-ZZ.wav: " + filename);
                    skippedCount++;
                }
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        logger_.log("SamplerIO/loadSamples", "error", "Filesystem error: " + std::string(e.what()));
        logger_.log("SamplerIO/loadSamples", "error", "Program termination due to filesystem error");
        std::exit(1);
    }
    
    // Finální statistiky
    logger_.log("SamplerIO/loadSamples", "info", "=== Sample loading completed ===");
    logger_.log("SamplerIO/loadSamples", "info", "Total files scanned: " + std::to_string(totalFiles));
    logger_.log("SamplerIO/loadSamples", "info", "Successfully loaded: " + std::to_string(loadedCount) + " valid samples");
    logger_.log("SamplerIO/loadSamples", "info", "Skipped files: " + std::to_string(skippedCount));
    logger_.log("SamplerIO/loadSamples", "info", "Sample library size: " + std::to_string(sampleList.size()) + " samples");
    
    if (loadedCount == 0) {
        logger_.log("SamplerIO/loadSamples", "warn", "No valid samples found in directory");
    }
}

/**
 * @brief Vyhledá sample podle MIDI noty a velocity
 * @param midi_note MIDI nota (0-127)
 * @param velocity Velocity (0-7)
 * @return Index v seznamu nebo -1 pokud nenalezeno
 */
int SamplerIO::findSample(uint8_t midi_note, uint8_t velocity) const {
    logger_.log("SamplerIO/findSample", "info", 
               "Searching for sample: MIDI=" + std::to_string(midi_note) + 
               ", Velocity=" + std::to_string(velocity));
    
    for (size_t i = 0; i < sampleList.size(); ++i) {
        if (sampleList[i].midi_note == midi_note && 
            sampleList[i].midi_note_velocity == velocity) {
            logger_.log("SamplerIO/findSample", "info", 
                       "Sample found at index " + std::to_string(i) + ": " + sampleList[i].filename);
            return static_cast<int>(i);
        }
    }
    
    logger_.log("SamplerIO/findSample", "warn", 
               "Sample not found for MIDI=" + std::to_string(midi_note) + 
               ", Velocity=" + std::to_string(velocity));
    return -1;  // Nenalezeno
}

/**
 * @brief Getter pro přístup k seznamu samples
 * @return Konstantní reference na vektor SampleInfo
 */
const std::vector<SampleInfo>& SamplerIO::getSampleList() const {
    logger_.log("SamplerIO/getSampleList", "info", 
               "Returning sample list with " + std::to_string(sampleList.size()) + " samples");
    return sampleList;
}

/**
 * @brief Destruktor - loguje ukončení
 */
SamplerIO::~SamplerIO() {
    logger_.log("SamplerIO/destructor", "info", 
               "SamplerIO instance destroyed. Final sample count: " + std::to_string(sampleList.size()));
}