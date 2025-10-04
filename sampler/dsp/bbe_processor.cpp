/**
 * @file bbe_processor.cpp
 * @brief Implementation of BBE Sound Processor
 *
 * This file implements the core BBE audio enhancement algorithm including:
 * - Filter initialization and coefficient updates
 * - 3-band crossover processing (Linkwitz-Riley 4th order)
 * - Phase compensation (all-pass filters)
 * - Dynamic harmonic enhancement (envelope-following VCA)
 * - Bass boost (low-shelf filter, 0-12 dB)
 * - Signal recombination
 *
 * Optimizations:
 * - Block processing for filters (better cache locality)
 * - Merged phase processing loops (reduced memory access)
 * - Auto-bypass when definition = 0 (CPU savings)
 * - Cached enable flags (avoid atomic loads per sample)
 *
 * @author IthacaCore Audio Team
 * @version 1.0.0
 * @date 2025
 */

#include "bbe_processor.h"
#include <cstring>
#include <vector>

// INITIALIZATION
void BBEProcessor::prepare(int sampleRate) noexcept {
    sampleRate_ = sampleRate;
    
    for (int ch = 0; ch < 2; ++ch) {
        crossover_[ch].lpBass1.setCoefficients(
            BiquadFilter::Type::LOWPASS, sampleRate, BASS_CUTOFF, 0.707);
        crossover_[ch].lpBass2.setCoefficients(
            BiquadFilter::Type::LOWPASS, sampleRate, BASS_CUTOFF, 0.707);
        crossover_[ch].hpMid1.setCoefficients(
            BiquadFilter::Type::HIGHPASS, sampleRate, BASS_CUTOFF, 0.707);
        crossover_[ch].hpMid2.setCoefficients(
            BiquadFilter::Type::HIGHPASS, sampleRate, BASS_CUTOFF, 0.707);
        crossover_[ch].lpMid1.setCoefficients(
            BiquadFilter::Type::LOWPASS, sampleRate, TREBLE_CUTOFF, 0.707);
        crossover_[ch].lpMid2.setCoefficients(
            BiquadFilter::Type::LOWPASS, sampleRate, TREBLE_CUTOFF, 0.707);
        crossover_[ch].hpTreble1.setCoefficients(
            BiquadFilter::Type::HIGHPASS, sampleRate, TREBLE_CUTOFF, 0.707);
        crossover_[ch].hpTreble2.setCoefficients(
            BiquadFilter::Type::HIGHPASS, sampleRate, TREBLE_CUTOFF, 0.707);
        phaseShifters_[ch].midPhase.setCoefficients(
            BiquadFilter::Type::ALLPASS_180, sampleRate, MID_CENTER);
        phaseShifters_[ch].treblePhase.setCoefficients(
            BiquadFilter::Type::ALLPASS_360, sampleRate, TREBLE_CENTER);
        bassBoost_[ch].setCoefficients(
            BiquadFilter::Type::LOW_SHELF, sampleRate, BASS_CUTOFF, 0.707, 0.0);
        enhancer_[ch].prepare(sampleRate);
    }
    
    lastDefinition_ = -1.0f;
    lastBassBoost_ = -1.0f;
}

void BBEProcessor::processBlock(float* left, float* right, int samples) noexcept {
    // STEP 1: Manual bypass check
    if (bypassed_.load(std::memory_order_relaxed)) {
        return;  // Early exit - manual bypass
    }

    // STEP 2: Update coefficients if parameters changed
    updateCoefficients();

    // STEP 3: Auto-bypass if definition = 0 (no enhancement)
    if (!definitionEnabled_) {
        return;  // Early exit - auto-bypass (definition = 0)
    }

    // STEP 4: Process each channel independently
    processChannel(left, samples, 0);   // Channel 0 = left
    processChannel(right, samples, 1);  // Channel 1 = right
}

void BBEProcessor::processChannel(float* buffer, int samples, int channelIndex) noexcept {
    static thread_local std::vector<float> bassBand(16384);
    static thread_local std::vector<float> midBand(16384);
    static thread_local std::vector<float> trebleBand(16384);

    if (bassBand.size() < static_cast<size_t>(samples)) {
        return;
    }

    CrossoverFilters& xover = crossover_[channelIndex];
    PhaseShifters& phase = phaseShifters_[channelIndex];

    xover.lpBass1.processBlock(buffer, bassBand.data(), samples);
    xover.lpBass2.processBlock(bassBand.data(), bassBand.data(), samples);
    xover.hpMid1.processBlock(buffer, midBand.data(), samples);
    xover.hpMid2.processBlock(midBand.data(), midBand.data(), samples);
    xover.lpMid1.processBlock(midBand.data(), midBand.data(), samples);
    xover.lpMid2.processBlock(midBand.data(), midBand.data(), samples);
    xover.hpTreble1.processBlock(buffer, trebleBand.data(), samples);
    xover.hpTreble2.processBlock(trebleBand.data(), trebleBand.data(), samples);

    for (int i = 0; i < samples; ++i) {
        midBand[i] = phase.midPhase.processSample(midBand[i]);
        trebleBand[i] = phase.treblePhase.processSample(trebleBand[i]);
    }

    enhancer_[channelIndex].processBlock(trebleBand.data(), samples);

    if (bassBoostEnabled_) {
        bassBoost_[channelIndex].processBlock(bassBand.data(), bassBand.data(), samples);
    }

    // Recombine bands (Linkwitz-Riley property ensures flat magnitude response)
    for (int i = 0; i < samples; ++i) {
        buffer[i] = bassBand[i] + midBand[i] + trebleBand[i];
    }
}

void BBEProcessor::updateCoefficients() noexcept {
    const float currentDefinition = definitionLevel_.load(std::memory_order_relaxed);
    const float currentBassBoost = bassBoostLevel_.load(std::memory_order_relaxed);

    // UPDATE DEFINITION LEVEL
    if (currentDefinition != lastDefinition_) {
        for (int ch = 0; ch < 2; ++ch) {
            enhancer_[ch].setDefinitionLevel(currentDefinition);
        }
        definitionEnabled_ = (currentDefinition > 0.0f);
        lastDefinition_ = currentDefinition;
    }

    // UPDATE BASS BOOST LEVEL
    if (currentBassBoost != lastBassBoost_) {
        // Convert 0.0-1.0 range to 0-12 dB gain
        const float gainDB = currentBassBoost * 12.0f;

        // Update both channel bass boost filters
        for (int ch = 0; ch < 2; ++ch) {
            bassBoost_[ch].setCoefficients(
                BiquadFilter::Type::LOW_SHELF,
                sampleRate_,
                BASS_CUTOFF,
                0.707,
                gainDB
            );
        }

        // Cache enabled flag to avoid atomic loads in processChannel()
        bassBoostEnabled_ = (currentBassBoost > 0.01f);
        lastBassBoost_ = currentBassBoost;
    }
}

void BBEProcessor::reset() noexcept {
    for (int ch = 0; ch < 2; ++ch) {
        crossover_[ch].lpBass1.reset();
        crossover_[ch].lpBass2.reset();
        crossover_[ch].hpMid1.reset();
        crossover_[ch].hpMid2.reset();
        crossover_[ch].lpMid1.reset();
        crossover_[ch].lpMid2.reset();
        crossover_[ch].hpTreble1.reset();
        crossover_[ch].hpTreble2.reset();
        phaseShifters_[ch].midPhase.reset();
        phaseShifters_[ch].treblePhase.reset();
        bassBoost_[ch].reset();
        enhancer_[ch].reset();
    }
}