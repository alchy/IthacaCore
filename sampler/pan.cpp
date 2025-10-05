#include "pan.h"
#include <cmath>
#include <algorithm>

// Definice M_PI pro konzistenci
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Inicializace statických členů
float Panning::pan_left_gains[PAN_TABLE_SIZE] = {0.0f};
float Panning::pan_right_gains[PAN_TABLE_SIZE] = {0.0f};
bool  Panning::pan_tables_initialized = false;

void Panning::initializePanTables() {
    // Inicializace lookup tabulky pro konstantní panning
    // MIDI pan: 0 = zcela vlevo, 64 = střed, 127 = zcela vpravo
    if (pan_tables_initialized) return;
    
    constexpr float PI_2 = M_PI / 2.0f;
    
    for (int i = 0; i < PAN_TABLE_SIZE; ++i) {
        // Normalizace MIDI hodnoty (0-127) na úhel (0 až π/2)
        float normalized = static_cast<float>(i) / 127.0f;
        float angle = normalized * PI_2;
        
        // Konstantní panning křivky pro udržení vnímané hlasitosti
        pan_left_gains[i] = std::cos(angle);   // 1.0 vlevo, 0.707 ve středu, 0.0 vpravo
        pan_right_gains[i] = std::sin(angle);  // 0.0 vlevo, 0.707 ve středu, 1.0 vpravo
    }
    
    pan_tables_initialized = true;
}

void Panning::getPanGains(float pan, float& leftGain, float& rightGain) noexcept {
    // Ořezání hodnoty panningu do platného rozsahu
    pan = std::max(-1.0f, std::min(1.0f, pan));
    
    // Převod panningu (-1.0 až +1.0) na MIDI index (0 až 127)
    int panIndex = static_cast<int>((pan + 1.0f) * 63.5f);
    panIndex = std::max(0, std::min(127, panIndex));
    
    // Získání gainů z předpočítané tabulky
    leftGain = pan_left_gains[panIndex];
    rightGain = pan_right_gains[panIndex];
}