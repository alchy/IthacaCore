#include "test_registry.h"
#include <stdexcept>

TestRegistry::TestRegistry(Logger& logger) : logger_(logger) {}

void TestRegistry::registerTest(std::unique_ptr<TestBase> test) {
    if (!test) return;
    tests_.push_back(std::move(test));
}

std::map<std::string, TestResult> TestRegistry::runAll(VoiceManager& vm, const TestConfig& cfg) {
    std::map<std::string, TestResult> out;
    for (auto& t : tests_) {
        if (!t) continue;
        try {
            // If test wants to use a specific config, we could inject; for now assume TestBase holds config.
            TestResult r = t->runTest(vm);
            out.emplace(t->getTestName(), r);
        } catch (const std::exception& e) {
            TestResult r;
            r.testName = t->getTestName();
            r.passed = false;
            r.errorMessage = e.what();
            out.emplace(r.testName, r);
        }
    }
    return out;
}
