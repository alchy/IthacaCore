/**
 * @file sine_wave_generator.cpp
 * @brief Implementation of sine wave generator
 */

#include "sine_wave_generator.h"
#include <algorithm> // for std::min

// ============================================================================
// Public Methods
// ============================================================================

std::vector<float> SineWaveGenerator::generateStereoSine(
    uint8_t midiNote,
    int velocityLayer,
    int totalVelocityLayers,
    int sampleRate,
    float durationSeconds)
{
    // Calculate frequency from MIDI note
    float frequency = midiNoteToFrequency(midiNote);

    // Calculate amplitude from velocity layer
    float amplitude = velocityLayerToAmplitude(velocityLayer, totalVelocityLayers);

    // Calculate total samples (stereo frames * 2)
    int stereoFrames = static_cast<int>(sampleRate * durationSeconds);
    int totalSamples = stereoFrames * 2; // Interleaved L/R

    // Calculate fade samples
    int fadeInSamples = static_cast<int>(sampleRate * FADE_IN_DURATION);
    int fadeOutSamples = static_cast<int>(sampleRate * FADE_OUT_DURATION);

    // Allocate buffer
    std::vector<float> buffer(totalSamples);

    // Generate interleaved stereo sine wave
    for (int frame = 0; frame < stereoFrames; ++frame) {
        float time = static_cast<float>(frame) / static_cast<float>(sampleRate);
        float phase = 2.0f * static_cast<float>(M_PI) * frequency * time;

        // Left channel: standard sine
        float leftSample = amplitude * std::sin(phase);

        // Right channel: sine with slight phase offset for stereo width
        float rightSample = amplitude * std::sin(phase + STEREO_PHASE_OFFSET);

        // Apply fade-in
        if (frame < fadeInSamples) {
            float fadeMult = applyFadeIn(frame, fadeInSamples);
            leftSample *= fadeMult;
            rightSample *= fadeMult;
        }

        // Apply fade-out
        if (frame >= stereoFrames - fadeOutSamples) {
            float fadeMult = applyFadeOut(frame, stereoFrames, fadeOutSamples);
            leftSample *= fadeMult;
            rightSample *= fadeMult;
        }

        // Store interleaved [L,R,L,R,...]
        buffer[frame * 2 + 0] = leftSample;
        buffer[frame * 2 + 1] = rightSample;
    }

    return buffer;
}

float SineWaveGenerator::midiNoteToFrequency(uint8_t midiNote) {
    // MIDI note 69 = A4 = 440 Hz
    // Formula: f = 440 * 2^((midiNote - 69) / 12)
    return 440.0f * std::pow(2.0f, (static_cast<float>(midiNote) - 69.0f) / 12.0f);
}

float SineWaveGenerator::velocityLayerToAmplitude(int layer, int totalLayers) {
    // Linear mapping: layer 1 → 1/totalLayers, layer N → N/totalLayers
    // Ensures layer 1 is audible (not 0.0) and layer N is full amplitude (1.0)
    if (totalLayers <= 0) totalLayers = 1; // Safety
    if (layer < 1) layer = 1;              // Safety
    if (layer > totalLayers) layer = totalLayers; // Safety

    return static_cast<float>(layer) / static_cast<float>(totalLayers);
}

// ============================================================================
// Private Methods
// ============================================================================

float SineWaveGenerator::applyFadeIn(int sampleIndex, int fadeInSamples) {
    if (fadeInSamples <= 0) return 1.0f; // No fade
    if (sampleIndex >= fadeInSamples) return 1.0f; // Past fade region

    // Linear fade from 0.0 to 1.0
    return static_cast<float>(sampleIndex) / static_cast<float>(fadeInSamples);
}

float SineWaveGenerator::applyFadeOut(int sampleIndex, int totalSamples, int fadeOutSamples) {
    if (fadeOutSamples <= 0) return 1.0f; // No fade

    int fadeStartSample = totalSamples - fadeOutSamples;
    if (sampleIndex < fadeStartSample) return 1.0f; // Before fade region

    // Linear fade from 1.0 to 0.0
    int samplesIntoFade = sampleIndex - fadeStartSample;
    return 1.0f - (static_cast<float>(samplesIntoFade) / static_cast<float>(fadeOutSamples));
}
