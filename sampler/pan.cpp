#include "pan.h"
#include <cmath>

// Definice M_PI pro konzistenci
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Static member initialization
float Panning::pan_left_gains[PAN_TABLE_SIZE] = {0.0f};
float Panning::pan_right_gains[PAN_TABLE_SIZE] = {0.0f};
bool Panning::pan_tables_initialized = false;

void Panning::initializePanTables() {
    if (pan_tables_initialized) return;
    
    // Constant power panning using sin/cos curves
    // MIDI pan: 0 = hard left, 64 = center, 127 = hard right
    constexpr float PI_2 = M_PI / 2.0f;
    
    for (int i = 0; i < PAN_TABLE_SIZE; ++i) {
        // Normalize MIDI value (0-127) to angle (0 to π/2)
        float normalized = static_cast<float>(i) / 127.0f;
        float angle = normalized * PI_2;
        
        // Constant power panning curves
        pan_left_gains[i] = std::cos(angle);   // 1.0 at left, 0.707 at center, 0.0 at right
        pan_right_gains[i] = std::sin(angle);  // 0.0 at left, 0.707 at center, 1.0 at right
    }
    
    pan_tables_initialized = true;
}

void Panning::getPanGains(float pan, float& leftGain, float& rightGain) noexcept {
    // Clamp pan to valid range
    pan = std::max(-1.0f, std::min(1.0f, pan));
    
    // Convert pan (-1.0 to +1.0) to MIDI index (0 to 127)
    int panIndex = static_cast<int>((pan + 1.0f) * 63.5f);
    panIndex = std::max(0, std::min(127, panIndex));
    
    // Lookup from pre-calculated table
    leftGain = pan_left_gains[panIndex];
    rightGain = pan_right_gains[panIndex];
}