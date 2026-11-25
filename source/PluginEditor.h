#pragma once

// =============================================================================
// PluginEditor.h - the plug-in window.
//
// Layout (top to bottom):
//
//   +--------------------------------------------------------------------+
//   | SLICE-ABLE        <sample name / info>     [Load...] [Export...]   |  header
//   +--------------------------------------------------------------------+
//   |                                                                    |
//   |                WaveformView (slices, click, drag)                  |
//   |                                                                    |
//   +--------------------------------------------------------------------+
//   | info line (slice count, tempo...)            status messages       |
//   +-------------------------------+------------------------------------+
//   | GRID                          | PLAYBACK                           |  controls
//   |  Division  (BPM)(Bars)(Offset)|  Mode     (Root)(Atk)(Rel)(Pit)(Gn)|
//   |  Tempo                        |  Voices   [x] Reverse              |
//   +-------------------------------+------------------------------------+
//   |                     SliceKeyboard                                  |
//   +--------------------------------------------------------------------+
//
// The editor only DISPLAYS and EDITS state; it owns no audio state of its own.
// Knobs and menus are connected to parameters through APVTS "attachments", so
// they stay in sync with host automation automatically. Everything here runs
// on the message thread. The host may close (delete) the editor at any time
// while audio keeps playing.
//
// Common changes:
//   - Add a control ......... add a Knob/Choice member, call setUpKnob/setUpChoice
//                             in the constructor, position it in resized().
//                             (The parameter itself goes in Parameters.cpp.)
//   - Window size ........... setResizeLimits / setSize in the constructor.
//   - Colours ............... SliceLookAndFeel.h.
//   - Info line text ........ refreshLayout().
// =============================================================================

#include "PluginProcessor.h"
#include "SliceLookAndFeel.h"
#include "WaveformView.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace sliceable
{

/** On-screen piano keyboard that tints the keys mapped to slices and lights up
    keys whose slice is currently playing. Clicking a key plays that slice.
*/
class SliceKeyboard final : public juce::MidiKeyboardComponent
{
public:
    SliceKeyboard (juce::MidiKeyboardState&, const SliceEngine&);

    /** Which keys have slices: `numNotes` keys starting at `firstNote` (the root note). */
    void setMappedRange (int firstNote, int numNotes);

private:
    // Overrides of JUCE's key drawing, to add the slice highlighting.
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour noteFillColour) override;
    juce::String getWhiteNoteText (int midiNoteNumber) override;

    bool isMapped (int note) const noexcept { return note >= firstMapped && note < firstMapped + numMapped; }

    const SliceEngine& engine; // asked "is this slice sounding?" when drawing
    int firstMapped = 36;
    int numMapped = 0;
};

/** The main plug-in window. Created by SliceAbleProcessor::createEditor(). */
class SliceAbleEditor final : public juce::AudioProcessorEditor,
                              private juce::ChangeListener, // hears "sample loaded" from the processor
                              private juce::Timer           // ~30 Hz UI refresh
{
public:
    explicit SliceAbleEditor (SliceAbleProcessor&);
    ~SliceAbleEditor() override;

    void paint (juce::Graphics&) override; // background and panel boxes
    void resized() override;               // positions every child component

private:
    /** A rotary knob with a title above it, connected to one parameter. */
    struct Knob
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment; // keeps slider <-> parameter in sync
    };

    /** A drop-down menu with a title above it, connected to one choice parameter. */
    struct Choice
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override; // sample changed
    void timerCallback() override;                                    // periodic refresh

    /** Creates the label + slider for a parameter and attaches them. */
    void setUpKnob (Knob&, const juce::String& paramId, const juce::String& title);
    /** Creates the label + menu for a choice parameter and attaches them. */
    void setUpChoice (Choice&, const juce::String& paramId, const juce::String& title);

    void refreshSample();   // the loaded sample changed: update waveform and header
    void refreshLayout();   // re-slice and update waveform, keyboard, info line
    void openLoadDialog();  // "Load Sample..." file browser
    void loadFile (const juce::File&);
    void exportAllSlices(); // "Export Slices..." folder browser + write
    void showStatus (const juce::String& message, bool isError); // temporary message, bottom right

    SliceAbleProcessor& processor;
    SliceLookAndFeel lookAndFeel; // must be declared before (so destroyed after) the components using it

    // --- Header and info line ---
    juce::Label titleLabel, sampleLabel, infoLabel, statusLabel;
    juce::TextButton loadButton { "Load Sample..." };
    juce::TextButton exportButton { "Export Slices..." };

    WaveformView waveform;

    // --- Controls (each connected to the parameter of the same name) ---
    Choice division, tempoMode, playMode, voiceMode;
    Knob bpm, bars, offset, root, attack, release, pitch, gain;
    juce::ToggleButton reverseButton { "Reverse" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> reverseAttachment;

    SliceKeyboard keyboard;

    // Areas of the GRID and PLAYBACK panels, worked out in resized() and drawn in paint().
    juce::Rectangle<int> gridPanel, playbackPanel;

    std::unique_ptr<juce::FileChooser> chooser; // kept alive while an async file dialog is open
    SampleData::Ptr sample;                     // the sample currently shown
    SliceLayout layout;                         // its slices, recomputed by refreshLayout()
    int lastRootNote = -1;                      // to scroll the keyboard only when the root changes
    int statusTicks = 0;                        // timer ticks until the status message is cleared

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceAbleEditor)
};

} // namespace sliceable
