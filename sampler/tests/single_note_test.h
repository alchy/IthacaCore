#ifndef SINGLE_NOTE_TEST_H
#define SINGLE_NOTE_TEST_H

#include "test_base.h"

class SingleNoteTest : public TestBase {
public:
    SingleNoteTest(Logger& logger, const TestConfig& config = TestConfig{});
    TestResult runTest(VoiceManager& voiceManager) override;
    bool shouldExportAudio() const override;
    std::vector<std::string> getExportFileNames() const override;
};

#endif // SINGLE_NOTE_TEST_H