#include "sample_rate_converter.h"

#include <cstdlib>
#include <cstring>
#include <cmath>

// speexdsp is a C library — include with C linkage
extern "C" {
#include "speex/speex_resampler.h"
}

float* SampleRateConverter::resampleStereo(
    const float* input,
    int          inputFrames,
    int          sourceRate,
    int          targetRate,
    int&         outputFrames,
    Logger&      logger)
{
    if (sourceRate == targetRate) {
        // Trivial case — no conversion needed, just copy
        outputFrames = inputFrames;
        float* out = static_cast<float*>(malloc(inputFrames * 2 * sizeof(float)));
        if (!out) {
            logger.log("SampleRateConverter/resampleStereo", LogSeverity::Critical,
                       "malloc failed for passthrough copy");
            return nullptr;
        }
        memcpy(out, input, static_cast<size_t>(inputFrames) * 2 * sizeof(float));
        return out;
    }

    // Initialize speex resampler (stereo = 2 channels, default quality = 4)
    int err = RESAMPLER_ERR_SUCCESS;
    SpeexResamplerState* resampler = speex_resampler_init(
        2,
        static_cast<spx_uint32_t>(sourceRate),
        static_cast<spx_uint32_t>(targetRate),
        SPEEX_RESAMPLER_QUALITY_DEFAULT,
        &err
    );

    if (!resampler || err != RESAMPLER_ERR_SUCCESS) {
        logger.log("SampleRateConverter/resampleStereo", LogSeverity::Critical,
                   "speex_resampler_init failed: " + std::string(speex_resampler_strerror(err)));
        return nullptr;
    }

    // Allocate output buffer with headroom for rounding
    const int headroom = 16;
    int maxOutputFrames = static_cast<int>(
        std::ceil(static_cast<double>(inputFrames) * targetRate / sourceRate)
    ) + headroom;

    float* output = static_cast<float*>(malloc(static_cast<size_t>(maxOutputFrames) * 2 * sizeof(float)));
    if (!output) {
        logger.log("SampleRateConverter/resampleStereo", LogSeverity::Critical,
                   "malloc failed for resampled output buffer (" +
                   std::to_string(maxOutputFrames * 2) + " floats)");
        speex_resampler_destroy(resampler);
        return nullptr;
    }

    spx_uint32_t inLen  = static_cast<spx_uint32_t>(inputFrames);
    spx_uint32_t outLen = static_cast<spx_uint32_t>(maxOutputFrames);

    err = speex_resampler_process_interleaved_float(resampler, input, &inLen, output, &outLen);
    speex_resampler_destroy(resampler);

    if (err != RESAMPLER_ERR_SUCCESS) {
        logger.log("SampleRateConverter/resampleStereo", LogSeverity::Error,
                   "speex_resampler_process_interleaved_float failed: " +
                   std::string(speex_resampler_strerror(err)));
        free(output);
        return nullptr;
    }

    outputFrames = static_cast<int>(outLen);

    logger.log("SampleRateConverter/resampleStereo", LogSeverity::Info,
               "Resampled " + std::to_string(inputFrames) + " frames @ " +
               std::to_string(sourceRate) + " Hz -> " +
               std::to_string(outputFrames) + " frames @ " +
               std::to_string(targetRate) + " Hz");

    return output;
}
