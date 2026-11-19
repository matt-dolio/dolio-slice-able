#include "Parameters.h"
#include "SliceGrid.h"

namespace sliceable
{

juce::String noteName (int midiNote)
{
    return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 3);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    StringArray divisionNames;
    for (const auto& d : gridDivisions)
        divisionNames.add (String (d.name.data(), d.name.size()));

    const auto msText = [] (float v, int) { return v < 10.0f ? String (v, 1) + " ms" : String (roundToInt (v)) + " ms"; };

    // --- Grid ---
    auto grid = std::make_unique<AudioProcessorParameterGroup> ("grid", "Grid", " | ");

    grid->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::division, 1 }, "Division",
                                                            divisionNames, defaultDivisionIndex));

    grid->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::tempoMode, 1 }, "Tempo Source",
                                                            tempoModeNames, (int) TempoMode::host));

    grid->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::manualBpm, 1 }, "Manual BPM",
        NormalisableRange<float> ((float) minBpm, 300.0f, 0.01f, 0.6f), 120.0f,
        AudioParameterFloatAttributes().withLabel ("BPM")
                                       .withStringFromValueFunction ([] (float v, int) { return String (v, 2); })));

    grid->addChild (std::make_unique<AudioParameterInt> (
        ParameterID { ParamIDs::fitBars, 1 }, "Fit Bars", 1, 64, 1,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return String (v) + (v == 1 ? " bar" : " bars"); })));

    grid->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::gridOffset, 1 }, "Grid Offset",
        NormalisableRange<float> (0.0f, maxGridOffsetMs, 0.01f, 0.4f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));

    // --- Playback ---
    auto playback = std::make_unique<AudioProcessorParameterGroup> ("playback", "Playback", " | ");

    playback->addChild (std::make_unique<AudioParameterInt> (
        ParameterID { ParamIDs::rootNote, 1 }, "Root Note", 0, 127, defaultRootNote,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return noteName (v); })
                                     .withValueFromStringFunction ([] (const String& text)
                                     {
                                         for (int n = 0; n < 128; ++n)
                                             if (noteName (n).equalsIgnoreCase (text.trim()))
                                                 return n;
                                         return text.getIntValue();
                                     })));

    playback->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::playMode, 1 }, "Play Mode",
                                                                playModeNames, 0));

    playback->addChild (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::voiceMode, 1 }, "Voice Mode",
                                                                voiceModeNames, 0));

    playback->addChild (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::reverse, 1 }, "Reverse", false));

    playback->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::attack, 1 }, "Attack",
        NormalisableRange<float> (0.0f, 2000.0f, 0.01f, 0.3f), 0.5f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));

    playback->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::release, 1 }, "Release",
        NormalisableRange<float> (0.0f, 5000.0f, 0.01f, 0.3f), 15.0f,
        AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText)));

    playback->addChild (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::pitch, 1 }, "Pitch",
        NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("st")
                                       .withStringFromValueFunction ([] (float v, int)
                                       {
                                           return (v > 0.0f ? "+" : "") + String (v, 2) + " st";
                                       })));

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
