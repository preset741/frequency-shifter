cmake_minimum_required(VERSION 3.22)

# Project setup
project(FrequencyShifter VERSION 0.1.0)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# macOS deployment target (for M1 compatibility)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build universal binary")
endif()

# Fetch JUCE
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(JUCE)

# Plugin sources
set(PLUGIN_SOURCES
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginEditor.cpp
    src/PluginEditor.h
    # DSP modules
    src/dsp/STFT.cpp
    src/dsp/STFT.h
    src/dsp/PhaseVocoder.cpp
    src/dsp/PhaseVocoder.h
    src/dsp/FrequencyShifter.cpp
    src/dsp/FrequencyShifter.h
    src/dsp/MusicalQuantizer.cpp
    src/dsp/MusicalQuantizer.h
    src/dsp/Scales.h
)

# Add the plugin target
juce_add_plugin(FrequencyShifter
    # Plugin info
    COMPANY_NAME "HarmonicTools"
    BUNDLE_ID "com.harmonictools.frequencyshifter"
    PLUGIN_MANUFACTURER_CODE Hrmt
    PLUGIN_CODE Fshf

    # Plugin formats to build
    FORMATS AU VST3 AUv3 Standalone

    # Plugin name
    PRODUCT_NAME "Frequency-shifter v67"

    # Plugin characteristics
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    # iOS/AUv3 settings
    PLUGIN_AU_EXPORT_PREFIX FreqShifterAU
    AU_MAIN_TYPE kAudioUnitType_Effect

    # Copy plugin to system folder after build (for testing)
    COPY_PLUGIN_AFTER_BUILD TRUE
)

# Generate JUCE header
juce_generate_juce_header(FrequencyShifter)

# Add source files
target_sources(FrequencyShifter PRIVATE ${PLUGIN_SOURCES})

# Compile definitions
target_compile_definitions(FrequencyShifter
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

# Link JUCE modules
target_link_libraries(FrequencyShifter
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Enable all warnings
if(MSVC)
    target_compile_options(FrequencyShifter PRIVATE /W4)
else()
    target_compile_options(FrequencyShifter PRIVATE -Wall -Wextra -Wpedantic)
endif()
