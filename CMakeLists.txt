# =============================================================================
# CMakeLists.txt - how Slice-able is built.
#
# CMake reads this file and generates a project for your platform's build tool
# (Xcode, Visual Studio, Ninja, Make). Typical use, from the repository folder:
#
#   cmake -S . -B build                      # configure (downloads JUCE the first time)
#   cmake --build build --config Release     # build
#   ctest --test-dir build -C Release        # run the unit tests
#
# Built plug-ins end up in build/SliceAble_artefacts/<Config>/<Format>/.
#
# Two targets are defined:
#   SliceAble       - the plug-in itself (VST3, AU on macOS, and a standalone app)
#   SliceAbleTests  - a small command-line program that unit-tests the core logic
#
# Common changes:
#   - Add a source file ....... list it in target_sources(SliceAble ...) below
#                               (and in SLICEABLE_CORE_SOURCES if the tests need it).
#   - Update JUCE ............. change GIT_TAG in the FetchContent block.
#   - Rename the plug-in ...... PRODUCT_NAME (but see the warning about codes).
#   - Add a plug-in format .... FORMATS (e.g. AAX needs the Avid SDK; see JUCE docs).
# =============================================================================

cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# Platform defaults (must be set before project())
# ---------------------------------------------------------------------------

# macOS: build a Universal Binary (Apple Silicon + Intel) unless told otherwise.
# To build for one architecture only (faster), configure with
#   -DCMAKE_OSX_ARCHITECTURES=arm64
if(CMAKE_HOST_APPLE)
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    endif()
    # Oldest macOS version the plug-in will run on.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# Windows: link the C runtime statically so users don't need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The VERSION here becomes the plug-in's version number shown in hosts.
project(SliceAble VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON) # tidy target folders in Xcode / Visual Studio

# Build options. Change on the command line, e.g. -DSLICEABLE_COPY_AFTER_BUILD=ON
option(SLICEABLE_BUILD_TESTS "Build the unit test executable" ON)
option(SLICEABLE_COPY_AFTER_BUILD "Copy the built plugins into the system plug-in folders" OFF)

# ---------------------------------------------------------------------------
# JUCE
#   Fetched automatically. To use a local checkout instead, configure with
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
#   GIT_TAG pins an exact JUCE release so every build uses the same code.
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

# juce_add_plugin creates one target per format plus a shared core.
#
# !! WARNING !!
# PLUGIN_MANUFACTURER_CODE and PLUGIN_CODE are how hosts identify the plug-in
# in saved projects (the AU validator uses them too: "auval -v aumu Slcb Dlio").
# Changing either makes hosts treat it as a different plug-in, so existing
# projects lose it. The same goes for BUNDLE_ID on macOS.
juce_add_plugin(SliceAble
    PRODUCT_NAME                "Slice-able"                # file name and name shown in hosts
    COMPANY_NAME                "Dolio"                     # hosts group plug-ins by this
    COMPANY_WEBSITE             "https://github.com/matt-dolio/dolio-slice-able"
    BUNDLE_ID                   "com.dolio.sliceable"
    PLUGIN_MANUFACTURER_CODE    Dlio                        # 4 chars, at least one upper-case
    PLUGIN_CODE                 Slcb                        # 4 chars, exactly one upper-case
    FORMATS                     ${SLICEABLE_FORMATS}
    IS_SYNTH                    TRUE                        # an instrument, not an effect
    NEEDS_MIDI_INPUT            TRUE                        # plays from MIDI notes
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE                       # leave computer-keyboard shortcuts to the host
    VST3_CATEGORIES             Instrument Sampler          # where VST3 hosts list it
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice  # "aumu": AU instrument
    MICROPHONE_PERMISSION_ENABLED FALSE                     # no audio input, so no mic prompt (standalone)
    COPY_PLUGIN_AFTER_BUILD     ${SLICEABLE_COPY_AFTER_BUILD}
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE)

# The core logic (no UI). Listed separately because the test program builds it too.
set(SLICEABLE_CORE_SOURCES
    source/SliceGrid.cpp
    source/SampleData.cpp
    source/SliceEngine.cpp
    source/SliceExporter.cpp)

# Every .cpp file in the plug-in. Headers don't need listing.
target_sources(SliceAble
    PRIVATE
        ${SLICEABLE_CORE_SOURCES}
        source/Parameters.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/WaveformView.cpp
        source/SliceLookAndFeel.cpp)

target_include_directories(SliceAble PRIVATE source)

# JUCE feature switches, shared by the plug-in and the tests:
#   JUCE_WEB_BROWSER / JUCE_USE_CURL = 0 ... features we don't use (fewer dependencies)
#   JUCE_USE_MP3AUDIOFORMAT = 1 ............ allow loading MP3 samples
#   JUCE_DISPLAY_SPLASH_SCREEN = 0 ......... no JUCE splash (check your JUCE licence allows it)
#   JUCE_VST3_CAN_REPLACE_VST2 = 0 ......... there was never a VST2 version to replace
set(SLICEABLE_JUCE_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_definitions(SliceAble PUBLIC ${SLICEABLE_JUCE_DEFINITIONS})

# JUCE modules used by the plug-in, plus JUCE's recommended compiler settings
# (optimisation, link-time optimisation and warnings).
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
#   Builds tests/TestMain.cpp together with the core sources into a console
#   program, and registers it with CTest so `ctest` runs it.
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

    # Only the non-GUI JUCE modules are needed, which keeps the test build quick.
    target_link_libraries(SliceAbleTests
        PRIVATE
            juce::juce_audio_basics
            juce::juce_audio_formats
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME SliceAbleTests COMMAND SliceAbleTests)
endif()
