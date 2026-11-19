#pragma once

#include "SampleData.h"
#include "SliceGrid.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace sliceable
{

class SliceAbleProcessor;

// Shows the sample with its grid slices.
//  - click a slice to audition it
//  - drag a slice out to drop it into the DAW timeline as an audio file
//  - Alt/Option-drag to slide the grid (sets Grid Offset)
//  - drop an audio file to load it
class WaveformView final : public juce::Component,
                           public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView (SliceAbleProcessor&);

    void setSample (SampleData::Ptr newSample, const juce::String& missingSamplePath);
    void setLayout (const SliceLayout& newLayout, int newRootNote);
    void updatePlayState();

    std::function<void()> onLoadRequest;
    std::function<void (const juce::File&)> onFileDropped;
    std::function<void (const juce::String&, bool isError)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    juce::Rectangle<float> getWaveArea() const;
    juce::Rectangle<float> getLabelArea() const;
    float sampleToX (int64_t samplePos) const;
    int64_t xToSample (float x) const;
    int sliceAtX (float x) const;
    void rebuildPeaks();
    void startAudition (int sliceIndex);
    void stopAudition();
    void dragSliceOut (int sliceIndex);

    SliceAbleProcessor& processor;
    SampleData::Ptr sample;
    juce::String missingPath;
    SliceLayout layout;
    int rootNote = 36;

    struct Peak { float min = 0.0f, max = 0.0f; };
    std::vector<Peak> peaks;

    std::array<bool, maxSlices> sounding {};
    double playPosition = -1.0;

    int hoverSlice = -1;
    int pressedSlice = -1;
    int auditionNote = -1;
    bool dragOutStarted = false;
    bool fileDragActive = false;

    bool draggingOffset = false;
    float offsetAtDragStart = 0.0f;
    juce::RangedAudioParameter* offsetParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};

} // namespace sliceable
