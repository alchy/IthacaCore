/**
 * @file bbe_processor.cpp
 * @brief Implementation of BBE Sound Processor
 * 
 * This file implements the core BBE audio enhancement algorithm including:
 * - Filter initialization and coefficient updates
 * - 3-band crossover processing
 * - Phase compensation
 * - Dynamic harmonic enhancement
 * - Bass boost
 * - Signal recombination
 * 
 * @author IthacaCore Audio Team
 * @version 1.0.0
 * @date 2025
 */

#include "bbe_processor.h"
#include <cstring>  // for memcpy if needed
#include <vector>   // for thread_local buffers

// ═════════════════════════════════════════════════════════════════════
// DspEffect Interface Implementation
// ═════════════════════════════════════════════════════════════════════

void BBEProcessor::prepare(int sampleRate, int maxBlockSize) {
    (void)maxBlockSize;  // Unused - BBE uses thread_local buffers
    sampleRate_ = sampleRate;
    
    // Initialize all filters for both channels (stereo)
    for (int ch = 0; ch < 2; ++ch) {
        // ─────────────────────────────────────────────────────────────
        // BASS BAND: Linkwitz-Riley 4th Order Lowpass
        // ─────────────────────────────────────────────────────────────
        // Two cascaded 2nd order Butterworth filters @ 150 Hz
        // Q = 0.707 (1/sqrt(2)) for Butterworth alignment
        // Result: -24 dB/octave rolloff, phase-coherent reconstruction
        
        crossover_[ch].lpBass1.setCoefficients(
            BiquadFilter::Type::LOWPASS,   // Filter type
            sampleRate,                     // Sample rate
            BASS_CUTOFF,                    // 150 Hz
            0.707                           // Q factor (Butterworth)
        );
        
        crossover_[ch].lpBass2.setCoefficients(
            BiquadFilter::Type::LOWPASS,
            sampleRate,
            BASS_CUTOFF,
            0.707
        );
        
        // ─────────────────────────────────────────────────────────────
        // MID BAND: Linkwitz-Riley 4th Order Bandpass
        // ─────────────────────────────────────────────────────────────
        // Implemented as: Highpass(150 Hz) × Lowpass(2400 Hz)
        // Each is LR4 (two cascaded Butterworth sections)
        // Result: Band-limited signal 150-2400 Hz
        
        // Highpass section (removes bass below 150 Hz)
        crossover_[ch].hpMid1.setCoefficients(
            BiquadFilter::Type::HIGHPASS,
            sampleRate,
            BASS_CUTOFF,                    // 150 Hz
            0.707
        );
        
        crossover_[ch].hpMid2.setCoefficients(
            BiquadFilter::Type::HIGHPASS,
            sampleRate,
            BASS_CUTOFF,
            0.707
        );
        
        // Lowpass section (removes treble above 2400 Hz)
        crossover_[ch].lpMid1.setCoefficients(
            BiquadFilter::Type::LOWPASS,
            sampleRate,
            TREBLE_CUTOFF,                  // 2400 Hz
            0.707
        );
        
        crossover_[ch].lpMid2.setCoefficients(
            BiquadFilter::Type::LOWPASS,
            sampleRate,
            TREBLE_CUTOFF,
            0.707
        );
        
        // ─────────────────────────────────────────────────────────────
        // TREBLE BAND: Linkwitz-Riley 4th Order Highpass
        // ─────────────────────────────────────────────────────────────
        // Two cascaded 2nd order Butterworth filters @ 2400 Hz
        // Result: Passes frequencies above 2400 Hz with -24 dB/octave
        
        crossover_[ch].hpTreble1.setCoefficients(
            BiquadFilter::Type::HIGHPASS,
            sampleRate,
            TREBLE_CUTOFF,                  // 2400 Hz
            0.707
        );
        
        crossover_[ch].hpTreble2.setCoefficients(
            BiquadFilter::Type::HIGHPASS,
            sampleRate,
            TREBLE_CUTOFF,
            0.707
        );
        
        // ─────────────────────────────────────────────────────────────
        // PHASE SHIFTERS: All-pass Filters
        // ─────────────────────────────────────────────────────────────
        // These filters pass all frequencies unchanged in amplitude
        // but rotate the phase to compensate for speaker distortion
        
        // Mid band: -180° phase shift at 1200 Hz (geometric mean of 150-2400)
        // This inverts the mid band relative to bass
        phaseShifters_[ch].midPhase.setCoefficients(
            BiquadFilter::Type::ALLPASS_180,
            sampleRate,
            MID_CENTER                       // 1200 Hz
        );
        
        // Treble band: -360° phase shift at 7200 Hz (geometric mean of 2400-20k)
        // This creates full cycle rotation relative to bass
        phaseShifters_[ch].treblePhase.setCoefficients(
            BiquadFilter::Type::ALLPASS_360,
            sampleRate,
            TREBLE_CENTER                    // 7200 Hz
        );
        
        // ─────────────────────────────────────────────────────────────
        // BASS BOOST: Low Shelf Filter
        // ─────────────────────────────────────────────────────────────
        // Initially set to 0 dB (no boost)
        // Will be updated when bassBoostLevel_ changes
        
        bassBoost_[ch].setCoefficients(
            BiquadFilter::Type::LOW_SHELF,
            sampleRate,
            BASS_CUTOFF,                     // 150 Hz transition
            0.707,                           // Q factor
            0.0                              // 0 dB gain initially
        );
        
        // ─────────────────────────────────────────────────────────────
        // HARMONIC ENHANCER: Dynamic Treble VCA
        // ─────────────────────────────────────────────────────────────
        // Prepares envelope follower and gain smoother
        
        enhancer_[ch].prepare(sampleRate);
    }
    
    // Force coefficient update on first processBlock() call
    lastDefinition_ = -1.0f;
    lastBassBoost_ = -1.0f;
}

// ═════════════════════════════════════════════════════════════════════
// AUDIO PROCESSING - MAIN ENTRY POINT (UNINTERLEAVED)
// ═════════════════════════════════════════════════════════════════════

void BBEProcessor::process(float* leftBuffer, float* rightBuffer, int numSamples) noexcept {
    // ─────────────────────────────────────────────────────────────────
    // STEP 1: ENABLE CHECK
    // ─────────────────────────────────────────────────────────────────
    // If disabled, return immediately (zero overhead)
    // Audio passes through unmodified (true zero-latency bypass)

    if (!enabled_.load(std::memory_order_relaxed)) {
        return;  // Early exit - no processing
    }
    
    // ─────────────────────────────────────────────────────────────────
    // STEP 2: UPDATE COEFFICIENTS IF PARAMETERS CHANGED
    // ─────────────────────────────────────────────────────────────────
    // Check if definition or bass boost changed since last call
    // Recalculate filter coefficients only when needed
    
    updateCoefficients();
    
    // ─────────────────────────────────────────────────────────────────
    // STEP 3: PROCESS EACH CHANNEL INDEPENDENTLY
    // ─────────────────────────────────────────────────────────────────
    // Left and right channels are processed identically
    // but use separate filter banks to maintain stereo image

    processChannel(leftBuffer, numSamples, 0);   // Channel 0 = left
    processChannel(rightBuffer, numSamples, 1);  // Channel 1 = right
}

// ═════════════════════════════════════════════════════════════════════
// AUDIO PROCESSING - PER CHANNEL
// ═════════════════════════════════════════════════════════════════════

void BBEProcessor::processChannel(float* buffer, int samples, int channelIndex) noexcept {
    // ─────────────────────────────────────────────────────────────────
    // THREAD_LOCAL BAND BUFFERS
    // ─────────────────────────────────────────────────────────────────
    // Pre-allocated per-thread buffers for band splitting
    // Allocated once per thread, reused across all processBlock() calls
    // 
    // Memory: 3 × 16384 samples × 4 bytes = 192 KB per audio thread
    // Performance: No RT allocations, cache-friendly access
    
    static thread_local std::vector<float> bassBand(16384);
    static thread_local std::vector<float> midBand(16384);
    static thread_local std::vector<float> trebleBand(16384);
    
    // Safety check: Ensure buffers are large enough
    // In production, samplesPerBlock should be ≤ 16384
    if (bassBand.size() < static_cast<size_t>(samples)) {
        return;  // Skip processing if buffer too small (shouldn't happen)
    }
    
    // Get references to filter banks for this channel
    CrossoverFilters& xover = crossover_[channelIndex];
    PhaseShifters& phase = phaseShifters_[channelIndex];
    
    // ─────────────────────────────────────────────────────────────────
    // PHASE 1: BAND SPLITTING (3-way Crossover)
    // ─────────────────────────────────────────────────────────────────
    // Split input into three frequency bands using Linkwitz-Riley filters
    // 
    // Why Linkwitz-Riley?
    // - Phase-coherent: When bands are summed, magnitude is flat
    // - No peaks or dips at crossover points
    // - Industry standard for speaker crossovers
    
    for (int i = 0; i < samples; ++i) {
        const float input = buffer[i];
        
        // BASS BAND: 20-150 Hz
        // Two cascaded lowpass filters (LR4 = -24 dB/octave)
        float bass = xover.lpBass1.processSample(input);
        bass = xover.lpBass2.processSample(bass);
        bassBand[i] = bass;
        
        // MID BAND: 150-2400 Hz
        // Highpass (150 Hz) then lowpass (2400 Hz) cascade
        // Each is LR4, creating bandpass with steep skirts
        float mid = xover.hpMid1.processSample(input);
        mid = xover.hpMid2.processSample(mid);
        mid = xover.lpMid1.processSample(mid);
        mid = xover.lpMid2.processSample(mid);
        midBand[i] = mid;
        
        // TREBLE BAND: 2400-20000 Hz
        // Two cascaded highpass filters (LR4 = -24 dB/octave)
        float treble = xover.hpTreble1.processSample(input);
        treble = xover.hpTreble2.processSample(treble);
        trebleBand[i] = treble;
    }
    
    // ─────────────────────────────────────────────────────────────────
    // PHASE 2: PHASE COMPENSATION
    // ─────────────────────────────────────────────────────────────────
    // Apply all-pass filters to realign phase relationships
    // 
    // Why phase compensation?
    // - Speakers and crossovers naturally introduce phase distortion
    // - High frequencies arrive delayed relative to bass
    // - Phase shift realigns the timing to restore original transients
    // 
    // Bass: No shift (0°) - used as phase reference
    // Mid: -180° shift - inverts signal at center frequency
    // Treble: -360° shift - full cycle rotation at center frequency
    
    // Mid band: Apply -180° phase shift at 1200 Hz
    for (int i = 0; i < samples; ++i) {
        midBand[i] = phase.midPhase.processSample(midBand[i]);
    }
    
    // Treble band: Apply -360° phase shift at 7200 Hz
    for (int i = 0; i < samples; ++i) {
        trebleBand[i] = phase.treblePhase.processSample(trebleBand[i]);
    }
    
    // ─────────────────────────────────────────────────────────────────
    // PHASE 3: HARMONIC ENHANCEMENT
    // ─────────────────────────────────────────────────────────────────
    // Dynamically enhance treble band using envelope-following VCA
    // 
    // Why dynamic enhancement?
    // - Static boost makes everything too bright
    // - Dynamic boost adds "air" to dull signals
    // - Reduces on bright signals to prevent harshness
    // - Maintains natural tonal balance
    
    enhancer_[channelIndex].processBlock(trebleBand.data(), samples);
    
    // ─────────────────────────────────────────────────────────────────
    // PHASE 4: BASS BOOST (if enabled)
    // ─────────────────────────────────────────────────────────────────
    // Apply low-shelf EQ to maintain balance with enhanced treble
    // 
    // Why bass boost?
    // - Enhanced treble can make bass sound thin
    // - Shelf filter adds warmth without muddiness
    // - Only applied if bassBoostLevel > 0
    
    const float bassBoost = bassBoostLevel_.load(std::memory_order_relaxed);
    if (bassBoost > 0.01f) {  // Skip if negligible (saves CPU)
        for (int i = 0; i < samples; ++i) {
            bassBand[i] = bassBoost_[channelIndex].processSample(bassBand[i]);
        }
    }
    
    // ─────────────────────────────────────────────────────────────────
    // PHASE 5: RECOMBINE BANDS
    // ─────────────────────────────────────────────────────────────────
    // Sum all three bands back together
    // 
    // Due to Linkwitz-Riley crossover design:
    // - Magnitude response is flat (no peaks/dips)
    // - Phase is now corrected (better transients)
    // - Treble is enhanced (more clarity)
    // - Bass is optionally boosted (more warmth)
    
    for (int i = 0; i < samples; ++i) {
        buffer[i] = bassBand[i] + midBand[i] + trebleBand[i];
    }
    
    // Processing complete - buffer now contains enhanced audio
}

// ═════════════════════════════════════════════════════════════════════
// COEFFICIENT UPDATE (PARAMETER CHANGES)
// ═════════════════════════════════════════════════════════════════════

void BBEProcessor::updateCoefficients() noexcept {
    // Read current parameter values (atomic, thread-safe)
    const float currentDefinition = definitionLevel_.load(std::memory_order_relaxed);
    const float currentBassBoost = bassBoostLevel_.load(std::memory_order_relaxed);
    
    // ─────────────────────────────────────────────────────────────────
    // UPDATE DEFINITION LEVEL
    // ─────────────────────────────────────────────────────────────────
    // Only update if changed (avoids unnecessary work)
    
    if (currentDefinition != lastDefinition_) {
        // Update both channel enhancers
        for (int ch = 0; ch < 2; ++ch) {
            enhancer_[ch].setDefinitionLevel(currentDefinition);
        }
        lastDefinition_ = currentDefinition;
    }
    
    // ─────────────────────────────────────────────────────────────────
    // UPDATE BASS BOOST LEVEL
    // ─────────────────────────────────────────────────────────────────
    // Recalculate filter coefficients if gain changed
    
    if (currentBassBoost != lastBassBoost_) {
        // Convert 0.0-1.0 range to 0-12 dB gain
        // 0.0 → 0 dB (no boost)
        // 0.5 → 6 dB
        // 1.0 → 12 dB
        const float gainDB = currentBassBoost * 12.0f;
        
        // Update both channel bass boost filters
        for (int ch = 0; ch < 2; ++ch) {
            bassBoost_[ch].setCoefficients(
                BiquadFilter::Type::LOW_SHELF,
                sampleRate_,
                BASS_CUTOFF,                 // 150 Hz
                0.707,                       // Q factor
                gainDB                       // Gain in dB
            );
        }
        lastBassBoost_ = currentBassBoost;
    }
}

// ═════════════════════════════════════════════════════════════════════
// RESET (Clear all filter states)
// ═════════════════════════════════════════════════════════════════════

void BBEProcessor::reset() noexcept {
    // Reset both channel filter banks
    for (int ch = 0; ch < 2; ++ch) {
        // Crossover filters (8 per channel)
        crossover_[ch].lpBass1.reset();
        crossover_[ch].lpBass2.reset();
        crossover_[ch].hpMid1.reset();
        crossover_[ch].hpMid2.reset();
        crossover_[ch].lpMid1.reset();
        crossover_[ch].lpMid2.reset();
        crossover_[ch].hpTreble1.reset();
        crossover_[ch].hpTreble2.reset();
        
        // Phase shifters (2 per channel)
        phaseShifters_[ch].midPhase.reset();
        phaseShifters_[ch].treblePhase.reset();
        
        // Bass boost (1 per channel)
        bassBoost_[ch].reset();
        
        // Harmonic enhancer (1 per channel)
        enhancer_[ch].reset();
    }
}