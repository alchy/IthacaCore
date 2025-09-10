#include "envelope_test.h"
#include "../core_logger.h"
#include "../voice_manager.h"
#include <vector>
#include <cmath>
#include <cstring>
#include <filesystem>

EnvelopeTest::EnvelopeTest(Logger& logger, const TestConfig& config)
    : TestBase("EnvelopeTest", logger, config) {}

bool EnvelopeTest::shouldExportAudio() const { return config().exportAudio; }

std::vector<std::string> EnvelopeTest::getExportFileNames() const {
    return { "envelope_attack.wav", "envelope_release.wav", "envelope_full.wav" };
}

TestResult EnvelopeTest::runTest(VoiceManager& voiceManager) {
    TestResult result;
    result.testName = getTestName();
    try {
        logProgress("Starting enhanced single note test with gain monitoring");
        uint8_t testMidi = findValidTestMidiNote(voiceManager, 60);
        uint8_t testVelocity = config().defaultTestVelocity;
        bool testPassed = true;

        logger_.log("EnvelopeTest/runTest", "info", 
                    "Testing MIDI " + std::to_string(testMidi) + 
                    " with velocity " + std::to_string(testVelocity) + 
                    " (enhanced gain monitoring)");

        Voice& voice = voiceManager.getVoice(testMidi);
        voiceManager.setNoteState(testMidi, true, testVelocity);
        voice.getGainDebugInfo(logger_);

        const int blockSize = config().exportBlockSize;
        const int numBlocks = 20; // Rozšířeno pro lepší zachycení envelope
        float* leftBuffer = createDummyAudioBuffer(blockSize, 1);
        float* rightBuffer = createDummyAudioBuffer(blockSize, 1);
        float* exportBuffer = shouldExportAudio() ? createDummyAudioBuffer(blockSize, 2) : nullptr;
        std::vector<float> attackRms;

        for (int block = 0; block < numBlocks; ++block) {
            memset(leftBuffer, 0, blockSize * sizeof(float));
            memset(rightBuffer, 0, blockSize * sizeof(float));
            bool hasAudio = voiceManager.processBlock(leftBuffer, rightBuffer, blockSize);
            AudioStats s = analyzeAudioBuffer(leftBuffer, blockSize, 1);
            attackRms.push_back(s.rmsLevel);

            if (hasAudio) {
                logger_.log("EnvelopeTest/runTest", "info", 
                            "Block " + std::to_string(block) + 
                            " - Peak: " + std::to_string(s.peakLevel) + 
                            ", RMS: " + std::to_string(s.rmsLevel) +
                            ", Voice gains: Env=" + std::to_string(voice.getCurrentEnvelopeGain()) +
                            ", Vel=" + std::to_string(voice.getVelocityGain()) +
                            ", Final=" + std::to_string(voice.getFinalGain()));
                if (shouldExportAudio() && exportBuffer) {
                    for (int i = 0; i < blockSize; ++i) {
                        exportBuffer[i * 2] = leftBuffer[i];
                        exportBuffer[i * 2 + 1] = rightBuffer[i];
                    }
                    std::string filename = (block < numBlocks / 2) ? "envelope_attack.wav" : "envelope_release.wav";
                    exportTestAudio(filename, exportBuffer, blockSize, 2, 44100);
                    logger_.log("EnvelopeTest/runTest", "info", "Exported: " + filename);
                }
            } else if (block < numBlocks / 2) {
                testPassed = false;
                logger_.log("EnvelopeTest/runTest", "error", 
                            "No audio in sustain phase at block " + std::to_string(block));
            }

            if (block == numBlocks / 2) {
                voiceManager.setNoteState(testMidi, false, 0);
                logger_.log("EnvelopeTest/runTest", "info", "Note-off sent - monitoring release envelope");
            }
        }

        if (shouldExportAudio() && exportBuffer) {
            voiceManager.setNoteState(testMidi, true, testVelocity);
            memset(leftBuffer, 0, blockSize * sizeof(float));
            memset(rightBuffer, 0, blockSize * sizeof(float));
            voiceManager.processBlock(leftBuffer, rightBuffer, blockSize);
            for (int i = 0; i < blockSize; ++i) {
                exportBuffer[i * 2] = leftBuffer[i];
                exportBuffer[i * 2 + 1] = rightBuffer[i];
            }
            exportTestAudio("envelope_full.wav", exportBuffer, blockSize, 2, 44100);
            logger_.log("EnvelopeTest/runTest", "info", "Exported: envelope_full.wav");
        }

        bool attackIncreasing = std::is_sorted(attackRms.begin(), attackRms.begin() + numBlocks / 2);
        bool releaseDecreasing = std::is_sorted(attackRms.rbegin(), attackRms.rbegin() + numBlocks / 2, std::greater<float>());
        logTestResult("attack_curve", attackIncreasing, attackIncreasing ? "attack increasing" : "attack not monotonic");
        logTestResult("release_curve", releaseDecreasing, releaseDecreasing ? "release decreasing" : "release not monotonic");

        destroyDummyAudioBuffer(leftBuffer);
        destroyDummyAudioBuffer(rightBuffer);
        if (exportBuffer) destroyDummyAudioBuffer(exportBuffer);

        result.passed = testPassed && attackIncreasing && releaseDecreasing;
        result.details = "attackRms samples: " + std::to_string(attackRms.size());
        if (!result.passed) result.errorMessage = "Envelope behaviour not as expected";
        logTestResult("envelope_test", result.passed, result.details);
    } catch (const std::exception& e) {
        result.passed = false;
        result.errorMessage = e.what();
        logTestResult("exception", false, result.errorMessage);
    }
    return result;
}