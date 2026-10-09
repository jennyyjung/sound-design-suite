cmake_minimum_required (VERSION 3.22)

project (SoundSuite VERSION 0.1.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

if (APPLE)
    set (CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
    set (CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

option (SOUNDSUITE_BUILD_TESTS "Build unit and safety tests" ON)
option (SOUNDSUITE_BUILD_TOOLS "Build the offline render tool" ON)
option (SOUNDSUITE_COPY_AFTER_BUILD "Copy plugins into the system plugin folders" OFF)

include (FetchContent)

# Pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE to build against a local checkout.
FetchContent_Declare (juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.10
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable (juce)

# ---------------------------------------------------------------------------
# Shared DSP + macro code, used by the plugin, the tests and the render tool.
# ---------------------------------------------------------------------------
add_library (soundsuite_core INTERFACE)
target_include_directories (soundsuite_core INTERFACE Source)

# Tuning tables are compiled in as binary data; debug builds can also hot-reload
# them from the tuning/ folder (see ZoneTable.h). Every tuning/*.json is picked
# up, so a new macro needs no edit here.
file (GLOB SOUNDSUITE_TUNING_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tuning/*.json")
juce_add_binary_data (soundsuite_tuning
    NAMESPACE TuningData
    SOURCES ${SOUNDSUITE_TUNING_FILES})

# ---------------------------------------------------------------------------
# The plugin
# ---------------------------------------------------------------------------
set (SOUNDSUITE_FORMATS VST3 Standalone)
if (APPLE)
    list (APPEND SOUNDSUITE_FORMATS AU)
endif()

juce_add_plugin (SoundSuite
    COMPANY_NAME                "Silo"
    PRODUCT_NAME                "Sound Suite"
    PLUGIN_MANUFACTURER_CODE    Silo
    PLUGIN_CODE                 Ssu1
    FORMATS                     ${SOUNDSUITE_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${SOUNDSUITE_COPY_AFTER_BUILD})

target_sources (SoundSuite PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions (SoundSuite PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    SOUNDSUITE_TUNING_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tuning")

target_link_libraries (SoundSuite
    PRIVATE
        soundsuite_core
        soundsuite_tuning
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests and tools
# ---------------------------------------------------------------------------
if (SOUNDSUITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory (tests)
endif()

if (SOUNDSUITE_BUILD_TOOLS)
    add_subdirectory (tools/render)
endif()
