#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace sliceable
{

namespace ParamIDs
{
    // Grid
    inline constexpr const char* division   = "division";
    inline constexpr const char* tempoMode  = "tempoMode";
    inline constexpr const char* manualBpm  = "manualBpm";
    inline constexpr const char* fitBars    = "fitBars";
    inline constexpr const char* gridOffset = "gridOffset";

    // Playback
    inline constexpr const char* rootNote   = "rootNote";
    inline constexpr const char* playMode   = "playMode";
    inline constexpr const char* voiceMode  = "voiceMode";
    inline constexpr const char* reverse    = "reverse";
    inline constexpr const char* attack     = "attack";
    inline constexpr const char* release    = "release";
    inline constexpr const char* pitch      = "pitch";
    inline constexpr const char* gain       = "gain";
}

// Where the grid's tempo comes from.
enum class TempoMode
{
    host = 0,     // follow the DAW session tempo (Pro Tools / Logic behaviour)
    manual = 1,   // a fixed BPM typed in
    fitBars = 2,  // the whole sample is N bars long; tempo is derived from its length
};

inline const juce::StringArray tempoModeNames { "Host Tempo", "Manual BPM", "Fit to Bars" };
inline const juce::StringArray playModeNames  { "One Shot", "Gate" };
inline const juce::StringArray voiceModeNames { "Poly", "Mono (Choke)" };

inline constexpr int defaultRootNote = 36; // C1, as in Logic's Quick Sampler slice mode
inline constexpr float maxGridOffsetMs = 1000.0f;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

juce::String noteName (int midiNote);

} // namespace sliceable
