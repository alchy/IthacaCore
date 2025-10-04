/**
 * @file dsp_chain.cpp
 * @brief Implementace DSP chain containeru
 */

#include "dsp_chain.h"

DspChain::DspChain()
    : isPrepared_(false)
{
}

DspChain::~DspChain()
{
    // Unique pointers jsou automaticky uvolněny
}

// ============================================================================
// Lifecycle
// ============================================================================

void DspChain::prepare(int sampleRate, int maxBlockSize)
{
    // Připrav všechny efekty
    for (auto& effect : effects_) {
        if (effect) {
            effect->prepare(sampleRate, maxBlockSize);
        }
    }

    isPrepared_ = true;
}

void DspChain::reset() noexcept
{
    // Resetuj všechny efekty
    for (auto& effect : effects_) {
        if (effect) {
            effect->reset();
        }
    }
}

// ============================================================================
// Processing
// ============================================================================

void DspChain::process(float* leftBuffer, float* rightBuffer, int numSamples) noexcept
{
    if (!isPrepared_) {
        return;  // Safety: neprocesuj pokud není prepared
    }

    // Zpracuj všechny efekty sériově
    for (auto& effect : effects_) {
        if (effect && effect->isEnabled()) {
            effect->process(leftBuffer, rightBuffer, numSamples);
        }
    }
}

// ============================================================================
// Effect Management
// ============================================================================

void DspChain::addEffect(std::unique_ptr<DspEffect> effect)
{
    if (effect) {
        effects_.push_back(std::move(effect));
    }
}

DspEffect* DspChain::getEffect(size_t index) const
{
    if (index < effects_.size()) {
        return effects_[index].get();
    }
    return nullptr;
}

size_t DspChain::getEffectCount() const noexcept
{
    return effects_.size();
}
