#ifndef EDGE_CASE_TEST_H
#define EDGE_CASE_TEST_H

#include "test_base.h"

class EdgeCaseTest : public TestBase {
public:
    EdgeCaseTest(Logger& logger, const TestConfig& config = TestConfig{});
    TestResult runTest(VoiceManager& voiceManager) override;
    bool shouldExportAudio() const override;
    std::vector<std::string> getExportFileNames() const override;
};

#endif // EDGE_CASE_TEST_H