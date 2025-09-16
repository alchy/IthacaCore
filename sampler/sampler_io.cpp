/*
THIS FILE IS LOCKED, IT IS FUNCTIONAL AND WILL NOT BE CHANGED
*/

#include "sampler.h"  // Include hlavičky pro definice SampleInfo a SamplerIO (nyní v sampler.h)
#include <filesystem>   // Pro procházení adresáře
#include <regex>        // Pro parsování názvů souborů
#include <sndfile.h>    // Pro práci s WAV soubory
#include <iostream>     // Pro std::cerr při chybách (jen pro libsndfile error, ale teď přes logger)
#include <cstdlib>      // Pro std::exit

/**
 * @brief Normalizuje zkrácenou frekvenci z názvu souboru na standardní hodnoty
 * @param freq Frekvence z názvu (zkrácená: 8, 11, 16, 22, 44, 48, 88, 96, 176, 192)
 * @return Normalizovaná frekvence nebo -1 při nerozpoznání
 */
int normalizeFrequency(int freq) {
    switch (freq) {
        case 8:   return 8000;
        case 11:  return 11025;
        case 16:  return 16000;
        case 22:  return 22050;
        case 44:  return 44100;
        case 48:  return 48000;
        case 88:  return 88200;
        case 96:  return 96000;
        case 176: return 176400;
        case 192: return 192000;
        default:  return -1;  // Nerozpoznaná frekvence
    }
}

/**
 * @brief Pomocná funkce pro extrakci frekvence z názvu souboru
 * Očekává pattern: mXXX-velY-fZZ.wav kde ZZ je zkrácená frekvence (44, 48, atd.)
 * @param filename Název souboru
 * @return Frekvence z názvu nebo -1 pokud není nalezena
 */
int parseFrequencyFromFilename(const std::string& filename) {
    // Regex pro pattern s frekvencí: mXXX-velY-fZZ.wav (nový formát s 'f' prefixem)
    std::regex freqPattern(R"(^m(\d+)-vel(\d+)-f(\d+)\.wav$)", std::regex_constants::icase);
    std::smatch matches;
    
    if (std::regex_match(filename, matches, freqPattern)) {
        return std::stoi(matches[3].str());  // Extrakce FREQ z třetí skupiny (po 'f')
    }
    
    return -1; // Frekvence není v názvu (nebo špatný formát)
}

/**
 * @brief Konstruktor SamplerIO - inicializuje prázdný seznam samples
 */
SamplerIO::SamplerIO() {
    // Prázdný konstruktor, sampleList se inicializuje automaticky
}

/**
 * @brief Destruktor SamplerIO - loguje ukončení instance
 */
SamplerIO::~SamplerIO() {
    // Pokud máme přístup k loggeru, zalogujeme ukončení
    // Poznámka: V tomto designu nemáme přímý přístup k loggeru v destruktoru,
    // takže použijeme printf pro konzistenci s Logger destruktorem
    printf("[SamplerIO] SamplerIO destructor called - releasing %zu samples\n", sampleList.size());
}

/**
 * @brief NOVÁ POMOCNÁ METODA: Detekce, zda je WAV v interleaved formátu
 * Pro standardní WAV soubory je vždy true, ale kontrolujeme pro bezpečnost
 * @param filename Cesta k souboru
 * @param logger Reference na logger pro zaznamenávání
 * @return true pokud interleaved (standard), false u vzácných případů
 * Při chybě: loguje error a volá std::exit(1)
 */
bool SamplerIO::detectInterleavedFormat(const char* filename, Logger& logger) const {
    SF_INFO sfInfo;
    memset(&sfInfo, 0, sizeof(sfInfo));
    
    SNDFILE* sndFile = sf_open(filename, SFM_READ, &sfInfo);
    if (!sndFile) {
        logger.log("SamplerIO/detectInterleavedFormat", "error", 
                  "Cannot open WAV file for interleaved detection: " + std::string(filename) + 
                  " - " + sf_strerror(nullptr));
        std::exit(1);
    }
    
    // Kontrola major formátu (SF_FORMAT_WAV)
    int majorFormat = sfInfo.format & SF_FORMAT_TYPEMASK;
    if (majorFormat != SF_FORMAT_WAV) {
        logger.log("SamplerIO/detectInterleavedFormat", "error", 
                  "File is not a WAV format: " + std::string(filename) + 
                  " (format: 0x" + std::to_string(majorFormat) + ")");
        sf_close(sndFile);
        std::exit(1);
    }
    
    // Pro WAV: Standardně vždy interleaved
    // Pouze vzácné případy speciálních WAV variant by mohly být non-interleaved
    // V praxi: Vždy true pro běžné WAV soubory
    sf_close(sndFile);
    
    logger.log("SamplerIO/detectInterleavedFormat", "info", 
              "WAV format confirmed as interleaved: " + std::string(filename));
    
    return true;  // Standardní WAV je vždy interleaved
}

/**
 * @brief Metoda pro detekci subformátu a potřeby konverze do float
 * @param filename Cesta k souboru
 * @param logger Reference na logger pro zaznamenávání
 * @return true pokud 16-bit PCM (potřebuje konverzi), false pokud již float
 * Při chybě nebo nepodporovaném formátu: loguje error a volá std::exit(1)
 */
bool SamplerIO::detectFloatConversionNeed(const char* filename, Logger& logger) const {
    SF_INFO sfInfo;
    memset(&sfInfo, 0, sizeof(sfInfo));
    
    SNDFILE* sndFile = sf_open(filename, SFM_READ, &sfInfo);
    if (!sndFile) {
        logger.log("SamplerIO/detectFloatConversionNeed", "error", 
                  "Cannot open WAV file for conversion detection: " + std::string(filename) + 
                  " - " + sf_strerror(nullptr));
        std::exit(1);
    }
    
    // Kontrola subformátu (SF_FORMAT_SUBMASK)
    int subformat = sfInfo.format & SF_FORMAT_SUBMASK;
    sf_close(sndFile);
    
    switch (subformat) {
        case SF_FORMAT_PCM_16:
            // 16-bit PCM: Potřebuje konverzi do float (normalizace z int16 na [-1.0, 1.0])
            logger.log("SamplerIO/detectFloatConversionNeed", "info", 
                      "16-bit PCM detected, conversion to float needed: " + std::string(filename));
            return true;
            
        case SF_FORMAT_FLOAT:
            // Již 32-bit float: Žádná konverze potřebná
            logger.log("SamplerIO/detectFloatConversionNeed", "info", 
                      "32-bit float detected, no conversion needed: " + std::string(filename));
            return false;
            
        case SF_FORMAT_PCM_24:
            // 24-bit PCM: Také potřebuje konverzi do float
            logger.log("SamplerIO/detectFloatConversionNeed", "info", 
                      "24-bit PCM detected, conversion to float needed: " + std::string(filename));
            return true;
            
        case SF_FORMAT_PCM_32:
            // 32-bit PCM: Potřebuje konverzi do float
            logger.log("SamplerIO/detectFloatConversionNeed", "info", 
                      "32-bit PCM detected, conversion to float needed: " + std::string(filename));
            return true;
            
        case SF_FORMAT_DOUBLE:
            // 64-bit double: Potřebuje konverzi do 32-bit float
            logger.log("SamplerIO/detectFloatConversionNeed", "info", 
                      "64-bit double detected, conversion to 32-bit float needed: " + std::string(filename));
            return true;
            
        default:
            // Nepodporovaný formát
            logger.log("SamplerIO/detectFloatConversionNeed", "error", 
                      "Unsupported subformat detected in file: " + std::string(filename) + 
                      " (subformat: 0x" + std::to_string(subformat) + 
                      "). Supported: 16-bit PCM, 24-bit PCM, 32-bit PCM, 32-bit float, 64-bit double");
            std::exit(1);
    }
}

/**
 * @brief Prohledá adresář s WAV soubory a naplní seznam metadat
 * @brief Také detekuje interleaved formát a potřebu konverze
 * @param directoryPath Cesta k adresáři se samples
 * @param logger Reference na logger pro zaznamenávání
 */
void SamplerIO::scanSampleDirectory(const std::string& directoryPath, Logger& logger) {
    logger.log("SamplerIO/scanSampleDirectory", "info", "Scanning sample directory: " + directoryPath);
    
    // Kontrola existence adresáře
    if (!std::filesystem::exists(directoryPath)) {
        logger.log("SamplerIO/scanSampleDirectory", "error", "Directory does not exist: " + directoryPath);
        std::exit(1);
    }
    
    if (!std::filesystem::is_directory(directoryPath)) {
        logger.log("SamplerIO/scanSampleDirectory", "error", "Path is not a directory: " + directoryPath);
        std::exit(1);
    }
    
    // Regex pattern pro parsování názvů souborů: mXXX-velY-fZZ.wav (nový formát s 'f' pro FREQ)
    std::regex filenamePattern(R"(^m(\d+)-vel(\d+)-f(\d+)\.wav$)", std::regex_constants::icase);
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
                    logger.log("SamplerIO/scanSampleDirectory", "warn", 
                              "Invalid MIDI note " + std::to_string(midiNote) + " in file: " + filename);
                    continue;
                }
                
                if (velocity < 0 || velocity > 7) {
                    logger.log("SamplerIO/scanSampleDirectory", "warn", 
                              "Invalid velocity " + std::to_string(velocity) + " in file: " + filename);
                    continue;
                }
                
                // Extrakce frekvence z názvu (nový formát s 'fZZ')
                int filenameFreq = parseFrequencyFromFilename(filename);
                
                // Načtení metadata z WAV souboru pomocí libsndfile
                SF_INFO sfInfo;
                memset(&sfInfo, 0, sizeof(sfInfo));
                
                SNDFILE* sndFile = sf_open(fullPath.c_str(), SFM_READ, &sfInfo);
                if (!sndFile) {
                    logger.log("SamplerIO/scanSampleDirectory", "error", 
                              "Cannot open WAV file: " + fullPath + " - " + sf_strerror(nullptr));
                    std::exit(1);
                }
                
                // Kontrola konzistence frekvence (pokud je v názvu)
                if (filenameFreq != -1) {
                    int normalizedFreq = normalizeFrequency(filenameFreq);
                    if (normalizedFreq == -1) {
                        logger.log("SamplerIO/scanSampleDirectory", "error", 
                                  "Unsupported frequency format in filename: " + filename + 
                                  " (frequency: " + std::to_string(filenameFreq) + 
                                  "). Supported: 8, 11, 16, 22, 44, 48, 88, 96, 176, 192");
                        sf_close(sndFile);
                        std::exit(1);
                    } else if (normalizedFreq != sfInfo.samplerate) {
                        logger.log("SamplerIO/scanSampleDirectory", "error", 
                                  "Frequency mismatch in file: " + filename + 
                                  " (filename: " + std::to_string(filenameFreq) + 
                                  " -> " + std::to_string(normalizedFreq) + 
                                  " Hz, actual file: " + std::to_string(sfInfo.samplerate) + " Hz)");
                        sf_close(sndFile);
                        std::exit(1);
                    }
                    
                    // Logování úspěšné validace
                    logger.log("SamplerIO/scanSampleDirectory", "info", 
                              "Frequency validation passed: " + filename + 
                              " (" + std::to_string(filenameFreq) + " -> " + 
                              std::to_string(normalizedFreq) + " Hz)");
                }
                
                // Uzavření souboru před dalšími detekcemi
                sf_close(sndFile);
                
                // NOVÉ: Detekce interleaved formátu a potřeby konverze
                bool isInterleaved = detectInterleavedFormat(fullPath.c_str(), logger);
                bool needsConversion = detectFloatConversionNeed(fullPath.c_str(), logger);
                
                // Výpočet délky v sekundách
                double duration = static_cast<double>(sfInfo.frames) / static_cast<double>(sfInfo.samplerate);
                
                // Vytvoření SampleInfo struktury s novými atributy
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
                sample.interleaved_format = isInterleaved;      // NOVÉ
                sample.needs_conversion = needsConversion;      // NOVÉ
                
                // Přidání do seznamu
                sampleList.push_back(sample);
                loadedCount++;
                
                // Logování informací o načteném sample s novými atributy
                std::string channelInfo = sample.is_stereo ? "stereo" : "mono";
                std::string interleavedInfo = sample.interleaved_format ? "interleaved" : "non-interleaved";
                std::string conversionInfo = sample.needs_conversion ? "needs float conversion" : "no conversion needed";
                
                logger.log("SamplerIO/scanSampleDirectory", "info", 
                          "Loaded: " + filename + " (MIDI: " + std::to_string(midiNote) + 
                          ", Vel: " + std::to_string(velocity) + 
                          ", Freq: " + std::to_string(sfInfo.samplerate) + " Hz" +
                          ", Duration: " + std::to_string(duration) + "s" +
                          ", Channels: " + std::to_string(sfInfo.channels) + " (" + channelInfo + ")" +
                          ", Frames: " + std::to_string(sfInfo.frames) +
                          ", Format: " + interleavedInfo + ", " + conversionInfo + ")");
                
            } else {
                logger.log("SamplerIO/scanSampleDirectory", "warn", 
                          "Filename doesn't match pattern mXXX-velY-fZZ.wav: " + filename);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        logger.log("SamplerIO/scanSampleDirectory", "error", "Filesystem error: " + std::string(e.what()));
        std::exit(1);
    }
    
    logger.log("SamplerIO/scanSampleDirectory", "info", 
              "Scanning complete. Total samples indexed: " + std::to_string(loadedCount));
}

/**
 * @brief Vyhledá index sample v interním seznamu podle MIDI noty, velocity a požadované frekvence vzorkování
 * @param midi_note MIDI nota (0-127)
 * @param velocity Velocity (0-7)
 * @param sampleRate Požadovaná frekvence vzorkování v Hz (např. 44100)
 * @return Index v seznamu nebo -1 pokud nenalezeno
 * Chování: Lineární prohledávání, vrací první shodu kde odpovídají všechny tři kritéria.
 * Pokud sampleRate je 0 nebo -1, ignoruje se (pro zpětnou kompatibilitu, ale nedoporučeno).
 */
int SamplerIO::findSampleInSampleList(uint8_t midi_note, uint8_t velocity, int sampleRate) const {
    for (size_t i = 0; i < sampleList.size(); ++i) {
        bool frequencyMatch = (sampleRate <= 0) ? true : (sampleList[i].frequency == sampleRate);  // Ignoruj frekvenci při neplatné hodnotě
        if (sampleList[i].midi_note == midi_note && 
            sampleList[i].midi_note_velocity == velocity &&
            frequencyMatch) {
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
 * @return int počet vzorků (frames)
 */
int SamplerIO::getSampleCount(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getSampleCount", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
        std::exit(1);
    }
    return sampleList[index].sample_count;
}

/**
 * @brief Getter pro délku v sekundách na zadaném indexu (přejmenováno pro jasnost)
 * @param index Index v seznamu
 * @param logger Reference pro logování chyb
 * @return double délka v sekundách
 */
double SamplerIO::getDurationInSeconds(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getDurationInSeconds", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
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

/**
 * @brief NOVÝ GETTER pro interleaved formát flag na zadaném indexu (přejmenováno pro konzistenci)
 * @param index Index v seznamu
 * @param logger Reference pro logování chyb
 * @return bool true pokud interleaved (standard pro WAV)
 */
bool SamplerIO::getIsInterleavedFormat(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getIsInterleavedFormat", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
        std::exit(1);
    }
    return sampleList[index].interleaved_format;
}

/**
 * @brief NOVÝ GETTER pro potřebu konverze do float na zadaném indexu
 * @param index Index v seznamu
 * @param logger Reference pro logování chyb
 * @return bool true pokud potřebuje konverzi (16-bit PCM -> float)
 */
bool SamplerIO::getNeedsConversion(int index, Logger& logger) const {
    if (index < 0 || static_cast<size_t>(index) >= sampleList.size()) {
        logger.log("SamplerIO/getNeedsConversion", "error", "Invalid index: " + std::to_string(index) + " (list size: " + std::to_string(sampleList.size()) + ")");
        std::exit(1);
    }
    return sampleList[index].needs_conversion;
}