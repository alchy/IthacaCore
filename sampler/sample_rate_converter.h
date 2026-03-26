#ifndef SAMPLE_RATE_CONVERTER_H
#define SAMPLE_RATE_CONVERTER_H

#include "core_logger.h"

/**
 * @class SampleRateConverter
 * @brief Offline stereo resampler using speex_resampler (speexdsp).
 *
 * Designed for use at sample-bank load time (not real-time).
 * No JUCE dependency — pure C++17 + speexdsp (C library).
 * Cross-platform: Windows, macOS, Linux, mbed.
 *
 * Memory contract:
 *   Returned buffer is malloc'd — caller must free() it.
 *   On failure returns nullptr (error already logged).
 */
class SampleRateConverter {
public:
    /**
     * @brief Resample a stereo interleaved float buffer.
     *
     * Input/output format: [L0,R0, L1,R1, ...] (interleaved stereo float)
     *
     * @param input        Source buffer (inputFrames * 2 floats)
     * @param inputFrames  Number of stereo frames in input
     * @param sourceRate   Source sample rate in Hz (e.g. 48000)
     * @param targetRate   Target sample rate in Hz (e.g. 44100)
     * @param outputFrames [out] Actual number of stereo frames written to output
     * @param logger       Logger reference (file-only, no console for non-fatal)
     * @return             Newly malloc'd buffer with outputFrames*2 floats, or nullptr on error
     */
    static float* resampleStereo(
        const float* input,
        int          inputFrames,
        int          sourceRate,
        int          targetRate,
        int&         outputFrames,
        Logger&      logger
    );
};

#endif // SAMPLE_RATE_CONVERTER_H
