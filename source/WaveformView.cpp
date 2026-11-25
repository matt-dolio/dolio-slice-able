// =============================================================================
// WaveformView.cpp - drawing the sample and its slices, and mouse handling.
// See WaveformView.h for the list of features.
// =============================================================================

#include "WaveformView.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "SliceExporter.h"
#include "SliceLookAndFeel.h"

namespace sliceable
{

namespace
{
    constexpr float labelHeight = 20.0f;      // height of the slice-number row at the top
    constexpr int dragOutThreshold = 6;       // pixels the mouse must move before a click becomes a drag
    constexpr float auditionVelocity = 0.8f;  // velocity used when clicking a slice
}

WaveformView::WaveformView (SliceAbleProcessor& p)
    : processor (p),
      offsetParam (p.getState().getParameter (ParamIDs::gridOffset))
{
    // We decide ourselves when to repaint (hover changes), instead of on every mouse move.
    setRepaintsOnMouseActivity (false);
}

void WaveformView::setSample (SampleData::Ptr newSample, const juce::String& missingSamplePath)
{
    stopAudition(); // the held note may refer to the old sample's slices
    sample = std::move (newSample);
    missingPath = missingSamplePath;
    hoverSlice = -1;
    rebuildPeaks();
    repaint();
}

void WaveformView::setLayout (const SliceLayout& newLayout, int newRootNote)
{
    // Called 30 times a second by the editor; avoid repainting when nothing changed.
    const auto same = newRootNote == rootNote
                   && newLayout.numSlices == layout.numSlices
                   && newLayout.numUnmapped == layout.numUnmapped
                   && std::equal (newLayout.begin(), newLayout.end(), layout.begin());

    if (same)
        return;

    layout = newLayout;
    rootNote = newRootNote;
    repaint();
}

void WaveformView::updatePlayState()
{
    // Copy the engine's "sounding" flags (written by the audio thread) and note
    // whether anything changed since last time.
    const auto& engine = processor.getEngine();
    auto changed = false;

    for (int i = 0; i < maxSlices; ++i)
    {
        const auto s = engine.isSliceSounding (i);
        changed = changed || s != sounding[(size_t) i];
        sounding[(size_t) i] = s;
    }

    const auto position = engine.getLatestVoicePosition();

    // Repaint when highlights changed or the playhead moved by at least half a frame.
    if (changed || std::abs (position - playPosition) > 0.5)
    {
        playPosition = position;
        repaint();
    }
}

//==============================================================================
// Geometry helpers
//==============================================================================

juce::Rectangle<float> WaveformView::getLabelArea() const
{
    return getLocalBounds().toFloat().removeFromTop (labelHeight);
}

juce::Rectangle<float> WaveformView::getWaveArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (labelHeight);
}

float WaveformView::sampleToX (int64_t samplePos) const
{
    if (sample == nullptr || sample->getLengthInSamples() == 0)
        return 0.0f;

    // The whole sample is stretched across the full width (no zoom / scroll).
    return (float) ((double) samplePos / (double) sample->getLengthInSamples() * getWidth());
}

int64_t WaveformView::xToSample (float x) const
{
    if (sample == nullptr || getWidth() <= 0)
        return 0;

    const auto proportion = juce::jlimit (0.0, 1.0, (double) x / getWidth());
    return (int64_t) (proportion * (double) sample->getLengthInSamples());
}

int WaveformView::sliceAtX (float x) const
{
    return findSliceAt (layout, xToSample (x));
}

void WaveformView::rebuildPeaks()
{
    peaks.clear();

    if (sample == nullptr || getWidth() <= 0)
        return;

    const auto width = getWidth();
    const auto length = sample->getLengthInSamples();
    const auto channels = sample->audio.getNumChannels();
    peaks.resize ((size_t) width);

    // For each pixel column, find the min and max sample value in its stretch of
    // audio, across all channels. Drawing a vertical line from min to max in each
    // column gives the familiar waveform outline.
    for (int px = 0; px < width; ++px)
    {
        const auto start = (int) ((int64_t) px * length / width);
        const auto end = juce::jmax (start + 1, (int) ((int64_t) (px + 1) * length / width));
        Peak peak { 1.0f, -1.0f }; // start "inverted" so the first values always replace it

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto range = juce::FloatVectorOperations::findMinAndMax (sample->audio.getReadPointer (ch, start),
                                                                          juce::jmin (end, (int) length) - start);
            peak.min = juce::jmin (peak.min, range.getStart());
            peak.max = juce::jmax (peak.max, range.getEnd());
        }

        peaks[(size_t) px] = peak;
    }
}

void WaveformView::resized()
{
    rebuildPeaks(); // one peak per pixel, so a new width needs new peaks
}

//==============================================================================
// Drawing
//==============================================================================

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Colours::sliceBandA);
    g.fillRoundedRectangle (bounds, 6.0f);

    // --- Empty state: dashed outline and "drop a file" prompt -------------------
    if (sample == nullptr)
    {
        g.setColour (fileDragActive ? Colours::accent : Colours::panelOutline);
        juce::Path outline;
        outline.addRoundedRectangle (bounds.reduced (1.0f), 6.0f);
        const float dashes[] { 6.0f, 4.0f }; // 6 px dash, 4 px gap
        juce::PathStrokeType (1.5f).createDashedStroke (outline, outline, dashes, 2);
        g.fillPath (outline);

        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText ("Drop an audio file here, or click to load", bounds.withTrimmedBottom (24.0f),
                    juce::Justification::centred);

        // Second line: either "sample not found" (in red) or a hint about formats.
        g.setColour (missingPath.isNotEmpty() ? Colours::error : Colours::dimText);
        g.setFont (juce::FontOptions (13.0f));
        const auto hint = missingPath.isNotEmpty()
                            ? "Sample not found: " + missingPath
                            : juce::String ("WAV, AIFF, FLAC, MP3, Ogg - sliced on the grid, one slice per MIDI note");
        g.drawFittedText (hint, bounds.withTrimmedTop (30.0f).reduced (20.0f, 0.0f).toNearestInt(),
                          juce::Justification::centred, 2);
        return;
    }

    const auto wave = getWaveArea();
    const auto labels = getLabelArea();

    // Clip everything below to the rounded rectangle so corners stay rounded.
    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (bounds, 6.0f);
    g.reduceClipRegion (clip);

    // --- 1. Slice bands (drawn first, behind the waveform) ----------------------
    for (int i = 0; i < layout.numSlices; ++i)
    {
        const auto& slice = layout.slices[(size_t) i];
        const auto x0 = sampleToX (slice.start);
        const auto x1 = sampleToX (slice.end);
        const auto band = juce::Rectangle<float> (x0, 0.0f, x1 - x0, bounds.getHeight());

        // Alternate two shades so neighbouring slices are easy to tell apart.
        g.setColour ((i % 2) == 0 ? Colours::sliceBandA : Colours::sliceBandB);
        g.fillRect (band);

        if (sounding[(size_t) i])
        {
            g.setColour (Colours::accent.withAlpha (0.22f)); // playing: accent tint
            g.fillRect (band);
        }
        else if (i == hoverSlice)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f)); // under the mouse: faint lift
            g.fillRect (band);
        }
    }

    // --- 2. Darken the region past the last mapped slice ------------------------
    // (Only happens when the grid would need more than 128 slices.)
    if (layout.numSlices > 0 && layout.numUnmapped > 0)
    {
        const auto x = sampleToX (layout.slices[(size_t) layout.numSlices - 1].end);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (x, 0.0f, bounds.getWidth() - x, bounds.getHeight()));
    }

    // --- 3. Waveform ------------------------------------------------------------
    if (! peaks.empty())
    {
        const auto midY = wave.getCentreY();
        const auto halfHeight = wave.getHeight() * 0.45f; // full scale fills 90% of the height
        juce::RectangleList<float> columns;

        // One 1-pixel-wide rectangle per column, from the peak max down to the peak min.
        // (Screen y grows downwards, hence "midY - value".)
        for (size_t px = 0; px < peaks.size(); ++px)
        {
            const auto top = midY - juce::jlimit (-1.0f, 1.0f, peaks[px].max) * halfHeight;
            const auto bottom = midY - juce::jlimit (-1.0f, 1.0f, peaks[px].min) * halfHeight;
            columns.addWithoutMerging ({ (float) px, top, 1.0f, juce::jmax (1.0f, bottom - top) });
        }

        g.setColour (Colours::waveform.withAlpha (0.9f));
        g.fillRectList (columns); // one draw call for all columns: much faster than a loop of fillRect

        g.setColour (Colours::waveform.withAlpha (0.15f));
        g.drawHorizontalLine ((int) midY, 0.0f, bounds.getWidth()); // centre (zero) line
    }

    // --- 4. Grid lines (slice boundaries) and labels ----------------------------
    g.setFont (juce::FontOptions (11.0f));

    for (int i = 0; i < layout.numSlices; ++i)
    {
        const auto& slice = layout.slices[(size_t) i];
        const auto x0 = sampleToX (slice.start);
        const auto x1 = sampleToX (slice.end);

        // Line at the START of every slice except the first (which starts at x = 0).
        // A line next to a playing slice is drawn in the accent colour.
        if (i > 0)
        {
            g.setColour (sounding[(size_t) i] || sounding[(size_t) i - 1] ? Colours::accent : Colours::gridLine);
            g.drawVerticalLine (juce::roundToInt (x0), 0.0f, bounds.getHeight());
        }

        // Label: slice number, plus the note name when there's room.
        // Very narrow slices get no label at all to avoid overlapping text.
        const auto labelArea = juce::Rectangle<float> (x0, labels.getY(), x1 - x0, labels.getHeight()).reduced (3.0f, 0.0f);

        if (labelArea.getWidth() < 12.0f)
            continue;

        const auto note = rootNote + i;
        auto text = juce::String (i + 1); // shown 1-based to users

        if (labelArea.getWidth() > 44.0f && juce::isPositiveAndBelow (note, 128))
            text << "  " << noteName (note);

        g.setColour (sounding[(size_t) i] ? Colours::accent : Colours::dimText);
        g.drawText (text, labelArea, juce::Justification::centredLeft, false);
    }

    // Divider under the label row.
    g.setColour (Colours::panelOutline);
    g.drawHorizontalLine ((int) labels.getBottom(), 0.0f, bounds.getWidth());

    // --- 5. Playhead of the most recent voice -----------------------------------
    if (playPosition >= 0.0)
    {
        g.setColour (Colours::text.withAlpha (0.8f));
        g.drawVerticalLine (juce::roundToInt (sampleToX ((int64_t) playPosition)), wave.getY(), wave.getBottom());
    }

    // --- 6. Readout while Alt-dragging the grid ---------------------------------
    if (draggingOffset)
    {
        g.setColour (Colours::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("Grid offset: " + (offsetParam != nullptr ? offsetParam->getCurrentValueAsText() : juce::String()),
                    wave.reduced (8.0f), juce::Justification::topRight, false);
    }

    g.restoreState(); // remove the rounded clip

    // Highlight border while a file is being dragged over a loaded waveform.
    if (fileDragActive)
    {
        g.setColour (Colours::accent);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 6.0f, 2.0f);
    }
}

//==============================================================================
// Mouse handling
//==============================================================================

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    // Track the hovered slice for its highlight.
    const auto slice = sample != nullptr ? sliceAtX ((float) e.x) : -1;

    if (slice != hoverSlice)
    {
        hoverSlice = slice;
        repaint();
    }

    // Cursor hints: "click to load" when empty, "slide" when Alt is held.
    if (sample == nullptr)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else if (e.mods.isAltDown())
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseExit (const juce::MouseEvent&)
{
    if (hoverSlice != -1)
    {
        hoverSlice = -1;
        repaint();
    }
}

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    dragOutStarted = false;
    pressedSlice = -1;

    // Empty view: a click opens the file browser.
    if (sample == nullptr)
    {
        if (onLoadRequest)
            onLoadRequest();
        return;
    }

    // Alt/Option + click: start sliding the grid.
    if (e.mods.isAltDown() && offsetParam != nullptr)
    {
        draggingOffset = true;
        offsetAtDragStart = offsetParam->convertFrom0to1 (offsetParam->getValue()); // current value in ms
        offsetParam->beginChangeGesture(); // tells the host a user edit has started (for automation recording)
        repaint();
        return;
    }

    // Plain click: play the slice under the mouse for as long as the button is held.
    pressedSlice = sliceAtX ((float) e.x);
    startAudition (pressedSlice);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (sample == nullptr)
        return;

    if (draggingOffset)
    {
        // Convert the horizontal mouse movement into a time offset:
        // pixels -> fraction of the width -> sample frames -> milliseconds.
        const auto deltaSamples = (double) e.getDistanceFromDragStartX() / juce::jmax (1, getWidth())
                                * (double) sample->getLengthInSamples();
        const auto deltaMs = (float) (deltaSamples / sample->sampleRate * 1000.0);

        // The grid repeats every step, so wrap rather than clamp - the grid keeps sliding.
        // e.g. with a 125 ms step, 130 ms wraps to 5 ms (the same grid), and -5 ms to 120 ms.
        // (The double fmod keeps the result positive for negative values.)
        auto ms = offsetAtDragStart + deltaMs;
        const auto stepMs = (float) (layout.stepSamples / sample->sampleRate * 1000.0);

        if (stepMs > 0.0f && stepMs <= maxGridOffsetMs)
            ms = std::fmod (std::fmod (ms, stepMs) + stepMs, stepMs);
        else
            ms = juce::jlimit (0.0f, maxGridOffsetMs, ms); // step longer than the knob's range: clamp

        // Set the parameter like the knob would, so the host sees (and can record) it.
        offsetParam->setValueNotifyingHost (offsetParam->convertTo0to1 (ms));
        repaint();
        return;
    }

    // Once the mouse has moved far enough, turn the click into a drag-out of the slice.
    if (pressedSlice >= 0 && ! dragOutStarted && e.getDistanceFromDragStart() > dragOutThreshold)
    {
        dragOutStarted = true;
        stopAudition();
        dragSliceOut (pressedSlice);
    }
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (draggingOffset)
    {
        draggingOffset = false;
        offsetParam->endChangeGesture(); // must pair with beginChangeGesture in mouseDown
        repaint();
    }

    stopAudition();
    pressedSlice = -1;
}

void WaveformView::startAudition (int sliceIndex)
{
    stopAudition();

    const auto note = rootNote + sliceIndex;

    if (sliceIndex < 0 || ! juce::isPositiveAndBelow (note, 128))
        return; // clicked outside the slices, or the slice has no MIDI note

    // Press the note on the shared keyboard state; the processor turns it into
    // a MIDI note-on in the next audio block, exactly like the on-screen keyboard.
    auditionNote = note;
    processor.keyboardState.noteOn (1, note, auditionVelocity);
}

void WaveformView::stopAudition()
{
    if (auditionNote >= 0)
        processor.keyboardState.noteOff (1, auditionNote, 0.0f);

    auditionNote = -1;
}

void WaveformView::dragSliceOut (int sliceIndex)
{
    if (sample == nullptr || ! juce::isPositiveAndBelow (sliceIndex, layout.numSlices))
        return;

    // The OS can only drag real files, so write the slice to disk first.
    const auto folder = SliceExporter::getDragExportFolder (*sample);
    const auto file = folder.getChildFile (SliceExporter::fileNameForSlice (*sample, sliceIndex, rootNote));
    const auto error = SliceExporter::writeSlice (*sample, layout.slices[(size_t) sliceIndex], file);

    if (error.isNotEmpty())
    {
        if (onStatus)
            onStatus (error, true);
        return;
    }

    if (onStatus)
        onStatus ("Dragging " + file.getFileName(), false);

    // Hand the file to the operating system's drag-and-drop. From here the user can
    // drop it on any app that accepts audio files (DAW timeline, Finder, Explorer).
    // `false` = copy, not move: the file stays in the drag-export folder.
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

//==============================================================================
// Files dragged IN from the operating system
//==============================================================================

bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    // Accept the drag if at least one of the files looks like audio.
    for (const auto& path : files)
        if (isSupportedAudioFile (juce::File (path), processor.getFormatManager()))
            return true;

    return false;
}

void WaveformView::fileDragEnter (const juce::StringArray&, int, int)
{
    fileDragActive = true; // draw the highlight border
    repaint();
}

void WaveformView::fileDragExit (const juce::StringArray&)
{
    fileDragActive = false;
    repaint();
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    fileDragActive = false;
    repaint();

    // Load the first audio file among those dropped (only one sample at a time).
    for (const auto& path : files)
    {
        const juce::File file (path);

        if (isSupportedAudioFile (file, processor.getFormatManager()))
        {
            if (onFileDropped)
                onFileDropped (file);
            return;
        }
    }
}

} // namespace sliceable
