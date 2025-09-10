#ifndef MASTER_GAIN_TEST_H
#define MASTER_GAIN_TEST_H

#include "test_base.h"
#include <vector>
#include <string>

struct MasterGainTestData {
    float masterGain = 0.0f;
    float measuredLevel = 0.0f;
    bool passed = false;
};

class MasterGainTest : public TestBase {
public:
    MasterGainTest(Logger& logger, const TestConfig& config = TestConfig{});
    TestResult runTest(VoiceManager& voiceManager) override;
    bool shouldExportAudio() const override;
    std::vector<std::string> getExportFileNames() const override;

private:
    bool testSingleMasterGain(VoiceManager& voiceManager, float masterGain, uint8_t testMidi);
    bool testMasterGainVelocityInteraction(VoiceManager& voiceManager, uint8_t testMidi);
    bool verifyMasterGainLinearity(const std::vector<float>& gains, const std::vector<float>& levels);

    std::vector<MasterGainTestData> testResults_;
};

#endif // MASTER_GAIN_TEST_H