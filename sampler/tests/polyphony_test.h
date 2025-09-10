#ifndef POLYPHONY_TEST_H
#define POLYPHONY_TEST_H

#include "test_base.h"
#include <vector>

class PolyphonyTest : public TestBase {
public:
    PolyphonyTest(Logger& logger, const TestConfig& config = TestConfig{});
    TestResult runTest(VoiceManager& voiceManager) override;
    bool shouldExportAudio() const override;
    std::vector<std::string> getExportFileNames() const override;
};

#endif // POLYPHONY_TEST_H