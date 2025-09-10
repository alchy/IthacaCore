#ifndef VELOCITY_GAIN_TEST_H
#define VELOCITY_GAIN_TEST_H

#include "test_base.h"
#include <vector>
#include <string>

class VelocityGainTest : public TestBase {
public:
    VelocityGainTest(Logger& logger, const TestConfig& config = TestConfig{});
    TestResult runTest(VoiceManager& voiceManager) override;
    bool shouldExportAudio() const override;
    std::vector<std::string> getExportFileNames() const override;
};

#endif // VELOCITY_GAIN_TEST_H
