#pragma once

// =============================================================================
// SliceExporter.h - writes slices to disk as WAV files.
//
// Two features use this:
//   - "Export Slices..." button: writes every slice into a folder the user picks
//     (the editor calls writeAllSlices).
//   - Dragging a slice out of the waveform into a DAW: the slice is written to a
//     file first, then that file is what gets dragged (WaveformView calls
//     writeSlice + getDragExportFolder).
//
// Together these are the equivalent of "Separate at Grid" followed by exporting
// the resulting clips in Pro Tools / Logic.
//
// Files are written at the sample's ORIGINAL sample rate and channel count,
// so a slice is a bit-for-bit cut of the source (apart from 24-bit conversion).
//
// These functions do file I/O, so call them from the message thread, never the
// audio thread.
//
// Common changes:
//   - File naming scheme ...... fileNameForSlice()
//   - Bit depth ............... the bitsPerSample default below (16, 24 or 32)
//   - Drag-out folder ......... getDragExportFolder()
//   - Another format (AIFF) ... swap juce::WavAudioFormat in SliceExporter.cpp
//                               and change the ".wav" extension in fileNameForSlice.
// =============================================================================

#include "SampleData.h"
#include "SliceGrid.h"

namespace sliceable
{

namespace SliceExporter
{
    /** Builds a file name for a slice: <sample name>_<slice number>_<note>.wav
        e.g. "DrumLoop_03_D1.wav" for slice index 2 with root note C1.
        Slice numbers are 1-based and zero-padded so files sort in order. Sharps
        are written as "s" ("Ds1" instead of "D#1") because JUCE removes "#"
        from file names.
    */
    juce::String fileNameForSlice (const SampleData& sample, int sliceIndex, int rootNote);

    /** Writes one slice to `destination` as a WAV file, replacing any existing file.
        Creates the destination folder if needed.
        @returns an error message for the user, or an empty string on success.
    */
    juce::String writeSlice (const SampleData& sample, Slice slice, const juce::File& destination,
                             int bitsPerSample = 24);

    /** Writes every slice in the layout into `folder`, named by fileNameForSlice().
        Stops at the first failure and puts the reason in `error` (empty on success).
        @returns the files that were written successfully.
    */
    juce::Array<juce::File> writeAllSlices (const SampleData& sample, const SliceLayout& layout, int rootNote,
                                            const juce::File& folder, juce::String& error);

    /** The folder that dragged-out slices are written to:
        ~/Music/Slice-able/Slices/<sample name>/  (the user's Music folder on each OS).

        This is deliberately NOT a temporary folder: some DAWs (e.g. Ableton Live)
        reference a dropped file where it lies instead of copying it, so the file
        must stay put after the drag.
    */
    juce::File getDragExportFolder (const SampleData& sample);
}

} // namespace sliceable
