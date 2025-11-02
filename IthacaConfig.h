#pragma once

/**
 * @file IthacaConfig.h
 * @brief Minimal configuration for ithaca-core audio engine (framework-agnostic)
 *
 * This is the FALLBACK configuration for standalone ithaca-core usage.
 * When ithaca-core is used as a module in a larger project (e.g., with JUCE),
 * the parent project's IthacaConfig.h takes priority.
 *
 * This config contains ONLY core audio engine settings - no GUI, no JUCE, no plugin metadata.
 */

// Guard: Only load if parent project hasn't provided config
#ifndef ITHACA_CORE_VERSION_MAJOR

// ============================================================================
// VERSION
// ============================================================================

#define ITHACA_CORE_VERSION_MAJOR 1
#define ITHACA_CORE_VERSION_MINOR 1
#define ITHACA_CORE_VERSION_PATCH 0
#define ITHACA_CORE_VERSION_STRING "1.1.0"

// ============================================================================
// AUDIO ENGINE - Core Parameters
// ============================================================================

// Voice management
#define ITHACA_MAX_VOICES 128
#define ITHACA_MAX_VELOCITY_LAYERS 8
#define ITHACA_DEFAULT_VOICE_GAIN 1.0f

// Sample rates (Hz)
#define ITHACA_DEFAULT_SAMPLE_RATE 44100
#define ITHACA_MIN_SAMPLE_RATE 22050
#define ITHACA_MAX_SAMPLE_RATE 192000

// Buffer sizes (samples)
#define ITHACA_DEFAULT_BLOCK_SIZE 512
#define ITHACA_MIN_BLOCK_SIZE 32
#define ITHACA_MAX_BLOCK_SIZE 4096

// MIDI parameters
#define ITHACA_MIDI_NOTE_MIN 0
#define ITHACA_MIDI_NOTE_MAX 127
#define ITHACA_MIDI_VELOCITY_MIN 0
#define ITHACA_MIDI_VELOCITY_MAX 127
#define ITHACA_DEFAULT_VELOCITY 80

// ============================================================================
// ENVELOPE - ADSR Configuration
// ============================================================================

// Envelope state transition thresholds
#define ITHACA_ENVELOPE_TRIGGERS_END_ATTACK  0.99f
#define ITHACA_ENVELOPE_TRIGGERS_END_RELEASE 0.01f

// Default envelope parameters (milliseconds)
#define ITHACA_DEFAULT_ATTACK_MS 10.0f
#define ITHACA_DEFAULT_DECAY_MS 200.0f
#define ITHACA_DEFAULT_SUSTAIN_LEVEL 0.7f
#define ITHACA_DEFAULT_RELEASE_MS 500.0f

// ============================================================================
// LOGGING - Core Logger Configuration
// ============================================================================

// Log severity levels
#define ITHACA_LOG_LEVEL_DEBUG 0
#define ITHACA_LOG_LEVEL_INFO 1
#define ITHACA_LOG_LEVEL_WARN 2
#define ITHACA_LOG_LEVEL_ERROR 3

// Default logging configuration for standalone mode
#ifdef NDEBUG
    #define ITHACA_DEFAULT_LOG_LEVEL ITHACA_LOG_LEVEL_INFO
    #define ITHACA_ENABLE_RT_LOGGING 0
#else
    #define ITHACA_DEFAULT_LOG_LEVEL ITHACA_LOG_LEVEL_DEBUG
    #define ITHACA_ENABLE_RT_LOGGING 1
#endif

// Log file configuration
#define ITHACA_LOG_DIR "core_logger"
#define ITHACA_LOG_FILENAME "core_logger.log"

// ============================================================================
// PERFORMANCE - Memory & Threading
// ============================================================================

#define ITHACA_USE_SHARED_ENVELOPE_DATA 1
#define ITHACA_OPTIMIZE_FOR_LOW_LATENCY 1
#define ITHACA_ENABLE_RT_SAFETY_CHECKS 1
#define ITHACA_VOICE_POOL_PREALLOCATE 1
#define ITHACA_ENABLE_DENORMAL_PROTECTION 1

// ============================================================================
// DEBUG - Development & Testing
// ============================================================================

#ifdef _DEBUG
    #define ITHACA_DEBUG_MODE 1
    #define ITHACA_ENABLE_ASSERTIONS 1
#else
    #define ITHACA_DEBUG_MODE 0
    #define ITHACA_ENABLE_ASSERTIONS 0
#endif

#if ITHACA_ENABLE_ASSERTIONS
    #include <cassert>
    #define ITHACA_ASSERT(condition) assert(condition)
    #define ITHACA_ASSERT_MSG(condition, message) assert((condition) && (message))
#else
    #define ITHACA_ASSERT(condition) ((void)0)
    #define ITHACA_ASSERT_MSG(condition, message) ((void)0)
#endif

// ============================================================================
// END OF ITHACA-CORE CONFIGURATION
// ============================================================================

#endif // ITHACA_CORE_VERSION_MAJOR
