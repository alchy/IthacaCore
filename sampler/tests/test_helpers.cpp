#include <cmath>
#include <algorithm>
#include <cstring>
#include <filesystem>

#include "test_helpers.h"
#include "../wav_file_exporter.h"

void exportTestAudio(const std::string& filename, const float* data, int numFrames, int channels, int sampleRate, Logger& logger) {
    try {
        // Získání export cesty pomocí helper funkce
        std::string exportPath = createTestExportDirectory(logger);
        if (exportPath.empty()) {
            logger.log("exportTestAudio", "error", "Failed to create or access export directory");
            return;
        }
        
        // Kontrola platnosti dat
        if (!data || numFrames <= 0 || channels <= 0) {
            logger.log("exportTestAudio", "error", "Invalid audio data parameters for file: " + filename);
            return;
        }
        
        logger.log("exportTestAudio", "info", "Attempting to export WAV: " + filename + 
                   " (frames: " + std::to_string(numFrames) + ", channels: " + std::to_string(channels) + 
                   ", sampleRate: " + std::to_string(sampleRate) + ")");
        logger.log("exportTestAudio", "info", "Export path: " + exportPath);
        
        // Inicializace WavExporter s dynamickou cestou a formátem Float
        WavExporter exporter(exportPath, logger, ExportFormat::Float);
        float* buffer = exporter.wavFileCreate(filename, sampleRate, numFrames, channels == 2, true);  // Reálný zápis
        
        if (!buffer) {
            logger.log("exportTestAudio", "error", "Failed to create WAV buffer for: " + filename);
            return;
        }
        
        // Kopírování dat do bufferu (interleaved)
        std::memcpy(buffer, data, static_cast<size_t>(numFrames) * channels * sizeof(float));
        
        bool success = exporter.wavFileWriteBuffer(buffer, numFrames);
        if (!success) {
            logger.log("exportTestAudio", "error", "Failed to write WAV buffer: " + filename);
        } else {
            logger.log("exportTestAudio", "info", "Successfully exported WAV: " + exportPath + filename);
        }
        // Destruktor exporteru se postará o close a free
        
    } catch (const std::exception& e) {
        logger.log("exportTestAudio", "error", "Exception during WAV export: " + filename + " - " + std::string(e.what()));
    } catch (...) {
        logger.log("exportTestAudio", "error", "Unknown exception during WAV export: " + filename);
    }
}

int calculateBlocksForDuration(double durationSec, int sampleRate, int blockSize) {
    return static_cast<int>(std::ceil(durationSec * sampleRate / blockSize));
}

bool analyzeAttackPhase(const std::vector<float>& envelopeGains, int attackBlocks, Logger& logger) {
    if (envelopeGains.size() < static_cast<size_t>(attackBlocks) || attackBlocks <= 1) {
        return false;
    }
    
    int increasingCount = 0;
    for (int i = 1; i < attackBlocks; ++i) {
        if (envelopeGains[i] >= envelopeGains[i - 1]) {
            increasingCount++;
        }
    }
    
    float ratio = static_cast<float>(increasingCount) / (attackBlocks - 1);
    bool ok = ratio >= 0.7f;
    
    logger.log("analyzeAttackPhase", "info", 
               "Attack: " + std::to_string(increasingCount) + "/" + std::to_string(attackBlocks - 1) + 
               " increasing (ratio: " + std::to_string(ratio) + ")");
    
    return ok;
}

bool analyzeSustainPhase(const std::vector<float>& envelopeGains, int attackBlocks, int sustainBlocks, Logger& logger) {
    int start = attackBlocks;
    int end = attackBlocks + sustainBlocks;
    
    if (envelopeGains.size() < static_cast<size_t>(end) || sustainBlocks <= 1) {
        return false;
    }
    
    float sustainLevel = envelopeGains[start];
    float maxVariation = 0.0f;
    
    for (int i = start; i < end; ++i) {
        maxVariation = std::max(maxVariation, std::abs(envelopeGains[i] - sustainLevel));
    }
    
    bool ok = maxVariation <= 0.2f;
    
    logger.log("analyzeSustainPhase", "info", 
               "Sustain: level=" + std::to_string(sustainLevel) + 
               ", max variation=" + std::to_string(maxVariation));
    
    return ok;
}

bool analyzeReleasePhase(const std::vector<float>& envelopeGains, int releaseStart, int releaseBlocks, Logger& logger) {
    if (envelopeGains.size() < static_cast<size_t>(releaseStart + releaseBlocks) || releaseBlocks <= 1) {
        return false;
    }
    
    int decreasingCount = 0;
    for (int i = releaseStart + 1; i < releaseStart + releaseBlocks; ++i) {
        if (envelopeGains[i] <= envelopeGains[i - 1]) {
            decreasingCount++;
        }
    }
    
    float ratio = static_cast<float>(decreasingCount) / (releaseBlocks - 1);
    bool ok = ratio >= 0.7f;
    
    logger.log("analyzeReleasePhase", "info", 
               "Release: " + std::to_string(decreasingCount) + "/" + std::to_string(releaseBlocks - 1) + 
               " decreasing (ratio: " + std::to_string(ratio) + ")");
    
    return ok;
}

std::string createTestExportDirectory(Logger& logger) {
    // Definice cesty k exportnímu adresáři (relativní k aktuálnímu pracovnímu adresáři)
    std::filesystem::path exportDir = "test-exports";

    try {
        // Kontrola existence adresáře
        if (!std::filesystem::exists(exportDir)) {
            // Vytvoření adresáře, pokud neexistuje
            std::filesystem::create_directory(exportDir);
            logger.log("createTestExportDirectory", "info", "Vytvořen exportní adresář: " + exportDir.string());
        } else {
            logger.log("createTestExportDirectory", "info", "Exportní adresář již existuje: " + exportDir.string());
        }

        // Vrácení plné cesty s trailing lomítkem pro snadné přidávání souborů
        return exportDir.string() + "/";
    } catch (const std::exception& e) {
        // Logování chyby při selhání
        logger.log("createTestExportDirectory", "error", "Selhalo vytvoření exportního adresáře: " + std::string(e.what()));
        return "";
    } catch (...) {
        // Logování neznámé chyby
        logger.log("createTestExportDirectory", "error", "Selhalo vytvoření exportního adresáře: neznámá chyba");
        return "";
    }
}
