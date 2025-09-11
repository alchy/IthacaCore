#ifndef TEST_REGISTRY_H
#define TEST_REGISTRY_H

#include "test_base.h"
#include <string>
#include <vector>
#include <memory>
#include <map>

class Logger;

class TestRegistry {
public:
    TestRegistry(Logger& logger);
    ~TestRegistry() = default;

    void registerTest(std::unique_ptr<TestBase> test);
    std::map<std::string, TestResult> runAll(VoiceManager& vm, const TestConfig& cfg);

private:
    Logger& logger_;
    std::vector<std::unique_ptr<TestBase>> tests_;
};

#endif // TEST_REGISTRY_H
