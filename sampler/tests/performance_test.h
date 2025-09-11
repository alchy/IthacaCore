#ifndef PERFORMANCE_TEST_H
#define PERFORMANCE_TEST_H

#include "test_base.h"
#include <vector>

class PerformanceTest : public TestBase {
public:
    PerformanceTest(Logger& logger, const TestConfig& config = TestConfig{});
    TestResult runTest(VoiceManager& voiceManager) override;
    bool shouldExportAudio() const override { return false; } // Performance testy neexportují audio

private:
    // Přidané chybějící deklarace metod
    bool testBasicThroughput(VoiceManager& voiceManager);
    bool testPolyphonicPerformance(VoiceManager& voiceManager);
    bool testMemoryPerformance(VoiceManager& voiceManager);
    bool testRealtimeStability(VoiceManager& voiceManager);
    bool analyzePerformanceScaling(const std::vector<PerformanceMetrics>& metrics);
};

#endif // PERFORMANCE_TEST_H