#include "test_base.h"
#include <cstring>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <iostream>

class Logger {
public:
    void log(const std::string& scope, const std::string& level, const std::string& msg) {
        // Minimal logging to stderr to aid debugging when running tests.
        (void)scope;
        std::cerr << "[" << level << "] " << msg << std::endl;
    }
    void log(const std::string& scope, const std::string& level, const std::string& msg, int current, int total) {
        (void)scope; (void)current; (void)total;
        std::cerr << "[" << level << "] " << msg << " (" << current << "/" << total << ")" << std::endl;
    }
};

class VoiceManager {
public:
    void resetAllVoices(Logger&) {}
    void setNoteState(uint8_t, bool, int) {}
    int getActiveVoicesCount() const { return 0; }
    bool processBlock(float*, float*, int) { return false; }
    int getNumVoices() const { return 16; }
    void setMasterGain(float, Logger&) {}
    float getMasterGain() const { return 1.0f; }
    struct DummyVoice {
        bool isActive() const { return false; }
        float getVelocityGain() const { return 1.0f; }
        float getEnvelopeGain() const { return 0.0f; }
    };
    DummyVoice& getVoice(uint8_t) {
        static DummyVoice v;
        return v;
    }
    void setEnvelopeFrequency(int, Logger&) {}
};

// TestBase definitions
TestBase::TestBase(const std::string& name, Logger& logger, const TestConfig& config)
    : testName_(name), config_(config), logger_(logger) {}

TestBase::~TestBase() = default;
const std::string& TestBase::getTestName() const { return testName_; }

bool TestBase::shouldExportAudio() const { return config_.exportAudio; }
std::vector<std::string> TestBase::getExportFileNames() const { return {}; }

void TestBase::logProgress(const std::string& message) {
    try { logger_.log(getTestName(), "progress", message); } catch(...) {}
}

void TestBase::logTestResult(const std::string& step, bool passed, const std::string& details) {
    try {
        std::string msg = step + (passed ? " - OK" : " - FAIL");
        if (!details.empty()) msg += ": " + details;
        logger_.log(getTestName(), passed ? "info" : "error", msg);
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
        sumSq += double(v)*double(v);
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

// Simple WAV writer: 16-bit PCM, little endian.
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
    std::string dir = config_.exportDir;
    if (dir.empty()) dir = ".";
    // ensure dir exists is not implemented here (depends on platform)
    std::string path = dir + "/" + filename;
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;

    uint32_t byteRate = sampleRate * channels * 2;
    uint16_t blockAlign = channels * 2;
    uint32_t dataSize = uint32_t(frames) * channels * 2;

    // RIFF header
    ofs.write("RIFF",4);
    write_le_u32(ofs, 36 + dataSize);
    ofs.write("WAVE",4);
    // fmt chunk
    ofs.write("fmt ",4);
    write_le_u32(ofs, 16); // fmt chunk size
    write_le_u16(ofs, 1); // PCM
    write_le_u16(ofs, uint16_t(channels));
    write_le_u32(ofs, uint32_t(sampleRate));
    write_le_u32(ofs, byteRate);
    write_le_u16(ofs, blockAlign);
    write_le_u16(ofs, 16); // bits per sample
    // data chunk
    ofs.write("data",4);
    write_le_u32(ofs, dataSize);

    // write samples (clamp and convert)
    int total = frames * channels;
    for (int i = 0; i < total; ++i) {
        float f = buffer[i];
        if (f > 1.0f) f = 1.0f;
        if (f < -1.0f) f = -1.0f;
        int16_t s = int16_t(std::round(f * 32767.0f));
        write_le_u16(ofs, uint16_t(s));
    }
    ofs.close();
    return true;
}

uint8_t TestBase::findValidTestMidiNote(VoiceManager& vm, uint8_t fallback) {
    (void)vm;
    return fallback;
}

std::vector<uint8_t> TestBase::findValidNotesForPolyphony(VoiceManager& vm, size_t count, uint8_t start) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < count; ++i) out.push_back(uint8_t(start + i));
    return out;
}
