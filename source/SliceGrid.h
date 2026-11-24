#pragma once

// =============================================================================
// SliceGrid.h - the grid maths: tempo + note division + offset -> slice points
//
// This is the heart of the plug-in. It answers one question: "given a sample of
// N frames, where does a musical grid cut it?" It mirrors "Separate Clip at
// Grid" in Pro Tools and "Slice at Grid" in Logic Pro:
//
//   1. Work out how long one grid step is (e.g. a 1/16 note at 120 BPM).
//   2. Lay grid lines over the sample every step, anchored at an offset.
//   3. Cut the sample at every grid line that falls inside it.
//
//   sample:      |=============================================|
//   grid lines:  |     |     |     |     |     |     |     |   |
//   slices:      [ 0  ][ 1  ][ 2  ][ 3  ][ 4  ][ 5  ][ 6  ][ 7 ]
//
// Slice i is later played by MIDI note (rootNote + i) - see SliceEngine.
//
// This file deliberately has NO JUCE dependency. It is plain C++ so it can be
// read, reasoned about and unit-tested on its own (see tests/TestMain.cpp).
// Nothing here allocates memory, so it is safe to call from the audio thread.
//
// Units used throughout:
//   - "samples" / "frames": positions in the audio file, one frame = one point
//     in time across all channels. Always at the FILE's sample rate, not the
//     host's (the playback engine converts between them).
//   - "quarter notes": musical length. One quarter note = one beat in 4/4.
//
// Common changes:
//   - Add a grid division ......... append a row to `gridDivisions` (read the
//                                   warning above it first).
//   - Change the default division . edit `defaultDivisionIndex`.
//   - Allow more than 128 slices ... not possible with one-note-per-slice MIDI
//                                   mapping; see `maxSlices`.
//   - Change how tiny slices merge . `GridSettings::minSliceSamples` (its value
//                                   is set in PluginProcessor.cpp).
// =============================================================================

#include <array>
#include <cstdint>
#include <string_view>

namespace sliceable
{

/** A musical time signature such as 4/4, 3/4 or 6/8.
    Used to know how long "1 Bar" is: a bar of 4/4 is 4 quarter notes, a bar of
    6/8 is 3 quarter notes (six eighth notes), and so on.
*/
struct TimeSignature
{
    int numerator = 4;   // beats per bar (top number)
    int denominator = 4; // note value of one beat (bottom number): 4 = quarter, 8 = eighth

    /** Length of one bar measured in quarter notes, e.g. 4/4 -> 4.0, 6/8 -> 3.0, 7/8 -> 3.5. */
    double quarterNotesPerBar() const noexcept;
};

/** One entry in the "Division" menu: how far apart grid lines are. */
struct GridDivision
{
    std::string_view name; // text shown in the UI and to the host
    double quarterNotes;   // length of one grid step in quarter notes (ignored when bars > 0)
    int bars;              // when > 0, the step is this many whole bars instead
                           // (bar length then depends on the time signature)
};

// -----------------------------------------------------------------------------
// The list of grid divisions offered in the "Division" menu.
//
// !! WARNING !!
// The ORDER of this table defines the "Division" parameter's values. DAWs save
// the parameter as an index into this list, so re-ordering or inserting rows
// would silently change the division in every saved project. To add a
// division, APPEND it at the end and bump the array size.
//
// Note lengths in quarter notes:
//   - straight:  1/2 = 2,  1/4 = 1,  1/8 = 0.5 ...
//   - dotted:    1.5 x the straight value (e.g. 1/8 dotted = 0.75)
//   - triplet:   2/3 x the straight value (three in the space of two)
// -----------------------------------------------------------------------------
inline constexpr std::array<GridDivision, 18> gridDivisions {{
    //  name            quarter notes  bars
    { "4 Bars",      0.0,         4 },
    { "2 Bars",      0.0,         2 },
    { "1 Bar",       0.0,         1 },
    { "1/2",         2.0,         0 },
    { "1/2 Dotted",  3.0,         0 },
    { "1/2 Triplet", 4.0 / 3.0,   0 },
    { "1/4",         1.0,         0 },
    { "1/4 Dotted",  1.5,         0 },
    { "1/4 Triplet", 2.0 / 3.0,   0 },
    { "1/8",         0.5,         0 },
    { "1/8 Dotted",  0.75,        0 },
    { "1/8 Triplet", 1.0 / 3.0,   0 },
    { "1/16",        0.25,        0 },
    { "1/16 Dotted", 0.375,       0 },
    { "1/16 Triplet",1.0 / 6.0,   0 },
    { "1/32",        0.125,       0 },
    { "1/32 Triplet",1.0 / 12.0,  0 },
    { "1/64",        0.0625,      0 },
}};

// Index into gridDivisions used when the plug-in is first opened.
inline constexpr int defaultDivisionIndex = 12; // 1/16

// Tempo limits. Anything outside this range is clamped, which also guarantees
// the grid step can never become so small that slicing loops forever.
inline constexpr double minBpm = 10.0;
inline constexpr double maxBpm = 999.0;

// Hard cap on the number of slices. Each slice is triggered by its own MIDI
// note and MIDI only has 128 notes (0-127), so more slices could never play.
// Anything past the 128th slice is reported as "unmapped" in SliceLayout.
inline constexpr int maxSlices = 128;

/** Everything computeSlices() needs to know to place the grid. */
struct GridSettings
{
    double bpm = 120.0;                       // tempo used for the grid (clamped to minBpm..maxBpm)
    int divisionIndex = defaultDivisionIndex; // index into gridDivisions
    TimeSignature timeSignature;              // only matters for the "Bar" divisions
    double offsetSamples = 0.0;               // a grid line sits exactly here; the grid
                                              // then extends both ways from this point
    double minSliceSamples = 1.0;             // grid lines closer than this to a neighbouring
                                              // cut are skipped, so no slice is a tiny sliver
};

/** One slice of the sample: the half-open range [start, end) in sample frames. */
struct Slice
{
    int64_t start = 0; // first frame of the slice (inclusive)
    int64_t end = 0;   // one past the last frame (exclusive), i.e. the next slice's start

    int64_t length() const noexcept { return end - start; }
    bool operator== (const Slice&) const = default;
};

/** The result of slicing: an ordered list of back-to-back slices.

    Slices are contiguous: slices[0].start == 0, and each slice starts where the
    previous one ended. The storage is a fixed-size array (not a vector) so that
    computing a layout never allocates memory - a requirement on the audio thread.
    Only the first `numSlices` entries are valid; begin()/end() let you write
    `for (const auto& slice : layout)`.
*/
struct SliceLayout
{
    std::array<Slice, maxSlices> slices {};
    int numSlices = 0;        // how many entries of `slices` are in use
    int numUnmapped = 0;      // approx. number of further grid slices that didn't fit in maxSlices
    double stepSamples = 0.0; // the grid step length that was used, in frames

    const Slice* begin() const noexcept { return slices.data(); }
    const Slice* end() const noexcept   { return slices.data() + numSlices; }
};

/** Clamps a tempo to [minBpm, maxBpm]. Non-numbers (NaN, infinity) become 120 BPM. */
double clampBpm (double bpm) noexcept;

/** Length of one grid step, in sample frames at the given sample rate.
    Example: 1/16 note at 120 BPM and 48 kHz = 0.25 quarter notes x 24000 frames = 6000 frames.
*/
double gridStepSamples (const GridSettings& settings, double sampleRate) noexcept;

/** The tempo at which a sample of the given length is exactly `bars` bars long.
    Used by the "Fit to Bars" tempo mode, for loops whose tempo you don't know.
    Example: a 4-second sample fitted to 2 bars of 4/4 -> 8 beats in 4 s -> 120 BPM.
*/
double bpmToFitBars (int64_t lengthSamples, double sampleRate, double bars, TimeSignature) noexcept;

/** Cuts the range [0, lengthSamples) at every grid line and writes the slices to `result`.

    @param settings      tempo, division, time signature and offset of the grid
    @param lengthSamples length of the sample, in frames
    @param sampleRate    the SAMPLE FILE's sample rate (frames per second)
    @param result        receives the slices; any previous contents are overwritten

    Never allocates, so it is safe to call on the audio thread.
*/
void computeSlices (const GridSettings& settings,
                    int64_t lengthSamples,
                    double sampleRate,
                    SliceLayout& result) noexcept;

/** Returns the index of the slice containing `samplePos`, or -1 if it is outside every slice.
    Used by the UI to work out which slice the mouse is over.
*/
int findSliceAt (const SliceLayout& layout, int64_t samplePos) noexcept;

} // namespace sliceable
