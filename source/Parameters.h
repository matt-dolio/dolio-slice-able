#pragma once

// =============================================================================
// Parameters.h - every user-facing setting (knob, menu, switch) of the plug-in.
//
// The settings are JUCE "parameters": the host (DAW) can see them, automate
// them, and save them with the project. They are created once in
// createParameterLayout() and stored in the processor's
// AudioProcessorValueTreeState ("APVTS"), which:
//   - gives the audio thread lock-free access to current values,
//   - keeps on-screen controls in sync through "attachments",
//   - saves / restores them as part of the plug-in state.
//
// HOW TO ADD A NEW PARAMETER (e.g. a "Velocity Sensitivity" knob):
//   1. Add an ID string below in ParamIDs.
//   2. Create it in createParameterLayout() (Parameters.cpp), in the right group.
//   3. Read it in the processor: grab a raw pointer in the SliceAbleProcessor
//      constructor and use it in processBlock() / readPlaybackParams().
//   4. Show it in the UI: add a Knob/Choice member to SliceAbleEditor, call
//      setUpKnob()/setUpChoice() in its constructor, and place it in resized().
//
// !! WARNING !!
// Never rename an existing ID string. Hosts store automation and saved settings
// against these IDs, so renaming one breaks every project that used it.
// =============================================================================

#include <juce_audio_processors/juce_audio_processors.h>

namespace sliceable
{

// Unique, permanent identifiers for each parameter (see the warning above).
namespace ParamIDs
{
    // Grid - where the slices go
    inline constexpr const char* division   = "division";   // grid spacing (index into gridDivisions)
    inline constexpr const char* tempoMode  = "tempoMode";  // where the tempo comes from (TempoMode)
    inline constexpr const char* manualBpm  = "manualBpm";  // tempo used in Manual mode (and as fallback)
    inline constexpr const char* fitBars    = "fitBars";    // sample length in bars, for Fit to Bars mode
    inline constexpr const char* gridOffset = "gridOffset"; // shifts the grid later, in milliseconds

    // Playback - how the slices sound
    inline constexpr const char* rootNote   = "rootNote";   // MIDI note that plays slice 1
    inline constexpr const char* playMode   = "playMode";   // One Shot / Gate (PlayMode)
    inline constexpr const char* voiceMode  = "voiceMode";  // Poly / Mono (VoiceMode)
    inline constexpr const char* reverse    = "reverse";    // play slices backwards
    inline constexpr const char* attack     = "attack";     // fade-in, ms
    inline constexpr const char* release    = "release";    // fade-out after note-off, ms
    inline constexpr const char* pitch      = "pitch";      // semitones
    inline constexpr const char* gain       = "gain";       // output level, dB
}

// Where the grid's tempo comes from. The numeric values are the indexes of the
// "Tempo Source" menu entries in tempoModeNames below - keep them in sync.
enum class TempoMode
{
    host = 0,     // follow the DAW session tempo (Pro Tools / Logic behaviour)
    manual = 1,   // a fixed BPM typed in
    fitBars = 2,  // the whole sample is N bars long; tempo is derived from its length
};

// Menu text for the choice parameters. As with gridDivisions, the order is
// saved in projects: only append new entries at the end.
// (Must match the TempoMode, PlayMode and VoiceMode enum values.)
inline const juce::StringArray tempoModeNames { "Host Tempo", "Manual BPM", "Fit to Bars" };
inline const juce::StringArray playModeNames  { "One Shot", "Gate" };
inline const juce::StringArray voiceModeNames { "Poly", "Mono (Choke)" };

inline constexpr int defaultRootNote = 36; // C1, as in Logic's Quick Sampler slice mode
inline constexpr float maxGridOffsetMs = 1000.0f; // upper end of the Grid Offset knob

/** Creates every parameter, grouped as "Grid" and "Playback" (hosts show these groups).
    Called once, from the processor's constructor.
*/
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Name of a MIDI note with middle C (note 60) as "C3", e.g. 36 -> "C1", 61 -> "C#3".
    This matches Logic, Ableton and most DAWs.
*/
juce::String noteName (int midiNote);

} // namespace sliceable
