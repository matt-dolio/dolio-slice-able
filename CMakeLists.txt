cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# Platform defaults (must be set before project())
# ---------------------------------------------------------------------------

# macOS: build a Universal Binary (Apple Silicon + Intel) unless told otherwise.
if(CMAKE_HOST_APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# Windows: link the C runtime statically so users don't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(SliceAble VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(SLICEABLE_BUILD_TESTS "Build the unit test executable" ON)
option(SLICEABLE_COPY_AFTER_BUILD "Copy the built plugins into the system plug-in folders" OFF)

# ---------------------------------------------------------------------------
# JUCE
#   Fetched automatically. To use a local checkout instead, configure with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.6
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
set(SLICEABLE_FORMATS VST3 Standalone)
if(APPLE)
    # Logic Pro only loads Audio Units, so build an AU alongside the VST3.
    list(APPEND SLICEABLE_FORMATS AU)
endif()

juce_add_plugin(SliceAble
    PRODUCT_NAME                "Slice-able"
    COMPANY_NAME                "Dolio"
    COMPANY_WEBSITE             "https://github.com/matt-dolio/dolio-slice-able"
    BUNDLE_ID                   "com.dolio.sliceable"
    PLUGIN_MANUFACTURER_CODE    Dlio
    PLUGIN_CODE                 Slcb
    FORMATS                     ${SLICEABLE_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Sampler
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE
    COPY_PLUGIN_AFTER_BUILD     ${SLICEABLE_COPY_AFTER_BUILD}
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

set(SLICEABLE_CORE_SOURCES
    source/SliceGrid.cpp
    source/SampleData.cpp
    source/SliceEngine.cpp
    source/SliceExporter.cpp)

target_sources(SliceAble
    PRIVATE
        ${SLICEABLE_CORE_SOURCES}
        source/Parameters.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/WaveformView.cpp
        source/SliceLookAndFeel.cpp)

target_include_directories(SliceAble PRIVATE source)

set(SLICEABLE_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_definitions(SliceAble PUBLIC ${SLICEABLE_JUCE_DEFINITIONS})

target_link_libraries(SliceAble
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests (slicing maths, playback engine, WAV export) - no GUI, no plugin host
# ---------------------------------------------------------------------------
if(SLICEABLE_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(SliceAbleTests PRODUCT_NAME "SliceAbleTests")

    target_sources(SliceAbleTests
        PRIVATE
            ${SLICEABLE_CORE_SOURCES}
            tests/TestMain.cpp)

    target_include_directories(SliceAbleTests PRIVATE source)

    target_compile_definitions(SliceAbleTests PRIVATE
        ${SLICEABLE_JUCE_DEFINITIONS}
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=0)

    target_link_libraries(SliceAbleTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME SliceAbleTests COMMAND SliceAbleTests)
endif()
