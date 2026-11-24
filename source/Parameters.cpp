// =============================================================================
// Parameters.cpp - creates the plug-in's parameters. See Parameters.h for how
// parameters flow through the plug-in and how to add one.
//
// Reading a parameter definition:
//
//   AudioParameterFloat (ParameterID { id, 1 },   <- permanent ID + version hint
//                        "Attack",                <- name shown in the host
//                        NormalisableRange<float> (min, max, step, skew),
//                        default,
//                        attributes)              <- unit label, text formatting
//
//   - The "1" in ParameterID is the plug-in version that introduced the
//     parameter. Audio Units use it to keep automation stable; if you add a
//     parameter in a later release, give it that release's number (e.g. 2).
//   - `step` is the smallest change (0.01 = hundredths).
//   - `skew` shapes how the knob travels. 1.0 = even. Below 1.0 gives more of
//     the knob's travel to SMALL values (good for times in ms); above 1.0 gives
//     more to LARGE values (used for Gain, so 0 dB isn't squashed at the top).
//   - withStringFromValueFunction controls the text shown for a value, both in
//     our UI and in the host's automation lanes.
// =============================================================================

#include "Parameters.h"
#include "SliceGrid.h"

namespace sliceable
{

juce::String noteName (int midiNote)
{
    // Arguments: use sharps (not flats), include the octave number, middle C = octave 3.
    return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    // The Division menu lists the names from the gridDivisions table (SliceGrid.h),
    // so adding a row there automatically adds it to the menu.
    StringArray divisionNames;
    for (const auto& d : gridDivisions)
        divisionNames.add (String (d.name.data(), d.name.size()));

    // Shared formatter for millisecond values: one decimal below 10 ms ("0.5 ms"),
    // whole numbers above ("250 ms").
    const auto msText = [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; };

    // -------------------------------------------------------------------------
    // Grid group: everything that decides WHERE the sample is cut.
    // -------------------------------------------------------------------------
    auto grid = std::make_unique<AudioProcessorParameterGroup> ("grid", "Grid", " | ");

    // Grid spacing. Stored as an index into gridDivisions.
    grid->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::division, 1 }, "Division",
                                                            divisionNames, defaultDivisionIndex));

    // Host tempo / manual BPM / fit to bars (see TempoMode).
    grid->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::tempoMode, 1 }, "Tempo Source",
                                                            tempoModeNames, (int) TempoMode::host));

    // Manual tempo. Also used when "Host Tempo" is selected but the host doesn't
    // report one (e.g. the standalone app). The knob stops at 300 BPM even though
    // the grid maths accepts up to maxBpm, because higher values are rarely useful.
    grid->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::manualBpm, 1 }, "Manual BPM",
        NormalisableRange<float> ((float) minBpm, 300.0f, 0.01f, 0.6f), 120.0f,
        AudioParameterFloatAttributes().withLabel ("BPM")
                                       .withStringFromValueFunction ([] (float v, int) { return String (v, 2); })));

    // "This sample is N bars long" for the Fit to Bars tempo mode.
    grid->addChild (std::make_unique<AudioParameterInt> (
        ParameterID { ParamIDs::fitBars, 1 }, "Fit Bars", 1, 64, 1,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return String (v) + (v == 1 ? " bar" : " bars"); })));

    // Moves the grid later by this many ms, to line it up with the sample's downbeat.
    // Skewed towards small values, where fine adjustment matters most.
    grid->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::gridOffset, 1 }, "Grid Offset",
        NormalisableRange<float> (0.0f, maxGridOffsetMs, 0.01f, 0.4f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));

    // -------------------------------------------------------------------------
    // Playback group: everything that decides HOW slices sound.
    // -------------------------------------------------------------------------
    auto playback = std::make_unique<AudioProcessorParameterGroup> ("playback", "Playback", " | ");

    // The note that plays slice 1. Displayed as a note name ("C1"); typing either
    // a note name or a MIDI number into the value box both work.
    playback->addChild (std::make_unique<AudioParameterInt> (
        ParameterID { ParamIDs::rootNote, 1 }, "Root Note", 0, 127, defaultRootNote,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return noteName (v); })
                                     .withValueFromStringFunction ([] (const String& text)
                                     {
                                         // Try to match a note name such as "D#2" first...
                                         for (int n = 0; n < 128; ++n)
                                             if (noteName (n).equalsIgnoreCase (text.trim()))
                                                 return n;
                                         // ...otherwise treat the text as a MIDI note number.
                                         return text.getIntValue();
                                     })));

    // One Shot / Gate (see PlayMode in SliceEngine.h).
    playback->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::playMode, 1 }, "Play Mode",
                                                                playModeNames, 0));

    // Poly / Mono (see VoiceMode in SliceEngine.h).
    playback->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::voiceMode, 1 }, "Voice Mode",
                                                                voiceModeNames, 0));

    playback->addChild (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::reverse, 1 }, "Reverse", false));

    // A tiny default attack (0.5 ms) softens clicks at slice starts without dulling drum hits.
    playback->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::attack, 1 }, "Attack",
        NormalisableRange<float> (0.0f, 2000.0f, 0.01f, 0.3f), 0.5f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));

    // Only audible in Gate mode (One Shot ignores note-off).
    playback->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::release, 1 }, "Release",
        NormalisableRange<float> (0.0f, 5000.0f, 0.01f, 0.3f), 15.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));

    // +/- 2 octaves. Pitching also changes speed (like a tape or classic sampler).
    playback->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::pitch, 1 }, "Pitch",
        NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("st")
                                       .withStringFromValueFunction ([] (float v, int)
                                       {
                                           return (v > 0.0f ? "+" : "") + String (v, 2) + " st";
                                       })));

    // Output level in decibels. -48 dB is treated as silence (see processBlock).
    playback->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::gain, 1 }, "Gain",
        NormalisableRange<float> (-48.0f, 12.0f, 0.01f, 2.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")
                                       .withStringFromValueFunction ([] (float v, int)
                                       {
                                           return (v > 0.0f ? "+" : "") + String (v, 1) + " dB";
                                       })));

    layout.add (std::move (grid), std::move (playback));
    return layout;
}

} // namespace sliceable
