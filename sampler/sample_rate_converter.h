#ifndef SAMPLE_RATE_CONVERTER_H
#define SAMPLE_RATE_CONVERTER_H

#include "core_logger.h"
#include <string>

/**
 * @class SampleRateConverter
 * @brief Offline stereo resampler using speex_resampler (speexdsp).
 *
 * Designed for use at sample-bank load time (not real-time).
 * No JUCE dependency — pure C++17 + speexdsp + libsndfile.
 * Cross-platform: Windows, macOS, Linux, mbed.
 *
 * Memory contract for resampleStereo:
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

    /**
     * @brief Build the output path for a resampled WAV file.
     *
     * Replaces the frequency suffix in the filename: "fXX" → "fYY"
     * where XX = sourceRate/1000, YY = targetRate/1000.
     * Example: "m021-vel0-f48.wav" @ 48000→44100 → "m021-vel0-f44.wav"
     *
     * @param originalPath  Full path to the source WAV file
     * @param sourceRate    Source sample rate in Hz
     * @param targetRate    Target sample rate in Hz
     * @return              Full path for the resampled file, or "" on failure
     */
    static std::string buildResampledPath(
        const std::string& originalPath,
        int                sourceRate,
        int                targetRate
    );

    /**
     * @brief Save a stereo interleaved float buffer as a 32-bit float WAV file.
     *
     * @param path        Output file path
     * @param buffer      Stereo interleaved buffer (frameCount * 2 floats)
     * @param frameCount  Number of stereo frames
     * @param sampleRate  Sample rate for the WAV header
     * @param logger      Logger reference
     * @return            true on success, false on error (already logged)
     */
    static bool saveWav(
        const std::string& path,
        const float*       buffer,
        int                frameCount,
        int                sampleRate,
        Logger&            logger
    );
};

#endif // SAMPLE_RATE_CONVERTER_H
