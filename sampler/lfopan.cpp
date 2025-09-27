#include "lfopan.h"
#include <cmath>
#include <algorithm>

// Define M_PI for consistency
#ifndef M_PI
#define M_PI 3.14159f
#endif

// Static member initialization
float LfoPanning::frequency_table[MIDI_TABLE_SIZE] = {0.0f};
float LfoPanning::depth_table[MIDI_TABLE_SIZE] = {0.0f};
float LfoPanning::sine_table[SINE_TABLE_SIZE] = {0.0f};
bool LfoPanning::tables_initialized = false;

void LfoPanning::initializeLfoTables() {
    if (tables_initialized) return;
    
    // Initialize frequency table: linear interpolation from 0 Hz to 2 Hz
    // MIDI 0 = 0 Hz (no panning), MIDI 127 = 2 Hz (500ms cycle)
    for (int i = 0; i < MIDI_TABLE_SIZE; ++i) {
        frequency_table[i] = (static_cast<float>(i) / 127.0f) * 2.0f;
    }
    
    // Initialize depth table: linear interpolation from 0.0 to 1.0
    // MIDI 0 = 0.0 (no effect), MIDI 127 = 1.0 (full range -1.0 to +1.0)
    for (int i = 0; i < MIDI_TABLE_SIZE; ++i) {
        depth_table[i] = static_cast<float>(i) / 127.0f;
    }
    
    // Initialize sine lookup table for smooth LFO calculation
    // Higher resolution than needed for audio-rate modulation smoothness
    for (int i = 0; i < SINE_TABLE_SIZE; ++i) {
        float phase = (static_cast<float>(i) / static_cast<float>(SINE_TABLE_SIZE)) * TWO_PI;
        sine_table[i] = std::sin(phase);
    }
    
    tables_initialized = true;
}

float LfoPanning::getFrequencyFromMIDI(uint8_t midi_speed) noexcept {
    // Clamp MIDI value to valid range
    int index = std::max(0, std::min(127, static_cast<int>(midi_speed)));
    return frequency_table[index];
}

float LfoPanning::getDepthFromMIDI(uint8_t midi_depth) noexcept {
    // Clamp MIDI value to valid range
    int index = std::max(0, std::min(127, static_cast<int>(midi_depth)));
    return depth_table[index];
}

float LfoPanning::getSineValue(float phase) noexcept {
    // Normalize phase to 0.0-1.0 range
    float normalized_phase = phase / TWO_PI;
    normalized_phase = normalized_phase - std::floor(normalized_phase); // Wrap to 0.0-1.0
    
    // Calculate table index with interpolation
    float float_index = normalized_phase * static_cast<float>(SINE_TABLE_SIZE - 1);
    int index = static_cast<int>(float_index);
    float fraction = float_index - static_cast<float>(index);
    
    // Clamp index to valid range
    index = std::max(0, std::min(SINE_TABLE_SIZE - 1, index));
    int next_index = std::min(SINE_TABLE_SIZE - 1, index + 1);
    
    // Linear interpolation between adjacent table values
    return sine_table[index] + fraction * (sine_table[next_index] - sine_table[index]);
}

float LfoPanning::calculatePhaseIncrement(float frequency, int sampleRate) noexcept {
    if (sampleRate <= 0 || frequency < 0.0f) return 0.0f;
    
    // Phase increment per sample = (frequency * 2π) / sampleRate
    return (frequency * TWO_PI) / static_cast<float>(sampleRate);
}

float LfoPanning::wrapPhase(float phase) noexcept {
    // Keep phase in range 0.0 to 2π
    while (phase >= TWO_PI) {
        phase -= TWO_PI;
    }
    while (phase < 0.0f) {
        phase += TWO_PI;
    }
    return phase;
}