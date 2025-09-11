#ifndef ENVELOPE_TEST_H
#define ENVELOPE_TEST_H

#include "test_base.h"
#include <string>
#include <vector>
#include <cstdint>

class EnvelopeTest : public TestBase {
public:
    EnvelopeTest(Logger& logger, const TestConfig& config = TestConfig{});
    TestResult runTest(VoiceManager& voiceManager) override;
    bool shouldExportAudio() const override;
    std::vector<std::string> getExportFileNames() const override;

private:
    // Přidané chybějící deklarace metod
    bool analyzeAttackPhase(const std::vector<float>& envelopeGains, int attackBlocks);
    bool analyzeSustainPhase(const std::vector<float>& envelopeGains, int attackBlocks, int sustainBlocks);
    bool analyzeReleasePhase(const std::vector<float>& envelopeGains, int releaseStart, int releaseBlocks);
};

#endif // ENVELOPE_TEST_H