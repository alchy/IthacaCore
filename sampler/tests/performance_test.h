#ifndef PERFORMANCE_TEST_H
#define PERFORMANCE_TEST_H

#include "test_base.h"
#include <vector>

class PerformanceTest : public TestBase {
public:
    PerformanceTest(Logger& logger, const TestConfig& config = TestConfig{});
    TestResult runTest(VoiceManager& voiceManager) override;
    bool shouldExportAudio() const override { return false; }
};

#endif // PERFORMANCE_TEST_H