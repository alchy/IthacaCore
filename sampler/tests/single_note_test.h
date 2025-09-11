#ifndef SINGLE_NOTE_TEST_H
#define SINGLE_NOTE_TEST_H

#include "test_base.h"

class SingleNoteTest : public TestBase {
public:
    SingleNoteTest(Logger& logger, const TestConfig& config = TestConfig{});
    TestResult runTest(VoiceManager& voiceManager) override;
    bool shouldExportAudio() const override;
    std::vector<std::string> getExportFileNames() const override;

private:
    // Přidané chybějící deklarace metod
    bool testVelocityResponse(VoiceManager& voiceManager, uint8_t testMidi);
    bool testVoiceStateTransitions(VoiceManager& voiceManager, uint8_t testMidi);
};

#endif // SINGLE_NOTE_TEST_H