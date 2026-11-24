#pragma once

// =============================================================================
// WaveformView.h - the big waveform display in the middle of the window.
//
// What it draws:
//   - the sample's waveform,
//   - alternating shaded bands, one per slice, with the grid lines between them,
//   - a label row along the top: slice number + the note that plays it,
//   - highlights for slices that are playing, plus a moving playhead line,
//   - a darkened area for audio past the last playable slice (grid too fine).
//
// What it does with the mouse:
//   - click a slice ........... auditions it (plays its note while held)
//   - drag a slice out ........ writes it to a WAV and starts an OS-level file
//                               drag, so it can be dropped on a DAW track
//   - Alt/Option + drag ....... slides the grid (changes the Grid Offset parameter)
//   - click when empty ........ opens the "Load Sample" dialog
//   - drop an audio file ...... loads it
//
// It reports back to the editor through the std::function callbacks below
// (onLoadRequest, onFileDropped, onStatus) rather than knowing about the editor.
// Runs on the message thread only.
//
// Coordinates: x = 0 is the start of the sample, x = getWidth() is its end.
// sampleToX() / xToSample() convert between pixels and sample frames.
// =============================================================================

#include "SampleData.h"
#include "SliceGrid.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace sliceable
{

class SliceAbleProcessor;

class WaveformView final : public juce::Component,
                           public juce::FileDragAndDropTarget // accept files dragged in from the OS
{
public:
    explicit WaveformView (SliceAbleProcessor&);

    /** Shows a new sample (or none). `missingSamplePath`, if not empty, is shown as
        "Sample not found: ..." when there is no sample.
    */
    void setSample (SampleData::Ptr newSample, const juce::String& missingSamplePath);

    /** Updates the slices and root note. Repaints only if something changed,
        so it's cheap to call on every timer tick.
    */
    void setLayout (const SliceLayout& newLayout, int newRootNote);

    /** Reads which slices are playing (from the engine) and repaints if that changed. */
    void updatePlayState();

    // --- Callbacks to the editor (set by SliceAbleEditor) ---
    std::function<void()> onLoadRequest;                              // user clicked an empty waveform
    std::function<void (const juce::File&)> onFileDropped;            // user dropped an audio file
    std::function<void (const juce::String&, bool isError)> onStatus; // show a status message

    // --- juce::Component ---
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // --- juce::FileDragAndDropTarget ---
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    juce::Rectangle<float> getWaveArea() const;  // below the label row
    juce::Rectangle<float> getLabelArea() const; // the slice-number row along the top
    float sampleToX (int64_t samplePos) const;   // sample frame -> pixel x
    int64_t xToSample (float x) const;           // pixel x -> sample frame
    int sliceAtX (float x) const;                // slice under pixel x, or -1
    void rebuildPeaks();                         // recompute the waveform outline
    void startAudition (int sliceIndex);
    void stopAudition();
    void dragSliceOut (int sliceIndex);

    SliceAbleProcessor& processor;
    SampleData::Ptr sample;      // what we draw (null = show the "drop a file" prompt)
    juce::String missingPath;    // shown when a saved sample couldn't be found
    SliceLayout layout;          // the slices to draw
    int rootNote = 36;           // for the note-name labels

    // The waveform is drawn from a pre-computed outline: for each pixel column,
    // the lowest and highest sample value in the stretch of audio it covers.
    // Rebuilt only when the sample or the width changes, so painting stays fast.
    struct Peak { float min = 0.0f, max = 0.0f; };
    std::vector<Peak> peaks;     // one entry per pixel column

    std::array<bool, maxSlices> sounding {}; // which slices were playing at the last update
    double playPosition = -1.0;              // newest voice's position in frames, -1 = silent

    // Mouse interaction state
    int hoverSlice = -1;         // slice under the mouse (lightly highlighted)
    int pressedSlice = -1;       // slice clicked on (candidate for drag-out)
    int auditionNote = -1;       // note currently held down by a click, or -1
    bool dragOutStarted = false; // a drag-out has begun for the current click
    bool fileDragActive = false; // a file is being dragged over us (draw a highlight)

    // Alt/Option-drag on the waveform adjusts the Grid Offset parameter directly.
    bool draggingOffset = false;
    float offsetAtDragStart = 0.0f;                    // in ms
    juce::RangedAudioParameter* offsetParam = nullptr; // the Grid Offset parameter

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};

} // namespace sliceable
