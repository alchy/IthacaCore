#include "envelope.h"
#include <algorithm>

// Konstruktor - inicializace per-voice state s výchozími hodnotami
Envelope::Envelope()
    : attack_midi_index_(8)
    , release_midi_index_(16)
    , sustain_level_(1.0f) {
}

// RT-SAFE: Nastavení MIDI indexu pro attack
void Envelope::setAttackMIDI(uint8_t midi_value) noexcept {
    attack_midi_index_ = std::min(midi_value, static_cast<uint8_t>(127));
}

// RT-SAFE: Nastavení MIDI indexu pro release
void Envelope::setReleaseMIDI(uint8_t midi_value) noexcept {
    release_midi_index_ = std::min(midi_value, static_cast<uint8_t>(127));
}

// RT-SAFE: Nastavení sustain úrovně dle MIDI
void Envelope::setSustainLevelMIDI(uint8_t midi_value) noexcept {
    sustain_level_ = std::clamp(static_cast<float>(midi_value) / 127.0f, 0.0f, 1.0f);
}

// RT-SAFE: Získání sustain úrovně
float Envelope::getSustainLevel() const noexcept { 
    return sustain_level_; 
}

// RT-SAFE: Kopírování attack dat do bufferu (deleguje na static data)
bool Envelope::getAttackGains(float* gain_buffer, int num_samples, 
                             int envelope_attack_position, int sample_rate) const noexcept {
    return EnvelopeStaticData::getAttackGains(gain_buffer, num_samples, 
                                             envelope_attack_position, 
                                             attack_midi_index_, sample_rate);
}

// RT-SAFE: Kopírování release dat do bufferu (deleguje na static data)
bool Envelope::getReleaseGains(float* gain_buffer, int num_samples, 
                              int envelope_release_position, int sample_rate) const noexcept {
    return EnvelopeStaticData::getReleaseGains(gain_buffer, num_samples, 
                                              envelope_release_position, 
                                              release_midi_index_, sample_rate);
}

// RT-SAFE: Získání délky attack fáze v ms (deleguje na static data)
float Envelope::getAttackLength(int sample_rate) const noexcept {
    return EnvelopeStaticData::getAttackLength(attack_midi_index_, sample_rate);
}

// RT-SAFE: Získání délky release fáze v ms (deleguje na static data)
float Envelope::getReleaseLength(int sample_rate) const noexcept {
    return EnvelopeStaticData::getReleaseLength(release_midi_index_, sample_rate);
}