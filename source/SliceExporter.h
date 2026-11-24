#pragma once

#include "SampleData.h"
#include "SliceGrid.h"

namespace sliceable
{

// Writes slices out as WAV files at the sample's native rate - the equivalent of
// "Separate at Grid" followed by exporting the resulting clips.
namespace SliceExporter
{
    // e.g. "DrumLoop_03_D1.wav" for slice index 2 with root note C1.
    juce::String fileNameForSlice (const SampleData& sample, int sliceIndex, int rootNote);

    // Writes one slice. Returns an error message, or an empty string on success.
    juce::String writeSlice (const SampleData& sample, Slice slice, const juce::File& destination,
                             int bitsPerSample = 24);

    // Writes every slice in the layout into `folder`. Returns the files written.
    juce::Array<juce::File> writeAllSlices (const SampleData& sample, const SliceLayout& layout, int rootNote,
                                            const juce::File& folder, juce::String& error);

    // A stable folder for slices dragged out of the plug-in. Some DAWs reference
    // dropped files in place rather than copying them, so this is not a temp folder.
    juce::File getDragExportFolder (const SampleData& sample);
}

} // namespace sliceable
