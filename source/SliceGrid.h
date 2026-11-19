#pragma once

// Grid slicing maths, kept free of JUCE so it can be reasoned about and tested in isolation.
//
// The model follows "Separate at Grid" in Pro Tools and "Slice at Grid" in Logic Pro:
// a grid is laid over the sample (tempo + time signature + note division), anchored
// at an offset, and the sample is cut at every grid line that falls inside it.
// Slice i is then played by MIDI note (rootNote + i).

#include <array>
#include <cstdint>
#include <string_view>

namespace sliceable
{

struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;

    double quarterNotesPerBar() const noexcept;
};

struct GridDivision
{
    std::string_view name;
    double quarterNotes; // length of one grid step in quarter notes (ignored when bars > 0)
    int bars;            // when > 0, the step is this many whole bars
};

// The order of this table defines the "Division" parameter's choices.
// Append new entries at the end only, or saved sessions will change meaning.
inline constexpr std::array<GridDivision, 18> gridDivisions {{
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

inline constexpr int defaultDivisionIndex = 12; // 1/16

inline constexpr double minBpm = 10.0;
inline constexpr double maxBpm = 999.0;

// One slice per MIDI note at most.
inline constexpr int maxSlices = 128;

struct GridSettings
{
    double bpm = 120.0;
    int divisionIndex = defaultDivisionIndex;
    TimeSignature timeSignature;
    double offsetSamples = 0.0;    // a grid line sits here; the grid extends both ways from it
    double minSliceSamples = 1.0;  // grid lines closer than this to a neighbour are dropped
};

struct Slice
{
    int64_t start = 0; // inclusive, in sample frames
    int64_t end = 0;   // exclusive

    int64_t length() const noexcept { return end - start; }
    bool operator== (const Slice&) const = default;
};

struct SliceLayout
{
    std::array<Slice, maxSlices> slices {};
    int numSlices = 0;
    int numUnmapped = 0;      // approx. grid slices beyond maxSlices that were not mapped
    double stepSamples = 0.0; // grid step actually used

    const Slice* begin() const noexcept { return slices.data(); }
    const Slice* end() const noexcept   { return slices.data() + numSlices; }
};

double clampBpm (double bpm) noexcept;

// Length of one grid step, in sample frames at the given sample rate.
double gridStepSamples (const GridSettings& settings, double sampleRate) noexcept;

// The tempo at which a sample of the given length is exactly `bars` bars long.
double bpmToFitBars (int64_t lengthSamples, double sampleRate, double bars, TimeSignature) noexcept;

// Cuts [0, lengthSamples) at every grid line. Never allocates, so it is safe on the audio thread.
void computeSlices (const GridSettings& settings,
                    int64_t lengthSamples,
                    double sampleRate,
                    SliceLayout& result) noexcept;

// Index of the slice containing samplePos, or -1.
int findSliceAt (const SliceLayout& layout, int64_t samplePos) noexcept;

} // namespace sliceable
