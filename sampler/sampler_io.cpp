#define _CRT_SECURE_NO_WARNINGS  // Ignorování warningu pro strncpy v MSVC

#include "sampler.h"  // Include hlavičky pro definice SampleInfo a SamplerIO (nyní v sampler.h)
#include <filesystem>   // Pro procházení adresáře
#include <regex>        // Pro parsování názvů souborů
#include <sndfile.h>    // Pro práci s WAV soubory
#include <iostream>     // Pro std::cerr při chybách (jen pro libsndfile error, ale teď přes logger)
#include <cstdlib>      // Pro std::exit

/**
 * @brief Konstruktor SamplerIO - inicializuje prázdný seznam samples
 */
SamplerIO::SamplerIO() {
    // Prázdný konstruktor, sampleList se inicializuje automaticky
}

/**
 * @brief Pomocná funkce pro extrakci frekvence z názvu souboru
 * Očekává pattern: mXXX-velY-ZZ-FREQ.wav nebo mXXX-velY-ZZ.wav (bez frekvence)
 * @param filename Název souboru
 * @return Frekvence z názvu nebo -1 pokud není nalezena
 */
int parseFrequencyFromFilename(const std::string& filename) {
    // Regex pro pattern s frekvencí: mXXX-velY-ZZ-FREQ.wav
    std::regex freqPattern(R"(^m(\d+)-vel(\d+)-\d+-(\d+)\.wav$)", std::regex_constants::icase);
    std::smatch matches;
    
    if (std::regex_match(filename, matches, freqPattern)) {
        return std::stoi(matches[3].str());
    }
    
    return -1; // Frekvence není v názvu
}

/**
 * @brief Načte WAV soubory z adresáře a parsuje jejich metadata
 * @param directoryPath Cesta k adresáři se samples
 * @param logger Reference na logger pro zaznamenávání
 */
void SamplerIO::loadSamples(const std::string& directoryPath, Logger& logger) {
    logger.log("SamplerIO/loadSamples", "info", "Loading samples from: " + directoryPath);
    
    // Kontrola existence adresáře
    if (!std::filesystem::exists(directoryPath)) {
        logger.log("SamplerIO/loadSamples", "error", "Directory does not exist: " + directoryPath);
        std::exit(1);
    }
    
    if (!std::filesystem::is_directory(directoryPath)) {
        logger.log("SamplerIO/loadSamples", "error", "Path is not a directory: " + directoryPath);
        std::exit(1);
    }
    
    // Regex pattern pro parsování názvů souborů: mXXX-velY-ZZ.wav nebo mXXX-velY-ZZ-FREQ.wav
    std::regex filenamePattern(R"(^m(\d+)-vel(\d+)-\d+(-\d+)?\.wav$)", std::regex_constants::icase);
    std::smatch matches;
    
    int loadedCount = 0;
    
    try {
        // Procházení všech souborů v adresáři
        for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            
            std::string filename = entry.path().filename().string();
            std::string fullPath = entry.path().string();
            
            // Pokus o parsování názvu souboru
            if (std::regex_match(filename, matches, filenamePattern)) {
                // Extrakce MIDI noty a velocity z názvu
                int midiNote = std::stoi(matches[1].str());
                int velocity = std::stoi(matches[2].str());
                
                // Validace rozsahů
                if (midiNote < 0 || midiNote > 127) {
                    logger.log("SamplerIO/loadSamples", "warn", 
                              "Invalid MIDI note " + std::to_string(midiNote) + " in file: " + filename);
                    continue;
                }
                
                if (velocity < 0 || velocity > 7) {
                    logger.log("SamplerIO/loadSamples", "warn", 
                              "Invalid velocity " + std::to_string(velocity) + " in file: " + filename);
                    continue;
                }
                
                // Extrakce frekvence z názvu (pokud je přítomná)
                int filenameFreq = parseFrequencyFromFilename(filename);
                
                // Načtení metadata z WAV souboru pomocí libsndfile
                SF_INFO sfInfo;
                memset(&sfInfo, 0, sizeof(sfInfo));
                
                SNDFILE* sndFile = sf_open(fullPath.c_str(), SFM_READ, &sfInfo);
                if (!sndFile) {
                    logger.log("SamplerIO/loadSamples", "error", 
                              "Cannot open WAV file: " + fullPath + " - " + sf_strerror(nullptr));
                    std::exit(1);
                }
                
                // Kontrola konzistence frekvence (pokud je v názvu)
                if (filenameFreq != -1 && filenameFreq != sfInfo.samplerate) {
                    logger.log("SamplerIO/loadSamples", "error", 
                              "Frequency mismatch in file: " + filename + 
                              " (filename: " + std::to_string(filenameFreq) + 
                              " Hz, file: " + std::to_string(sfInfo.samplerate) + " Hz)");
                    sf_close(sndFile);
                    std::exit(1);
                }
                
                // Výpočet délky v sekundách
                double duration = static_cast<double>(sfInfo.frames) / static_cast<double>(sfInfo.samplerate);
                
                // Vytvoření SampleInfo struktury
                SampleInfo sample;
                strncpy(sample.filename, fullPath.c_str(), sizeof(sample.filename) - 1);
                sample.filename[sizeof(sample.filename) - 1] = '\0';  // Zajištění null-termination
                sample.midi_note = static_cast<uint8_t>(midiNote);
                sample.midi_note_velocity = static_cast<uint8_t>(velocity);
                sample.frequency = sfInfo.samplerate;
                sample.sample_count = sfInfo.frames;
                sample.duration_seconds = duration;
                sample.channels = sfInfo.channels;
                sample.is_stereo = (sfInfo.channels >= 2);
                
                // Přidání do seznamu
                sampleList.push_back(sample);
                loadedCount++;
                
                // Logování informací o načteném sample
                std::string channelInfo = sample.is_stereo ? "stereo" : "mono";
                logger.log("SamplerIO/loadSamples", "info", 
                          "Loaded: " + filename + " (MIDI: " + std::to_string(midiNote) + 
                          ", Vel: " + std::to_string(velocity) + 
                          ", Freq: " + std::to_string(sfInfo.samplerate) + " Hz" +
                          ", Duration: " + std::to_string(duration) + "s" +
                          ", Channels: " + std::to_string(sfInfo.channels) + " (" + channelInfo + ")" +
                          ", Frames: " + std::to_string(sfInfo.frames) + ")");
                
                sf_close(sndFile);
            } else {
                logger.log("SamplerIO/loadSamples", "warn", 
                          "Filename doesn't match pattern mXXX-velY-ZZ.wav or mXXX-velY-ZZ-FREQ.wav: " + filename);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        logger.log("SamplerIO/loadSamples", "error", "Filesystem error: " + std::string(e.what()));
        std::exit(1);
    }
    
    logger.log("SamplerIO/loadSamples", "info", 
              "Loading complete. Total samples loaded: " + std::to_string(loadedCount));
}

/**
 * @brief Vyhledá index sample v interním seznamu podle MIDI noty a velocity
 * @param midi_note MIDI nota (0-127)
 * @param velocity Velocity (0-7)
 * @return Index v seznamu nebo -1 pokud nenalezeno
 */
int SamplerIO::findSampleInSampleList(uint8_t midi_note, uint8_t velocity) const {
    for (size_t i = 0; i < sampleList.size(); ++i) {
        if (sampleList[i].midi_note == midi_note && 
            sampleList[i].midi_note_velocity == velocity) {
            return static_cast<int>(i);
        }
    }
    return -1;  // Nenalezeno
}

/**
 * @brief Getter pro přístup k načtenému seznamu samples
 * @return Konstantní reference na vektor SampleInfo
 */
const std::vector<SampleInfo>& SamplerIO::getLoadedSampleList() const {
    return sampleList;
}

/**
 * @brief Getter pro název souboru na zadaném indexu
 * @param index Index v seznamu
 * @param logger Reference pro logování chyb
 * @return const char* na filename, pokud platný index
 */
const char* SamplerIO::getFilename(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getFilename", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
        std::exit(1);
    }
    return sampleList[index].filename;
}

/**
 * @brief Getter pro MIDI notu na zadaném indexu
 * @param index Index v seznamu
 * @param logger Reference pro logování chyb
 * @return uint8_t MIDI nota
 */
uint8_t SamplerIO::getMidiNote(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getMidiNote", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
        std::exit(1);
    }
    return sampleList[index].midi_note;
}

/**
 * @brief Getter pro velocity MIDI noty na zadaném indexu
 * @param index Index v seznamu
 * @param logger Reference pro logování chyb
 * @return uint8_t velocity
 */
uint8_t SamplerIO::getMidiNoteVelocity(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getMidiNoteVelocity", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
        std::exit(1);
    }
    return sampleList[index].midi_note_velocity;
}

/**
 * @brief Getter pro frekvenci samplu na zadaném indexu
 * @param index Index v seznamu
 * @param logger Reference pro logování chyb
 * @return int frekvence (Hz)
 */
int SamplerIO::getFrequency(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getFrequency", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
        std::exit(1);
    }
    return sampleList[index].frequency;
}

/**
 * @brief Getter pro počet vzorků na zadaném indexu
 * @param index Index v seznamu
 * @param logger Reference pro logování chyb
 * @return sf_count_t počet vzorků (frames)
 */
sf_count_t SamplerIO::getSampleCount(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getSampleCount", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
        std::exit(1);
    }
    return sampleList[index].sample_count;
}

/**
 * @brief Getter pro délku v sekundách na zadaném indexu
 * @param index Index v seznamu
 * @param logger Reference pro logování chyb
 * @return double délka v sekundách
 */
double SamplerIO::getDurationSeconds(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getDurationSeconds", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
        std::exit(1);
    }
    return sampleList[index].duration_seconds;
}

/**
 * @brief Getter pro počet kanálů na zadaném indexu
 * @param index Index v seznamu
 * @param logger Reference pro logování chyb
 * @return int počet kanálů
 */
int SamplerIO::getChannelCount(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getChannelCount", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
        std::exit(1);
    }
    return sampleList[index].channels;
}

/**
 * @brief Getter pro stereo flag na zadaném indexu
 * @param index Index v seznamu
 * @param logger Reference pro logování chyb
 * @return bool true pokud stereo (channels >= 2)
 */
bool SamplerIO::getIsStereo(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getIsStereo", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
        std::exit(1);
    }
    return sampleList[index].is_stereo;
}