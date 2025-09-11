#include "test_base.h"
#include "../core_logger.h"
#include "../voice_manager.h"  
#include <cstring>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <filesystem>


// TestBase implementace
TestBase::TestBase(const std::string& name, Logger& logger, const TestConfig& config)
    : testName_(name), config_(config), logger_(logger) {}

TestBase::~TestBase() = default;

const std::string& TestBase::getTestName() const { 
    return testName_; 
}

bool TestBase::shouldExportAudio() const { 
    return config_.exportAudio; 
}

std::vector<std::string> TestBase::getExportFileNames() const { 
    return {}; 
}

void TestBase::logProgress(const std::string& message) {
    try { 
        logger_.log(getTestName() + "/progress", "info", message); 
    } catch(...) {}
}

void TestBase::logTestResult(const std::string& step, bool passed, const std::string& details) {
    try {
        std::string msg = step + (passed ? " - OK" : " - FAIL");
        if (!details.empty()) msg += ": " + details;
        logger_.log(getTestName() + "/" + step, passed ? "info" : "error", msg);
    } catch(...) {}
}

AudioStats TestBase::analyzeAudioBuffer(const float* buffer, int blockSize, int channels) {
    AudioStats s;
    if (!buffer || blockSize <= 0 || channels <= 0) return s;
    
    int total = blockSize * channels;
    double sumSq = 0.0;
    float peak = 0.0f;
    
    for (int i = 0; i < total; ++i) {
        float v = buffer[i];
        peak = std::max(peak, std::fabs(v));
        sumSq += double(v) * double(v);
    }
    
    s.peakLevel = peak;
    s.rmsLevel = total > 0 ? float(std::sqrt(sumSq / total)) : 0.0f;
    return s;
}

float* TestBase::createDummyAudioBuffer(int blockSize, int channels) {
    if (blockSize <= 0 || channels <= 0) return nullptr;
    try {
        size_t samples = size_t(blockSize) * size_t(channels);
        float* buf = new float[samples];
        std::memset(buf, 0, samples * sizeof(float));
        return buf;
    } catch(...) {
        return nullptr;
    }
}

void TestBase::destroyDummyAudioBuffer(float* buffer) {
    delete[] buffer;
}

// WAV export pomocné funkce
static void write_le_u32(std::ofstream& ofs, uint32_t v) {
    ofs.put(char(v & 0xFF));
    ofs.put(char((v >> 8) & 0xFF));
    ofs.put(char((v >> 16) & 0xFF));
    ofs.put(char((v >> 24) & 0xFF));
}

static void write_le_u16(std::ofstream& ofs, uint16_t v) {
    ofs.put(char(v & 0xFF));
    ofs.put(char((v >> 8) & 0xFF));
}

bool TestBase::exportTestAudio(const std::string& filename, const float* buffer, int frames, int channels, int sampleRate) {
    if (!buffer || frames <= 0 || channels <= 0) return false;
    
    // Vytvoření export adresáře
    std::string fullDir = config_.exportDir;
    try {
        std::filesystem::create_directories(fullDir);
    } catch (...) {
        logger_.log(getTestName() + "/exportTestAudio", "error", 
                   "Failed to create export directory: " + fullDir);
        return false;
    }
    
    std::string path = fullDir + "/" + filename;
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        logger_.log(getTestName() + "/exportTestAudio", "error", 
                   "Failed to create WAV file: " + path);
        return false;
    }

    uint32_t byteRate = sampleRate * channels * 2;
    uint16_t blockAlign = channels * 2;
    uint32_t dataSize = uint32_t(frames) * channels * 2;

    // RIFF header
    ofs.write("RIFF", 4);
    write_le_u32(ofs, 36 + dataSize);
    ofs.write("WAVE", 4);
    
    // fmt chunk
    ofs.write("fmt ", 4);
    write_le_u32(ofs, 16);
    write_le_u16(ofs, 1); // PCM
    write_le_u16(ofs, uint16_t(channels));
    write_le_u32(ofs, uint32_t(sampleRate));
    write_le_u32(ofs, byteRate);
    write_le_u16(ofs, blockAlign);
    write_le_u16(ofs, 16); // bits per sample
    
    // data chunk
    ofs.write("data", 4);
    write_le_u32(ofs, dataSize);

    // write samples
    int total = frames * channels;
    for (int i = 0; i < total; ++i) {
        float f = buffer[i];
        if (f > 1.0f) f = 1.0f;
        if (f < -1.0f) f = -1.0f;
        int16_t s = int16_t(std::round(f * 32767.0f));
        write_le_u16(ofs, uint16_t(s));
    }
    
    ofs.close();
    
    logger_.log(getTestName() + "/exportTestAudio", "info", 
               "Exported WAV file: " + path + " (" + std::to_string(frames) + 
               " frames, " + std::to_string(channels) + " channels)");
    
    return true;
}

uint8_t TestBase::findValidTestMidiNote(VoiceManager& vm, uint8_t fallback) {
    // Dočasná implementace - používáme fallback dokud nemáme reálné API
    logger_.log(getTestName() + "/findValidTestMidiNote", "info", 
               "Using fallback MIDI note: " + std::to_string(fallback) + " (API placeholder)");
    return fallback;
}

std::vector<uint8_t> TestBase::findValidNotesForPolyphony(VoiceManager& vm, size_t count, uint8_t start) {
    std::vector<uint8_t> out;
    
    // Dočasná implementace - vytvoříme sekvenci not
    for (uint8_t note = start; note < 127 && out.size() < count; ++note) {
        out.push_back(note);
    }
    
    logger_.log(getTestName() + "/findValidNotesForPolyphony", "info", 
               "Generated " + std::to_string(out.size()) + " notes for polyphony test (API placeholder)");
    
    return out;
}