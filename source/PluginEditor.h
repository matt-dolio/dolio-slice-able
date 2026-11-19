#pragma once

#include "PluginProcessor.h"
#include "SliceLookAndFeel.h"
#include "WaveformView.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace sliceable
{

// On-screen keyboard that highlights the keys mapped to slices.
class SliceKeyboard final : public juce::MidiKeyboardComponent
{
public:
    SliceKeyboard (juce::MidiKeyboardState&, const SliceEngine&);

    void setMappedRange (int firstNote, int numNotes);

private:
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;
    juce::String getWhiteNoteText (int midiNoteNumber) override;

    bool isMapped (int note) const noexcept { return note >= firstMapped && note < firstMapped + numMapped; }

    const SliceEngine& engine;
    int firstMapped = 36;
    int numMapped = 0;
};

class SliceAbleEditor final : public juce::AudioProcessorEditor,
                              private juce::ChangeListener,
                              private juce::Timer
{
public:
    explicit SliceAbleEditor (SliceAbleProcessor&);
    ~SliceAbleEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void setUpKnob (Knob&, const juce::String& paramId, const juce::String& title);
    void setUpChoice (Choice&, const juce::String& paramId, const juce::String& title);
    void refreshSample();
    void refreshLayout();
    void openLoadDialog();
    void loadFile (const juce::File&);
    void exportAllSlices();
    void showStatus (const juce::String& message, bool isError);

    SliceAbleProcessor& processor;
    SliceLookAndFeel lookAndFeel;

    juce::Label titleLabel, sampleLabel, infoLabel, statusLabel;
    juce::TextButton loadButton { "Load Sample..." };
    juce::TextButton exportButton { "Export Slices..." };

    WaveformView waveform;

    Choice division, tempoMode, playMode, voiceMode;
    Knob bpm, bars, offset, root, attack, release, pitch, gain;
    juce::ToggleButton reverseButton { "Reverse" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverseAttachment;

    SliceKeyboard keyboard;

    juce::Rectangle<int> gridPanel, playbackPanel;

    std::unique_ptr<juce::FileChooser> chooser;
    SampleData::Ptr sample;
    SliceLayout layout;
    int lastRootNote = -1;
    int statusTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceAbleEditor)
};

} // namespace sliceable
