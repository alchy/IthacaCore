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

private:
    // Přidané chybějící deklarace metod
    bool testBasicChord(VoiceManager& voiceManager);
    bool testNoteProgression(VoiceManager& voiceManager);
    bool testPolyphonyStress(VoiceManager& voiceManager);
    bool testVoiceManagement(VoiceManager& voiceManager);
};

#endif // POLYPHONY_TEST_H