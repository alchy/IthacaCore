#pragma once

/**
 * @file IthacaConfig.h
 * @brief Configuration constants and defines for IthacaCore + JUCE integration
 * 
 * This file centralizes all configuration for the integrated sampler plugin.
 */

// ===== ITHACA CORE VERSION =====
#define ITHACA_CORE_VERSION_MAJOR 1
#define ITHACA_CORE_VERSION_MINOR 1
#define ITHACA_CORE_VERSION_PATCH 0
#define ITHACA_CORE_VERSION_STRING "1.1.0"

// ===== AUDIO ENGINE CONFIGURATION =====

// Voice management
#define ITHACA_MAX_VOICES 128
#define ITHACA_MAX_VELOCITY_LAYERS 8
#define ITHACA_DEFAULT_VOICE_GAIN 1.0f

// Sample rates (Hz)
#define ITHACA_DEFAULT_SAMPLE_RATE 44100
#define ITHACA_ALTERNATIVE_SAMPLE_RATE 48000
#define ITHACA_MIN_SAMPLE_RATE 22050
#define ITHACA_MAX_SAMPLE_RATE 192000

// Buffer sizes
#define ITHACA_DEFAULT_JUCE_BLOCK_SIZE 512
#define ITHACA_MAX_JUCE_BLOCK_SIZE 4096
#define ITHACA_MIN_JUCE_BLOCK_SIZE 32

// MIDI configuration
#define ITHACA_MIDI_NOTE_MIN 0
#define ITHACA_MIDI_NOTE_MAX 127
#define ITHACA_MIDI_VELOCITY_MIN 0
#define ITHACA_MIDI_VELOCITY_MAX 127
#define ITHACA_DEFAULT_VELOCITY 80

// ===== ENVELOPE CONFIGURATION =====

// ADSR thresholds
#define ITHACA_ENVELOPE_TRIGGER_END_ATTACK  0.99f
#define ITHACA_ENVELOPE_TRIGGER_END_RELEASE 0.01f

// Envelope timing (in milliseconds)
#define ITHACA_DEFAULT_ATTACK_MS 10.0f
#define ITHACA_DEFAULT_DECAY_MS 200.0f
#define ITHACA_DEFAULT_SUSTAIN_LEVEL 0.7f
#define ITHACA_DEFAULT_RELEASE_MS 500.0f

// ===== SAMPLE DIRECTORY PATHS =====

// Platform-specific default paths
#ifdef _WIN32
    #define ITHACA_DEFAULT_SAMPLE_DIR_VARIANT            R"(C:\Users\jindr\AppData\Roaming\IthacaPlayer\instrument)"
    #define ITHACA_DEFAULT_SAMPLE_DIR  R"(C:\Users\nemej992\AppData\Roaming\IthacaPlayer\instrument)"
    #define ITHACA_FALLBACK_SAMPLE_DIR          R"(C:\ProgramData\IthacaPlayer\samples)"
#elif __APPLE__
    #define ITHACA_DEFAULT_SAMPLE_DIR "~/Library/Application Support/IthacaPlayer/instrument"
    #define ITHACA_FALLBACK_SAMPLE_DIR "/Library/Application Support/IthacaPlayer/samples"
#else
    #define ITHACA_DEFAULT_SAMPLE_DIR "~/.local/share/IthacaPlayer/instrument"
    #define ITHACA_FALLBACK_SAMPLE_DIR "/usr/share/IthacaPlayer/samples"
#endif

// ===== JUCE PLUGIN INTEGRATION =====
#ifdef ITHACA_JUCE_INTEGRATION
    // JUCE-specific features enabled
    #define ITHACA_USE_JUCE_PARAMETERS 1
    #define ITHACA_USE_JUCE_GUI 1
    #define ITHACA_JUCE_TIMER_FPS 30
#endif

// ===== LOGGING CONFIGURATION =====

// Logging levels
#define ITHACA_LOG_LEVEL_DEBUG 0
#define ITHACA_LOG_LEVEL_INFO 1
#define ITHACA_LOG_LEVEL_WARN 2
#define ITHACA_LOG_LEVEL_ERROR 3

#ifdef ITHACA_JUCE_INTEGRATION
    // In JUCE plugin mode, reduce logging in release builds
    #ifdef NDEBUG
        #define ITHACA_DEFAULT_LOG_LEVEL ITHACA_LOG_LEVEL_WARN
        #define ITHACA_ENABLE_RT_LOGGING 0
    #else
        #define ITHACA_DEFAULT_LOG_LEVEL ITHACA_LOG_LEVEL_DEBUG
        #define ITHACA_ENABLE_RT_LOGGING 1
    #endif
#else
    // Standalone mode - always enable full logging
    #define ITHACA_DEFAULT_LOG_LEVEL ITHACA_LOG_LEVEL_DEBUG
    #define ITHACA_ENABLE_RT_LOGGING 1
#endif

// Log directory
#define ITHACA_LOG_DIR "core_logger"
#define ITHACA_LOG_FILENAME "core_logger.log"

// ===== PERFORMANCE CONFIGURATION =====

// Memory optimization
#define ITHACA_USE_SHARED_ENVELOPE_DATA 1
#define ITHACA_OPTIMIZE_FOR_LOW_LATENCY 1

// Threading
#define ITHACA_ENABLE_RT_SAFETY_CHECKS 1
#define ITHACA_VOICE_POOL_PREALLOCATE 1

// Audio processing optimization
#define ITHACA_USE_SIMD_OPTIMIZATION 0  // Can be enabled if needed
#define ITHACA_ENABLE_DENORMAL_PROTECTION 1

// ===== GUI CONFIGURATION =====

#ifdef ITHACA_USE_JUCE_GUI
    // Plugin window dimensions
    #define ITHACA_GUI_DEFAULT_WIDTH 800
    #define ITHACA_GUI_DEFAULT_HEIGHT 600
    #define ITHACA_GUI_MIN_WIDTH 600
    #define ITHACA_GUI_MIN_HEIGHT 400
    
    // GUI update rates
    #define ITHACA_GUI_STATS_UPDATE_MS 33  // ~30 FPS
    #define ITHACA_GUI_VOICE_ACTIVITY_UPDATE_MS 50  // 20 FPS
    
    // Color scheme
    #define ITHACA_GUI_BG_COLOR_TOP 0xff2a2a2a
    #define ITHACA_GUI_BG_COLOR_BOTTOM 0xff1a1a1a
    #define ITHACA_GUI_TEXT_COLOR 0xffffffff
    #define ITHACA_GUI_ACCENT_COLOR 0xff0088ff
#endif

// ===== PLUGIN IDENTIFICATION =====

#define ITHACA_PLUGIN_NAME "IthacaPlayer"
#define ITHACA_PLUGIN_MANUFACTURER "Lord Audio"
#define ITHACA_PLUGIN_DESCRIPTION "Professional Sample Engine with Hybrid Testing"
#define ITHACA_PLUGIN_CATEGORY "Instrument"

// Plugin codes (JUCE)
#define ITHACA_MANUFACTURER_CODE "Lau0"
#define ITHACA_PLUGIN_CODE "Itca"

// ===== FEATURE FLAGS =====

// Enable/disable specific features
#define ITHACA_ENABLE_TESTS 1
#define ITHACA_ENABLE_WAV_EXPORT 1
#define ITHACA_ENABLE_VELOCITY_LAYERS 1
#define ITHACA_ENABLE_PAN_CONTROL 1
#define ITHACA_ENABLE_MASTER_GAIN 1

// Compatibility flags
#define ITHACA_LEGACY_MODE_SUPPORT 0
#define ITHACA_STANDALONE_MODE_SUPPORT 1

// ===== VALIDATION MACROS =====

// Sample rate validation
#define ITHACA_IS_VALID_SAMPLE_RATE(sr) \
    ((sr) >= ITHACA_MIN_SAMPLE_RATE && (sr) <= ITHACA_MAX_SAMPLE_RATE)

// MIDI validation
#define ITHACA_IS_VALID_MIDI_NOTE(note) \
    ((note) >= ITHACA_MIDI_NOTE_MIN && (note) <= ITHACA_MIDI_NOTE_MAX)

#define ITHACA_IS_VALID_MIDI_VELOCITY(vel) \
    ((vel) >= ITHACA_MIDI_VELOCITY_MIN && (vel) <= ITHACA_MIDI_VELOCITY_MAX)

// Voice validation
#define ITHACA_IS_VALID_VOICE_INDEX(idx) \
    ((idx) >= 0 && (idx) < ITHACA_MAX_VOICES)

// Buffer size validation
#define ITHACA_IS_VALID_BLOCK_SIZE(size) \
    ((size) >= ITHACA_MIN_JUCE_BLOCK_SIZE && (size) <= ITHACA_MAX_JUCE_BLOCK_SIZE)

// ===== DEBUG CONFIGURATION =====

#ifdef _DEBUG
    #define ITHACA_DEBUG_MODE 1
    #define ITHACA_ENABLE_ASSERTIONS 1
    #define ITHACA_VERBOSE_LOGGING 1
#else
    #define ITHACA_DEBUG_MODE 0
    #define ITHACA_ENABLE_ASSERTIONS 0
    #define ITHACA_VERBOSE_LOGGING 0
#endif

// Debug macros
#if ITHACA_ENABLE_ASSERTIONS
    #include <cassert>
    #define ITHACA_ASSERT(condition) assert(condition)
    #define ITHACA_ASSERT_MSG(condition, message) assert((condition) && (message))
#else
    #define ITHACA_ASSERT(condition) ((void)0)
    #define ITHACA_ASSERT_MSG(condition, message) ((void)0)
#endif

// ===== COMPILE-TIME FEATURE DETECTION =====

// Check for C++17 features
#if __cplusplus >= 201703L
    #define ITHACA_HAS_CPP17 1
    #define ITHACA_CONSTEXPR_IF constexpr
#else
    #define ITHACA_HAS_CPP17 0
    #define ITHACA_CONSTEXPR_IF
#endif

// Platform detection
#ifdef _WIN32
    #define ITHACA_PLATFORM_WINDOWS 1
    #define ITHACA_PLATFORM_MACOS 0
    #define ITHACA_PLATFORM_LINUX 0
#elif __APPLE__
    #define ITHACA_PLATFORM_WINDOWS 0
    #define ITHACA_PLATFORM_MACOS 1
    #define ITHACA_PLATFORM_LINUX 0
#else
    #define ITHACA_PLATFORM_WINDOWS 0
    #define ITHACA_PLATFORM_MACOS 0
    #define ITHACA_PLATFORM_LINUX 1
#endif

// ===== OPTIMIZATION HINTS =====

// Force inline for critical functions
#ifdef _MSC_VER
    #define ITHACA_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    #define ITHACA_FORCE_INLINE __attribute__((always_inline)) inline
#else
    #define ITHACA_FORCE_INLINE inline
#endif

// Likely/unlikely branch prediction
#if defined(__GNUC__) || defined(__clang__)
    #define ITHACA_LIKELY(x) __builtin_expect(!!(x), 1)
    #define ITHACA_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define ITHACA_LIKELY(x) (x)
    #define ITHACA_UNLIKELY(x) (x)
#endif

// ===== BACKWARD COMPATIBILITY =====

// Legacy defines for existing IthacaCore code
#define DEFAULT_SAMPLE_DIR ITHACA_DEFAULT_SAMPLE_DIR
#define DEFAULT_SAMPLE_RATE ITHACA_DEFAULT_SAMPLE_RATE
#define DEFAULT_JUCE_BLOCK_SIZE ITHACA_DEFAULT_JUCE_BLOCK_SIZE
#define VOICE_GAIN ITHACA_DEFAULT_VOICE_GAIN
#define DEFAULT_VELOCITY ITHACA_DEFAULT_VELOCITY
#define ALTERNATIVE_SAMPLE_RATE ITHACA_ALTERNATIVE_SAMPLE_RATE
#define MAX_JUCE_BLOCK_SIZE ITHACA_MAX_JUCE_BLOCK_SIZE
#define ENVELOPE_TRIGGER_END_ATTACK ITHACA_ENVELOPE_TRIGGER_END_ATTACK
#define ENVELOPE_TRIGGER_END_RELEASE ITHACA_ENVELOPE_TRIGGER_END_RELEASE

// ===== END OF CONFIGURATION =====