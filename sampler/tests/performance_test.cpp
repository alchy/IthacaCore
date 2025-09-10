#include "performance_test.h"
#include "../core_logger.h"
#include "../voice_manager.h"
#include <chrono>
#include <thread>

PerformanceTest::PerformanceTest(Logger& logger, const TestConfig& config)
    : TestBase("PerformanceTest", logger, config) {}

TestResult PerformanceTest::runTest(VoiceManager& voiceManager) {
    TestResult result;
    result.testName = getTestName();
    try {
        logProgress("Starting performance measurement");
        const int iterations = 200;
        const int blockSize = config().exportBlockSize;
        float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
        float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
        using clk = std::chrono::high_resolution_clock;
        auto t0 = clk::now();
        for (int i = 0; i < iterations; ++i) {
            voiceManager.processBlock(leftBuffer, rightBuffer, blockSize);
        }
        auto t1 = clk::now();
        destroyDummyAudioBuffer(leftBuffer);
        destroyDummyAudioBuffer(rightBuffer);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        result.passed = true;
        result.details = "Processed " + std::to_string(iterations) + " blocks in " + std::to_string(ms) + " ms";
        logTestResult("throughput", true, result.details);
    } catch (const std::exception& e) {
        result.passed = false;
        result.errorMessage = e.what();
        logTestResult("exception", false, result.errorMessage);
    }
    return result;
}