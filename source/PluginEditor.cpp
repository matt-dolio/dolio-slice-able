// =============================================================================
// PluginEditor.cpp - the plug-in window. See PluginEditor.h for the layout
// sketch and for where to make common UI changes.
// =============================================================================

#include "PluginEditor.h"

#include "SliceExporter.h"

namespace sliceable
{

namespace
{
    constexpr int timerHz = 30;          // UI refresh rate (playing-slice highlights, info line)
    constexpr int statusSeconds = 6;     // how long a status message stays visible
    constexpr int margin = 12;           // padding around the window edge and between panels
    constexpr int panelTitleHeight = 20; // space for "GRID" / "PLAYBACK" titles
}

//==============================================================================
// SliceKeyboard
//==============================================================================

SliceKeyboard::SliceKeyboard (juce::MidiKeyboardState& keyboardState, const SliceEngine& e)
    : MidiKeyboardComponent (keyboardState, horizontalKeyboard), engine (e)
{
    setOctaveForMiddleC (3); // label middle C as C3, matching noteName()
    setScrollButtonsVisible (true);

    // Colours of the keyboard itself (the rest of the UI uses SliceLookAndFeel).
    setColour (whiteNoteColourId, juce::Colour (0xffd9dce1));
    setColour (blackNoteColourId, juce::Colour (0xff23262d));
    setColour (keySeparatorLineColourId, juce::Colour (0xff8b929d));
    setColour (keyDownOverlayColourId, Colours::accent.withAlpha (0.85f));
    setColour (mouseOverKeyOverlayColourId, Colours::accent.withAlpha (0.3f));
    setColour (shadowColourId, juce::Colours::black.withAlpha (0.4f));
    setColour (upDownButtonBackgroundColourId, Colours::panel);
    setColour (upDownButtonArrowColourId, Colours::dimText);
}

void SliceKeyboard::setMappedRange (int firstNote, int numNotes)
{
    // Called 30 times a second; only repaint when something actually changed.
    if (firstNote != firstMapped || numNotes != numMapped)
    {
        firstMapped = firstNote;
        numMapped = numNotes;
        repaint();
    }
}

void SliceKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                   juce::Colour lineColour, juce::Colour textColour)
{
    // Tint mapped keys before JUCE draws the key's normal overlays on top.
    if (isMapped (note))
    {
        g.setColour (Colours::accent.withAlpha (0.28f));
        g.fillRect (area);
    }

    // Show a key as pressed while its slice is playing (even if triggered by the host's MIDI).
    if (isMapped (note) && engine.isSliceSounding (note - firstMapped))
        isDown = true;

    MidiKeyboardComponent::drawWhiteNote (note, g, area, isDown, isOver, lineColour, textColour);

    // Accent strip along the bottom of mapped keys.
    if (isMapped (note))
    {
        g.setColour (Colours::accent);
        g.fillRect (area.removeFromBottom (3.0f));
    }
}

void SliceKeyboard::drawBlackNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                   juce::Colour noteFillColour)
{
    if (isMapped (note) && engine.isSliceSounding (note - firstMapped))
        isDown = true;

    // Mapped black keys get a colour blended towards the accent.
    MidiKeyboardComponent::drawBlackNote (note, g, area, isDown, isOver,
                                          isMapped (note) ? noteFillColour.interpolatedWith (Colours::accent, 0.35f)
                                                          : noteFillColour);
}

juce::String SliceKeyboard::getWhiteNoteText (int midiNoteNumber)
{
    // Label C keys, plus the first mapped key so the root is easy to find.
    if (midiNoteNumber == firstMapped && numMapped > 0)
        return noteName (midiNoteNumber);

    return MidiKeyboardComponent::getWhiteNoteText (midiNoteNumber);
}

//==============================================================================
// SliceAbleEditor - construction
//==============================================================================

SliceAbleEditor::SliceAbleEditor (SliceAbleProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      waveform (p),
      keyboard (p.keyboardState, p.getEngine())
{
    // Apply our colours/drawing to this window and every child component.
    setLookAndFeel (&lookAndFeel);

    // --- Header ---
    titleLabel.setText ("SLICE-ABLE", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, Colours::accent);
    addAndMakeVisible (titleLabel);

    sampleLabel.setFont (juce::FontOptions (14.0f)); // file name, length, rate (set in refreshSample)
    sampleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (sampleLabel);

    // --- Info line under the waveform ---
    infoLabel.setFont (juce::FontOptions (12.5f)); // slice count, tempo (set in refreshLayout)
    infoLabel.setColour (juce::Label::textColourId, Colours::dimText);
    addAndMakeVisible (infoLabel);

    statusLabel.setFont (juce::FontOptions (12.5f)); // temporary messages (showStatus)
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);

    loadButton.onClick = [this] { openLoadDialog(); };
    addAndMakeVisible (loadButton);

    exportButton.onClick = [this] { exportAllSlices(); };
    exportButton.setTooltip ("Write every slice to a folder as WAV files");
    addAndMakeVisible (exportButton);

    // The waveform reports user actions back to the editor through these callbacks.
    waveform.onLoadRequest = [this] { openLoadDialog(); };
    waveform.onFileDropped = [this] (const juce::File& f) { loadFile (f); };
    waveform.onStatus = [this] (const juce::String& message, bool isError) { showStatus (message, isError); };
    addAndMakeVisible (waveform);

    // --- Controls: each one is bound to the parameter with the given ID ---
    setUpChoice (division, ParamIDs::division, "Division");
    setUpChoice (tempoMode, ParamIDs::tempoMode, "Tempo");
    setUpChoice (playMode, ParamIDs::playMode, "Mode");
    setUpChoice (voiceMode, ParamIDs::voiceMode, "Voices");

    setUpKnob (bpm, ParamIDs::manualBpm, "BPM");
    setUpKnob (bars, ParamIDs::fitBars, "Bars");
    setUpKnob (offset, ParamIDs::gridOffset, "Offset");
    setUpKnob (root, ParamIDs::rootNote, "Root");
    setUpKnob (attack, ParamIDs::attack, "Attack");
    setUpKnob (release, ParamIDs::release, "Release");
    setUpKnob (pitch, ParamIDs::pitch, "Pitch");
    setUpKnob (gain, ParamIDs::gain, "Gain");

    offset.slider.setTooltip ("Moves the grid. Tip: Alt/Option-drag on the waveform.");
    bars.slider.setTooltip ("Fit to Bars: the whole sample is this many bars long.");

    reverseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processor.getState(), ParamIDs::reverse, reverseButton);
    addAndMakeVisible (reverseButton);

    addAndMakeVisible (keyboard);

    // Be told when a sample is loaded / cleared (including by session restore).
    processor.addChangeListener (this);

    // Hosts let the user resize the window within these limits (width, height).
    setResizable (true, true);
    setResizeLimits (900, 560, 2000, 1400);
    setSize (980, 640); // also triggers the first resized()

    refreshSample();
    startTimerHz (timerHz);
}

SliceAbleEditor::~SliceAbleEditor()
{
    stopTimer();
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr); // detach before `lookAndFeel` is destroyed
}

void SliceAbleEditor::setUpKnob (Knob& knob, const juce::String& paramId, const juce::String& title)
{
    knob.label.setText (title, juce::dontSendNotification);
    knob.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    knob.label.setColour (juce::Label::textColourId, Colours::dimText);
    knob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (knob.label);

    // Add first, so the value box is (re)created with this editor's look and feel.
    addAndMakeVisible (knob.slider);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 16);

    // The attachment copies the parameter's range, value and text formatting onto
    // the slider, and keeps the two in sync from then on (both directions).
    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getState(), paramId, knob.slider);
}

void SliceAbleEditor::setUpChoice (Choice& choice, const juce::String& paramId, const juce::String& title)
{
    choice.label.setText (title, juce::dontSendNotification);
    choice.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    choice.label.setColour (juce::Label::textColourId, Colours::dimText);
    addAndMakeVisible (choice.label);

    // Fill the menu from the parameter's own list of choices. Item IDs start at 1
    // because JUCE uses 0 for "nothing selected"; the attachment handles the offset.
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.getState().getParameter (paramId)))
        choice.box.addItemList (param->choices, 1);

    addAndMakeVisible (choice.box);

    // Must be created AFTER the items are added, or the menu shows nothing selected.
    choice.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.getState(), paramId, choice.box);
}

//==============================================================================
// SliceAbleEditor - drawing and layout
//==============================================================================

void SliceAbleEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // A rounded panel with a small title, used for the GRID and PLAYBACK groups.
    const auto drawPanel = [&g] (juce::Rectangle<int> area, const juce::String& title)
    {
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (area.toFloat(), 6.0f);
        g.setColour (Colours::panelOutline);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 6.0f, 1.0f);

        g.setColour (Colours::dimText);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (title, area.reduced (10, 0).removeFromTop (panelTitleHeight), juce::Justification::centredLeft);
    };

    drawPanel (gridPanel, "GRID");
    drawPanel (playbackPanel, "PLAYBACK");
}

void SliceAbleEditor::resized()
{
    // Layout is done by repeatedly slicing strips off a rectangle:
    // removeFromTop(32) takes the top 32 px and shrinks `area` by that much.
    // Fixed-height rows are taken from the top and bottom; the waveform gets
    // whatever is left in the middle, so it grows when the window is resized.
    auto area = getLocalBounds().reduced (margin);

    // Header: title on the left, buttons on the right, sample info in between.
    auto header = area.removeFromTop (32);
    titleLabel.setBounds (header.removeFromLeft (140));
    exportButton.setBounds (header.removeFromRight (130));
    header.removeFromRight (8);
    loadButton.setBounds (header.removeFromRight (120));
    sampleLabel.setBounds (header.reduced (8, 0));
    area.removeFromTop (8);

    // Keyboard along the bottom. 52 white keys fill the width (about 7 octaves visible).
    keyboard.setBounds (area.removeFromBottom (64));
    keyboard.setKeyWidth (juce::jmax (12.0f, (float) keyboard.getWidth() / 52.0f));
    area.removeFromBottom (8);

    // Controls panel row above the keyboard.
    auto controls = area.removeFromBottom (146);
    area.removeFromBottom (6);

    // Info line: description on the left half, status messages on the right half.
    auto info = area.removeFromBottom (20);
    statusLabel.setBounds (info.removeFromRight (info.getWidth() / 2));
    infoLabel.setBounds (info);
    area.removeFromBottom (4);

    // The waveform takes all remaining space.
    waveform.setBounds (area);

    // Panels: grid gets 3 knobs, playback gets 5 knobs; both get a column of menus.
    // All 8 knobs share one width so they line up across both panels.
    // (If you add a knob, update knobUnits and the lists in the blocks below.)
    constexpr int choiceWidth = 140;
    const auto knobUnits = 3 + 5;
    const auto knobWidth = (controls.getWidth() - margin - 2 * (choiceWidth + 24)) / knobUnits;

    gridPanel = controls.removeFromLeft (choiceWidth + 24 + 3 * knobWidth);
    controls.removeFromLeft (margin);
    playbackPanel = controls;

    // Places a menu (title + box) at the top of `column` and moves `column` down.
    const auto layoutChoice = [] (Choice& c, juce::Rectangle<int>& column)
    {
        c.label.setBounds (column.removeFromTop (16));
        c.box.setBounds (column.removeFromTop (24));
        column.removeFromTop (4);
    };

    // Places a knob (title + slider with its value box) in `cell`.
    const auto layoutKnob = [] (Knob& k, juce::Rectangle<int> cell)
    {
        k.label.setBounds (cell.removeFromTop (16));
        k.slider.setBounds (cell);
    };

    // GRID panel: [Division / Tempo menus] [BPM] [Bars] [Offset]
    {
        auto inner = gridPanel.reduced (10).withTrimmedTop (panelTitleHeight - 6);
        auto column = inner.removeFromLeft (choiceWidth);
        layoutChoice (division, column);
        layoutChoice (tempoMode, column);
        inner.removeFromLeft (10);

        for (auto* k : { &bpm, &bars, &offset })
            layoutKnob (*k, inner.removeFromLeft (knobWidth));
    }

    // PLAYBACK panel: [Mode / Voices menus + Reverse] [Root] [Attack] [Release] [Pitch] [Gain]
    {
        auto inner = playbackPanel.reduced (10).withTrimmedTop (panelTitleHeight - 6);
        auto column = inner.removeFromLeft (choiceWidth);
        layoutChoice (playMode, column);
        layoutChoice (voiceMode, column);
        reverseButton.setBounds (column.removeFromTop (22));
        inner.removeFromLeft (10);

        for (auto* k : { &root, &attack, &release, &pitch, &gain })
            layoutKnob (*k, inner.removeFromLeft (knobWidth));
    }
}

//==============================================================================
// SliceAbleEditor - keeping the display up to date
//==============================================================================

void SliceAbleEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // The processor loaded or cleared a sample.
    refreshSample();
}

void SliceAbleEditor::refreshSample()
{
    sample = processor.getSample();
    const auto missing = processor.getMissingSamplePath();

    waveform.setSample (sample, missing);

    // Header text: "Break.wav   4.00 s  |  44.1 kHz  |  Stereo", or why there's no sample.
    if (sample != nullptr)
    {
        sampleLabel.setText (sample->file.getFileName() + "   "
                                 + juce::String (sample->getLengthInSeconds(), 2) + " s  |  "
                                 + juce::String (sample->sampleRate / 1000.0, 1) + " kHz  |  "
                                 + (sample->audio.getNumChannels() > 1 ? "Stereo" : "Mono"),
                             juce::dontSendNotification);
        sampleLabel.setColour (juce::Label::textColourId, Colours::text);
    }
    else
    {
        sampleLabel.setText (missing.isNotEmpty() ? "Sample missing - load it again" : "No sample loaded",
                             juce::dontSendNotification);
        sampleLabel.setColour (juce::Label::textColourId, missing.isNotEmpty() ? Colours::error : Colours::dimText);
    }

    exportButton.setEnabled (sample != nullptr);
    lastRootNote = -1; // force the keyboard to scroll to the root note again
    refreshLayout();
}

void SliceAbleEditor::refreshLayout()
{
    // Called 30 times a second (and after a sample change). It recomputes the
    // slices from the current parameters and host tempo; this is cheap, and the
    // waveform / keyboard only repaint when something actually changed.
    processor.computeLayout (sample.get(), layout);
    const auto rootNote = processor.getRootNote();
    waveform.setLayout (layout, rootNote);

    // Slices above MIDI note 127 can't be played, so they're not "mapped".
    const auto mapped = juce::jmin (layout.numSlices, 128 - rootNote);
    keyboard.setMappedRange (rootNote, mapped);

    // When the root note changes, scroll the keyboard so the mapped keys are in
    // view (start one octave below the root's octave).
    if (rootNote != lastRootNote)
    {
        keyboard.setLowestVisibleKey (juce::jmax (0, rootNote - (rootNote % 12) - 12));
        lastRootNote = rootNote;
    }

    const auto gridInfo = processor.getGridInfo (sample.get());
    const auto mode = gridInfo.tempoMode;

    // Grey out knobs that currently have no effect.
    bpm.slider.setEnabled (mode == TempoMode::manual || (mode == TempoMode::host && ! gridInfo.hostTempoAvailable));
    bars.slider.setEnabled (mode == TempoMode::fitBars);

    if (sample == nullptr)
    {
        infoLabel.setText ("Load a sample to slice it on the grid", juce::dontSendNotification);
        return;
    }

    // Build the info line, e.g.
    //   "32 slices  |  1/16 @ 120.00 BPM (host)  |  4/4  |  C1 - G3"
    const auto& settings = gridInfo.settings;
    const auto divisionName = gridDivisions[(size_t) juce::jlimit (0, (int) gridDivisions.size() - 1, settings.divisionIndex)].name;

    juce::String tempoSource;
    switch (mode)
    {
        case TempoMode::host:    tempoSource = gridInfo.hostTempoAvailable ? "host" : "no host tempo, using BPM knob"; break;
        case TempoMode::manual:  tempoSource = "manual"; break;
        case TempoMode::fitBars: tempoSource = "fit to " + juce::String ((int) processor.getState().getRawParameterValue (ParamIDs::fitBars)->load()) + " bars"; break;
    }

    juce::String text;
    text << layout.numSlices << (layout.numSlices == 1 ? " slice" : " slices")
         << "  |  " << juce::String (divisionName.data(), divisionName.size())
         << " @ " << juce::String (settings.bpm, 2) << " BPM (" << tempoSource << ")"
         << "  |  " << settings.timeSignature.numerator << "/" << settings.timeSignature.denominator;

    if (mapped > 0)
        text << "  |  " << noteName (rootNote) << " - " << noteName (rootNote + mapped - 1);

    // Warn when some slices can't be played (more than 128, or past note 127).
    if (mapped < layout.numSlices || layout.numUnmapped > 0)
        text << "  |  grid too fine: " << (layout.numSlices - mapped + layout.numUnmapped) << " slices not mapped";

    infoLabel.setText (text, juce::dontSendNotification);
}

void SliceAbleEditor::timerCallback()
{
    // Things that can change without the editor being told directly: host tempo,
    // parameter automation, which slices are playing.
    refreshLayout();
    waveform.updatePlayState();
    keyboard.repaint(); // the "sounding" highlight comes from the audio thread

    // Count down and clear the status message.
    if (statusTicks > 0 && --statusTicks == 0)
        statusLabel.setText ({}, juce::dontSendNotification);
}

//==============================================================================
// SliceAbleEditor - loading and exporting
//==============================================================================

void SliceAbleEditor::openLoadDialog()
{
    // Start browsing next to the current sample, or in the user's Music folder.
    const auto startDir = sample != nullptr ? sample->file.getParentDirectory()
                                            : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Load a sample to slice", startDir,
                                                   processor.getFormatManager().getWildcardForAllFormats());

    // Async dialogs are required in plug-ins (a blocking dialog can freeze some
    // hosts). The callback runs later; SafePointer becomes null if the editor
    // was closed in the meantime, so we don't touch a deleted object.
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safeThis = juce::Component::SafePointer<SliceAbleEditor> (this)] (const juce::FileChooser& fc)
                          {
                              if (safeThis == nullptr)
                                  return;

                              const auto file = fc.getResult();

                              if (file != juce::File()) // empty = user cancelled
                                  safeThis->loadFile (file);
                          });
}

void SliceAbleEditor::loadFile (const juce::File& file)
{
    // On success the processor broadcasts a change and refreshSample() runs.
    const auto error = processor.loadSample (file);

    if (error.isNotEmpty())
        showStatus (error, true);
    else
        showStatus ("Loaded " + file.getFileName(), false);
}

void SliceAbleEditor::exportAllSlices()
{
    if (sample == nullptr)
        return;

    chooser = std::make_unique<juce::FileChooser> ("Choose a folder for the slices",
                                                   sample->file.getParentDirectory());

    // Same async pattern as openLoadDialog(), but picking a folder.
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safeThis = juce::Component::SafePointer<SliceAbleEditor> (this)] (const juce::FileChooser& fc)
                          {
                              if (safeThis == nullptr || safeThis->sample == nullptr)
                                  return;

                              const auto folder = fc.getResult();

                              if (folder == juce::File())
                                  return; // cancelled

                              // Slice with the CURRENT settings (they may have changed
                              // while the dialog was open), then write the files.
                              // This runs on the message thread; fine for typical loops.
                              auto& self = *safeThis;
                              SliceLayout current;
                              self.processor.computeLayout (self.sample.get(), current);

                              juce::String error;
                              const auto files = SliceExporter::writeAllSlices (*self.sample, current,
                                                                                self.processor.getRootNote(),
                                                                                folder, error);
                              if (error.isNotEmpty())
                                  self.showStatus (error, true);
                              else
                                  self.showStatus ("Exported " + juce::String (files.size()) + " slices to "
                                                       + folder.getFullPathName(), false);
                          });
}

void SliceAbleEditor::showStatus (const juce::String& message, bool isError)
{
    statusLabel.setText (message, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? Colours::error : Colours::success);
    statusTicks = statusSeconds * timerHz; // cleared by timerCallback after statusSeconds
}

} // namespace sliceable
