/*
THIS FILE IS LOCKED, IT IS FUNCTIONAL AND WILL NOT BE CHANGED
*/

#include "wav_file_exporter.h"
#include <filesystem>   // Pro cestu k souboru
#include <cstdlib>      // Pro std::exit
#include <cstring>      // Pro memset
#include <iostream>     // Pro std::cerr při chybě (pokud potřeba)
#include <algorithm>    // Pro std::clamp (clipping)

// Konstruktor: Inicializuje složku, ukládá parametry, loguje (pokud LOG_ENABLED)
WavExporter::WavExporter(const std::string& outputDir, Logger& logger, ExportFormat exportFormat)
    : logger_(logger), outputDir_(outputDir), exportFormat_(exportFormat), channels_(0), bufferSize_(0), dummy_write_(false) {
    std::filesystem::path dirPath(outputDir_);
    if (!std::filesystem::exists(dirPath)) {
        std::filesystem::create_directory(dirPath);
        #if LOG_ENABLED
        logger_.log("WavExporter/constructor", "info", "Created output directory: " + outputDir_);
        #endif
    }
    startTime_ = std::chrono::steady_clock::now();
    #if LOG_ENABLED
    std::string formatStr = (exportFormat_ == ExportFormat::Pcm16) ? "Pcm16 (default)" : "Float";
    logger_.log("WavExporter/constructor", "info", "WavExporter initialized for directory: " + outputDir_ + ", format: " + formatStr);
    #endif
}

// wavFileCreate: Vytvoří soubor, nastaví formát (default Pcm16), alokuje buffery, loguje (pokud LOG_ENABLED)
float* WavExporter::wavFileCreate(const std::string& filename, int frequency, int bufferSize, bool stereo, bool dummy_write) {
    if (frequency <= 0 || bufferSize <= 0) {
        #if LOG_ENABLED
        logger_.log("WavExporter/wavFileCreate", "error", "Invalid params: frequency=" + std::to_string(frequency) + ", bufferSize=" + std::to_string(bufferSize));
        #endif
        std::exit(1);
    }

    dummy_write_ = !dummy_write;  // true = reálný zápis
    bufferSize_ = bufferSize;
    channels_ = stereo ? 2 : 1;

    std::filesystem::path fullPath = std::filesystem::path(outputDir_) / filename;

    memset(&sfinfo_, 0, sizeof(sfinfo_));
    sfinfo_.samplerate = frequency;
    sfinfo_.channels = channels_;

    if (exportFormat_ == ExportFormat::Float) {
        sfinfo_.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;  // 32-bit float
    } else {
        sfinfo_.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16; // 16-bit PCM (default)
    }

    if (!dummy_write_) {
        sndfile_ = sf_open(fullPath.string().c_str(), SFM_WRITE, &sfinfo_);
        if (!sndfile_) {
            #if LOG_ENABLED
            logger_.log("WavExporter/wavFileCreate", "error", "Cannot create WAV file: " + fullPath.string() + " - " + sf_strerror(nullptr));
            #endif
            std::exit(1);
        }
        #if LOG_ENABLED
        std::string formatStr = (exportFormat_ == ExportFormat::Float) ? "32-bit float" : "16-bit PCM";
        logger_.log("WavExporter/wavFileCreate", "info", "WAV file created: " + fullPath.string() + " (freq=" + std::to_string(frequency) + " Hz, channels=" + std::to_string(channels_) + ", format=" + formatStr + ")");
        #endif
    } else {
        #if LOG_ENABLED
        std::string formatStr = (exportFormat_ == ExportFormat::Float) ? "Float" : "Pcm16";
        logger_.log("WavExporter/wavFileCreate", "info", "Dummy mode (" + formatStr + "): No file created, measuring copy time only");
        #endif
    }

    // Alokace float bufferu (vždy float pro vstup)
    size_t bufferBytes = static_cast<size_t>(bufferSize_) * channels_ * sizeof(float);
    buffer_ = static_cast<float*>(malloc(bufferBytes));
    if (!buffer_) {
        #if LOG_ENABLED
        logger_.log("WavExporter/wavFileCreate", "error", "Memory allocation failed for float buffer: " + std::to_string(bufferBytes) + " bytes");
        #endif
        if (sndfile_) sf_close(sndfile_);
        std::exit(1);
    }

    // Pro Pcm16: Alokace temp int16 (interní)
    if (exportFormat_ == ExportFormat::Pcm16) {
        size_t pcmBytes = static_cast<size_t>(bufferSize_) * channels_ * sizeof(int16_t);
        tempPcmBuffer_ = static_cast<int16_t*>(malloc(pcmBytes));
        if (!tempPcmBuffer_) {
            #if LOG_ENABLED
            logger_.log("WavExporter/wavFileCreate", "error", "Memory allocation failed for Pcm16 temp buffer: " + std::to_string(pcmBytes) + " bytes");
            #endif
            free(buffer_);
            if (sndfile_) sf_close(sndfile_);
            std::exit(1);
        }
    }

    memset(buffer_, 0, bufferBytes);

    #if LOG_ENABLED
    logger_.log("WavExporter/wavFileCreate", "info", "Float buffer allocated: " + std::to_string(bufferSize_) + " samples, " + std::to_string(channels_) + " channels");
    #endif
    return buffer_;
}

// wavFileWriteBuffer: Zapisuje/ konvertuje, měří čas (vždy), loguje (pokud LOG_ENABLED)
bool WavExporter::wavFileWriteBuffer(float* buffer_ptr, int buffer_size) {
    if (!buffer_ptr || buffer_size <= 0 || buffer_size > bufferSize_) {
        #if LOG_ENABLED
        logger_.log("WavExporter/wavFileWriteBuffer", "error", "Invalid buffer or size: " + std::to_string(buffer_size));
        #endif
        return false;
    }

    auto start = std::chrono::steady_clock::now();
    int framesToWrite = buffer_size;

    if (!dummy_write_) {
        if (exportFormat_ == ExportFormat::Float) {
            int framesWritten = sf_writef_float(sndfile_, buffer_ptr, framesToWrite);
            if (framesWritten != framesToWrite) {
                #if LOG_ENABLED
                logger_.log("WavExporter/wavFileWriteBuffer", "error", "Float write error: expected " + std::to_string(framesToWrite) + " frames, wrote " + std::to_string(framesWritten));
                #endif
                return false;
            }
        } else {  // Pcm16 (default)
            convertFloatToInt16(buffer_ptr, tempPcmBuffer_, static_cast<int>(framesToWrite * channels_));
            int framesWritten = sf_writef_short(sndfile_, tempPcmBuffer_, framesToWrite);
            if (framesWritten != framesToWrite) {
                #if LOG_ENABLED
                logger_.log("WavExporter/wavFileWriteBuffer", "error", "Pcm16 write error: expected " + std::to_string(framesToWrite) + " frames, wrote " + std::to_string(framesWritten));
                #endif
                return false;
            }
        }
    } else {
        // Dummy: Simulace (měření konverze/kopírování)
        if (exportFormat_ == ExportFormat::Pcm16) {
            convertFloatToInt16(buffer_ptr, tempPcmBuffer_, static_cast<int>(framesToWrite * channels_));
        } else {
            float* temp = static_cast<float*>(malloc(static_cast<size_t>(framesToWrite) * channels_ * sizeof(float)));
            if (temp) {
                memcpy(temp, buffer_ptr, static_cast<size_t>(framesToWrite) * channels_ * sizeof(float));
                free(temp);
            }
        }
    }

    // Log času (pokud LOG_ENABLED)
    #if LOG_ENABLED
    std::string op = (exportFormat_ == ExportFormat::Float) ? "wavFileWriteBuffer (float, " : "wavFileWriteBuffer (Pcm16 conversion, ";
    op += std::to_string(buffer_size) + " samples)";
    logTime(op, start);
    #endif
    return true;
}

// Destruktor: Uzavře soubor, uvolní paměť, loguje (pokud LOG_ENABLED)
WavExporter::~WavExporter() {
    if (buffer_) {
        free(buffer_);
        buffer_ = nullptr;
    }
    if (tempPcmBuffer_) {
        free(tempPcmBuffer_);
        tempPcmBuffer_ = nullptr;
    }
    if (sndfile_) {
        sf_close(sndfile_);
        sndfile_ = nullptr;
    }

    auto endTime = std::chrono::steady_clock::now();
    auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime_).count();
    #if LOG_ENABLED
    std::string formatStr = (exportFormat_ == ExportFormat::Pcm16) ? "Pcm16" : "Float";
    logger_.log("WavExporter/destructor", "info", "Export completed (" + formatStr + "). Total time: " + std::to_string(totalMs) + " ms");
    #endif
}

void WavExporter::logTime(const std::string& operation, std::chrono::steady_clock::time_point start) {
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    logger_.log("WavExporter/" + operation, "info", "Time: " + std::to_string(ms) + " ms");
}

// convertFloatToInt16: Škáluje a clipuje (bez ditheringu)
// SKonverze float → int16 s manuálním clippingem (pro kompatibilitu s MSVC)
void WavExporter::convertFloatToInt16(float* src, int16_t* dst, int numSamples) {
    static constexpr int16_t MAX_INT16 = 32767;
    static constexpr int16_t MIN_INT16 = -32768;
    for (int i = 0; i < numSamples; ++i) {
        float scaled = src[i] * MAX_INT16;  // Škálování -1.0..1.0 → -32767..32767
        int32_t temp = static_cast<int32_t>(scaled);  // Dočasný int32_t pro bezpečný cast
        int16_t clamped;
        if (temp > MAX_INT16) {
            clamped = MAX_INT16;  // Clipping nahoru
        } else if (temp < MIN_INT16) {
            clamped = MIN_INT16;  // Clipping dolů
        } else {
            clamped = static_cast<int16_t>(temp);  // Bez clippingu
        }
        dst[i] = clamped;
    }
}