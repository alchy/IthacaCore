cmake_minimum_required(VERSION 3.10)
project(IthacaCore CXX)

# Nastavení C++ standardu
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Informace o projektu
set(PROJECT_VERSION "1.1.0")
set(PROJECT_DESCRIPTION "IthacaCore Audio Sampler - Professional Sample Engine with Hybrid Testing")

# Build konfigurace
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Výstupní adresáře
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Debug)

# Přidání externí knihovny libsndfile
add_subdirectory(libsndfile)

# Hlavní executable
add_executable(IthacaCore 
    # Entry point
    main.cpp
    
    # Core Logger module
    sampler/core_logger.cpp
    sampler/core_logger.h
    
    # SamplerIO module  
    sampler/sampler_io.cpp
    
    # Sampler coordinator module
    sampler/sampler.cpp
    sampler/sampler.h
    
    # InstrumentLoader module
    sampler/instrument_loader.cpp
    sampler/instrument_loader.h

    # Wav exporter
    sampler/wav_file_exporter.cpp
    sampler/wav_file_exporter.h

    # Voice module
    sampler/voice.cpp
    sampler/voice_processing.cpp
    sampler/voice.h
    
    # VoiceManager module
    sampler/voice_manager.cpp
    sampler/voice_manager.h

    # Envelopes (ADSR/ASR)
    sampler/envelopes/envelope.cpp
    sampler/envelopes/envelope.h
    sampler/envelopes/envelope_static_data.cpp
    sampler/envelopes/envelope_static_data.h

    # Pan
    sampler/pan.cpp
    sampler/pan.h
    sampler/lfopan.cpp
    sampler/lfopan.h

    # ===== DSP / BBE SOUND PROCESSOR =====
    sampler/dsp/biquad_filter.cpp
    sampler/dsp/biquad_filter.h
    sampler/dsp/harmonic_enhancer.cpp
    sampler/dsp/harmonic_enhancer.h
    sampler/dsp/bbe_processor.cpp
    sampler/dsp/bbe_processor.h

    # tests
    sampler/tests/test_helpers.cpp
    sampler/tests/test_helpers.h
    sampler/tests/tests.cpp
    sampler/tests/tests.h
)

# Include directories pro kompilaci a IntelliSense
target_include_directories(IthacaCore PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}          # Kořenový adresář projektu
    ${CMAKE_CURRENT_SOURCE_DIR}/sampler  # Sampler moduly
    ${CMAKE_CURRENT_SOURCE_DIR}/sampler/tests  # Nový test framework
)

# Linkování externích knihoven
target_link_libraries(IthacaCore PRIVATE 
    sndfile  # libsndfile pro WAV operace
)

# Compiler-specific nastavení
if(MSVC)
    # MSVC specifické flagy
    target_compile_definitions(IthacaCore PRIVATE 
        _CRT_SECURE_NO_WARNINGS  # Odstranění MSVC warnings pro bezpečné funkce
        WIN32_LEAN_AND_MEAN      # Rychlejší kompilace na Windows
        ENABLE_TESTS             # Povolení hybridního test systému
    )
    
    # Optimalizace pro Debug/Release
    target_compile_options(IthacaCore PRIVATE
        $<$<CONFIG:Debug>:/Od /Zi /RTC1>      # Debug: žádná optimalizace, debug info
        $<$<CONFIG:Release>:/O2 /DNDEBUG>     # Release: optimalizace, bez debug
    )
else()
    # GCC/Clang flagy
    target_compile_definitions(IthacaCore PRIVATE 
        ENABLE_TESTS             # Povolení hybridního test systému
    )
    target_compile_options(IthacaCore PRIVATE
        -Wall -Wextra -pedantic              # Všechna varování
        $<$<CONFIG:Debug>:-g -O0>            # Debug: debug info, žádná optimalizace  
        $<$<CONFIG:Release>:-O3 -DNDEBUG>    # Release: maximální optimalizace
    )
endif()

# Výstupní informace
message(STATUS "=== IthacaCore Hybrid Test Build Configuration ===")
message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Description: ${PROJECT_DESCRIPTION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "Testing: HYBRID (Legacy + New Framework)")
message(STATUS "Export capability: ENABLED")
message(STATUS "==================================================")

# Post-build informace
add_custom_command(TARGET IthacaCore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "=== IthacaCore Hybrid Build Completed ==="
    COMMAND ${CMAKE_COMMAND} -E echo "Executable: $<TARGET_FILE:IthacaCore>"
    COMMAND ${CMAKE_COMMAND} -E echo "Ready to run: ./$<TARGET_FILE_NAME:IthacaCore>"
    COMMAND ${CMAKE_COMMAND} -E echo "Log output: ./core_logger/core_logger.log"
    COMMAND ${CMAKE_COMMAND} -E echo "Test exports: ./exports/tests/"
    COMMAND ${CMAKE_COMMAND} -E echo "Testing: Legacy critical tests + New framework"
    COMMAND ${CMAKE_COMMAND} -E echo "==========================================="
)

# Instalace (volitelné)
if(WIN32)
    install(TARGETS IthacaCore
        RUNTIME DESTINATION bin
        CONFIGURATIONS Release
    )
    
    # Kopírování libsndfile DLL na Windows
    install(FILES $<TARGET_FILE:sndfile>
        DESTINATION bin
        CONFIGURATIONS Release
    )
endif()

# === ENHANCED DEVELOPMENT TARGETS ===

# Target pro vyčištění log souborů
add_custom_target(clean-logs
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/core_logger
    COMMENT "Cleaning log files and directories"
)

# Target pro vyčištění test exportů
add_custom_target(clean-exports
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/exports
    COMMENT "Cleaning test export files"
)

# Target pro kompletní vyčištění
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/core_logger
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/exports
    COMMENT "Cleaning all logs and exports"
)

# Target pro spuštění s čistými logy
add_custom_target(run-clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/core_logger
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_SOURCE_DIR}/exports
    COMMAND $<TARGET_FILE:IthacaCore>
    DEPENDS IthacaCore
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running IthacaCore with clean logs and exports"
)

# Target pro zobrazení logu
if(WIN32)
    add_custom_target(show-log
        COMMAND type ${CMAKE_CURRENT_SOURCE_DIR}/core_logger/core_logger.log
        COMMENT "Displaying current log file"
    )
    
    add_custom_target(show-test-results
        COMMAND echo "=== Test Export Files ===" && dir ${CMAKE_CURRENT_SOURCE_DIR}/exports/tests/
        COMMENT "Showing test export files"
    )
else()
    add_custom_target(show-log
        COMMAND cat ${CMAKE_CURRENT_SOURCE_DIR}/core_logger/core_logger.log
        COMMENT "Displaying current log file"
    )
    
    add_custom_target(show-test-results
        COMMAND echo "=== Test Export Files ===" && ls -la ${CMAKE_CURRENT_SOURCE_DIR}/exports/tests/
        COMMENT "Showing test export files"
    )
endif()

# Debug informace pro vývojáře
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "=== Hybrid Test Build Information ===")
    message(STATUS "Enable detailed logging: YES")
    message(STATUS "Runtime checks: ENABLED")
    message(STATUS "Optimization: DISABLED")
    message(STATUS "Testing framework: HYBRID")
    message(STATUS "Available targets:")
    message(STATUS "  - IthacaCore: Build main executable")
    message(STATUS "  - clean-logs: Remove all log files")
    message(STATUS "  - clean-exports: Remove test exports")
    message(STATUS "  - clean-all: Remove logs and exports")
    message(STATUS "  - run-clean: Run with fresh logs")
    message(STATUS "  - show-log: Display current log")
    message(STATUS "  - show-test-results: Display test exports")
    message(STATUS "=====================================")
endif()