#include "WaveformView.h"

#include "Parameters.h"
#include "PluginProcessor.h"
#include "SliceExporter.h"
#include "SliceLookAndFeel.h"

namespace sliceable
{

namespace
{
    constexpr float labelHeight = 20.0f;
    constexpr int dragOutThreshold = 6;
    constexpr float auditionVelocity = 0.8f;
}

WaveformView::WaveformView (SliceAbleProcessor& p)
    : processor (p),
      offsetParam (p.getState().getParameter (ParamIDs::gridOffset))
{
    setRepaintsOnMouseActivity (false);
}

void WaveformView::setSample (SampleData::Ptr newSample, const juce::String& missingSamplePath)
{
    stopAudition();
    sample = std::move (newSample);
    missingPath = missingSamplePath;
    hoverSlice = -1;
    rebuildPeaks();
    repaint();
}

void WaveformView::setLayout (const SliceLayout& newLayout, int newRootNote)
{
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
    const auto& engine = processor.getEngine();
    auto changed = false;

    for (int i = 0; i < maxSlices; ++i)
    {
        const auto s = engine.isSliceSounding (i);
        changed = changed || s != sounding[(size_t) i];
        sounding[(size_t) i] = s;
    }

    const auto position = engine.getLatestVoicePosition();

    if (changed || std::abs (position - playPosition) > 0.5)
    {
        playPosition = position;
        repaint();
    }
}

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

    for (int px = 0; px < width; ++px)
    {
        const auto start = (int) ((int64_t) px * length / width);
        const auto end = juce::jmax (start + 1, (int) ((int64_t) (px + 1) * length / width));
        Peak peak { 1.0f, -1.0f };

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
    rebuildPeaks();
}

//==============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (Colours::sliceBandA);
    g.fillRoundedRectangle (bounds, 6.0f);

    if (sample == nullptr)
    {
        g.setColour (fileDragActive ? Colours::accent : Colours::panelOutline);
        juce::Path outline;
        outline.addRoundedRectangle (bounds.reduced (1.0f), 6.0f);
        const float dashes[] { 6.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (outline, outline, dashes, 2);
        g.fillPath (outline);

        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText ("Drop an audio file here, or click to load", bounds.withTrimmedBottom (24.0f),
                    juce::Justification::centred);

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

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (bounds, 6.0f);
    g.reduceClipRegion (clip);

    // Slice bands
    for (int i = 0; i < layout.numSlices; ++i)
    {
        const auto& slice = layout.slices[(size_t) i];
        const auto x0 = sampleToX (slice.start);
        const auto x1 = sampleToX (slice.end);
        const auto band = juce::Rectangle<float> (x0, 0.0f, x1 - x0, bounds.getHeight());

        g.setColour ((i % 2) == 0 ? Colours::sliceBandA : Colours::sliceBandB);
        g.fillRect (band);

        if (sounding[(size_t) i])
        {
            g.setColour (Colours::accent.withAlpha (0.22f));
            g.fillRect (band);
        }
        else if (i == hoverSlice)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (band);
        }
    }

    // Region past the last mapped slice (grid finer than the 128 MIDI notes allow)
    if (layout.numSlices > 0 && layout.numUnmapped > 0)
    {
        const auto x = sampleToX (layout.slices[(size_t) layout.numSlices - 1].end);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (juce::Rectangle<float> (x, 0.0f, bounds.getWidth() - x, bounds.getHeight()));
    }

    // Waveform
    if (! peaks.empty())
    {
        const auto midY = wave.getCentreY();
        const auto halfHeight = wave.getHeight() * 0.45f;
        juce::RectangleList<float> columns;

        for (size_t px = 0; px < peaks.size(); ++px)
        {
            const auto top = midY - juce::jlimit (-1.0f, 1.0f, peaks[px].max) * halfHeight;
            const auto bottom = midY - juce::jlimit (-1.0f, 1.0f, peaks[px].min) * halfHeight;
            columns.addWithoutMerging ({ (float) px, top, 1.0f, juce::jmax (1.0f, bottom - top) });
        }

        g.setColour (Colours::waveform.withAlpha (0.9f));
        g.fillRectList (columns);

        g.setColour (Colours::waveform.withAlpha (0.15f));
        g.drawHorizontalLine ((int) midY, 0.0f, bounds.getWidth());
    }

    // Grid lines (slice boundaries) and labels
    g.setFont (juce::FontOptions (11.0f));

    for (int i = 0; i < layout.numSlices; ++i)
    {
        const auto& slice = layout.slices[(size_t) i];
        const auto x0 = sampleToX (slice.start);
        const auto x1 = sampleToX (slice.end);

        if (i > 0)
        {
            g.setColour (sounding[(size_t) i] || sounding[(size_t) i - 1] ? Colours::accent : Colours::gridLine);
            g.drawVerticalLine (juce::roundToInt (x0), 0.0f, bounds.getHeight());
        }

        const auto labelArea = juce::Rectangle<float> (x0, labels.getY(), x1 - x0, labels.getHeight()).reduced (3.0f, 0.0f);

        if (labelArea.getWidth() < 12.0f)
            continue;

        const auto note = rootNote + i;
        auto text = juce::String (i + 1);

        if (labelArea.getWidth() > 44.0f && juce::isPositiveAndBelow (note, 128))
            text << "  " << noteName (note);

        g.setColour (sounding[(size_t) i] ? Colours::accent : Colours::dimText);
        g.drawText (text, labelArea, juce::Justification::centredLeft, false);
    }

    g.setColour (Colours::panelOutline);
    g.drawHorizontalLine ((int) labels.getBottom(), 0.0f, bounds.getWidth());

    // Playhead of the most recent voice
    if (playPosition >= 0.0)
    {
        g.setColour (Colours::text.withAlpha (0.8f));
        g.drawVerticalLine (juce::roundToInt (sampleToX ((int64_t) playPosition)), wave.getY(), wave.getBottom());
    }

    if (draggingOffset)
    {
        g.setColour (Colours::accent);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("Grid offset: " + (offsetParam != nullptr ? offsetParam->getCurrentValueAsText() : juce::String()),
                    wave.reduced (8.0f), juce::Justification::topRight, false);
    }

    g.restoreState();

    if (fileDragActive)
    {
        g.setColour (Colours::accent);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 6.0f, 2.0f);
    }
}

//==============================================================================
void WaveformView::mouseMove (const juce::MouseEvent& e)
{
    const auto slice = sample != nullptr ? sliceAtX ((float) e.x) : -1;

    if (slice != hoverSlice)
    {
        hoverSlice = slice;
        repaint();
    }

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

    if (sample == nullptr)
    {
        if (onLoadRequest)
            onLoadRequest();
        return;
    }

    if (e.mods.isAltDown() && offsetParam != nullptr)
    {
        draggingOffset = true;
        offsetAtDragStart = offsetParam->convertFrom0to1 (offsetParam->getValue());
        offsetParam->beginChangeGesture();
        repaint();
        return;
    }

    pressedSlice = sliceAtX ((float) e.x);
    startAudition (pressedSlice);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (sample == nullptr)
        return;

    if (draggingOffset)
    {
        const auto deltaSamples = (double) e.getDistanceFromDragStartX() / juce::jmax (1, getWidth())
                                * (double) sample->getLengthInSamples();
        const auto deltaMs = (float) (deltaSamples / sample->sampleRate * 1000.0);

        // The grid repeats every step, so wrap rather than clamp - the grid keeps sliding.
        auto ms = offsetAtDragStart + deltaMs;
        const auto stepMs = (float) (layout.stepSamples / sample->sampleRate * 1000.0);

        if (stepMs > 0.0f && stepMs <= maxGridOffsetMs)
            ms = std::fmod (std::fmod (ms, stepMs) + stepMs, stepMs);
        else
            ms = juce::jlimit (0.0f, maxGridOffsetMs, ms);

        offsetParam->setValueNotifyingHost (offsetParam->convertTo0to1 (ms));
        repaint();
        return;
    }

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
        offsetParam->endChangeGesture();
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
        return;

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

    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

//==============================================================================
bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& path : files)
        if (isSupportedAudioFile (juce::File (path), processor.getFormatManager()))
            return true;

    return false;
}

void WaveformView::fileDragEnter (const juce::StringArray&, int, int)
{
    fileDragActive = true;
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
