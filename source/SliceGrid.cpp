// =============================================================================
// SliceGrid.cpp - implementation of the grid maths declared in SliceGrid.h.
//
// Read SliceGrid.h first for the big picture and the units used here.
// Everything in this file is pure arithmetic: no JUCE, no memory allocation,
// no global state. That makes it safe on the audio thread and easy to test.
// =============================================================================

#include "SliceGrid.h"

#include <algorithm>
#include <cmath>

namespace sliceable
{

double TimeSignature::quarterNotesPerBar() const noexcept
{
    // Guard against a host reporting 0 for either number (would divide by zero).
    const auto num = std::max (1, numerator);
    const auto den = std::max (1, denominator);

    // A bar holds `num` notes of length (4 / den) quarter notes.
    // e.g. 6/8 -> 6 eighth notes -> 6 * (4/8) = 3 quarter notes.
    return num * 4.0 / den;
}

double clampBpm (double bpm) noexcept
{
    // NaN or infinity can arrive from a misbehaving host or a divide by zero
    // upstream. Fall back to a sensible tempo instead of propagating garbage.
    if (! std::isfinite (bpm))
        return 120.0;

    return std::clamp (bpm, minBpm, maxBpm);
}

double gridStepSamples (const GridSettings& settings, double sampleRate) noexcept
{
    // Clamp the index so a corrupt or out-of-range parameter can't read past the table.
    const auto index = std::clamp (settings.divisionIndex, 0, (int) gridDivisions.size() - 1);
    const auto& division = gridDivisions[(size_t) index];

    // How long is one grid step, in quarter notes?
    // Bar-based divisions depend on the time signature; note-based ones don't.
    const auto quarterNotes = division.bars > 0
                                ? division.bars * settings.timeSignature.quarterNotesPerBar()
                                : division.quarterNotes;

    // One quarter note lasts 60 / BPM seconds, i.e. (sampleRate * 60 / BPM) frames.
    const auto samplesPerQuarter = sampleRate * 60.0 / clampBpm (settings.bpm);
    return quarterNotes * samplesPerQuarter;
}

double bpmToFitBars (int64_t lengthSamples, double sampleRate, double bars, TimeSignature timeSignature) noexcept
{
    // Nothing sensible to fit: return the default tempo.
    if (lengthSamples <= 0 || sampleRate <= 0.0 || bars <= 0.0)
        return 120.0;

    // If the sample lasts `seconds` and must contain `quarterNotes` beats,
    // then beats-per-minute = quarterNotes / seconds * 60.
    const auto seconds = (double) lengthSamples / sampleRate;
    const auto quarterNotes = bars * timeSignature.quarterNotesPerBar();
    return clampBpm (quarterNotes * 60.0 / seconds);
}

void computeSlices (const GridSettings& settings,
                    int64_t lengthSamples,
                    double sampleRate,
                    SliceLayout& result) noexcept
{
    // Start from an empty result so early returns leave it in a valid state.
    result.numSlices = 0;
    result.numUnmapped = 0;
    result.stepSamples = 0.0;

    if (lengthSamples <= 0 || sampleRate <= 0.0)
        return; // no audio -> no slices

    const auto step = gridStepSamples (settings, sampleRate);
    result.stepSamples = step;

    // Shortest slice we allow, in whole frames (at least 1 so every slice has audio).
    const auto minLength = std::max<int64_t> (1, (int64_t) std::ceil (settings.minSliceSamples));

    // Defensive: a step under one frame, or a broken offset, can't form a usable
    // grid. Treat the whole sample as a single slice. (`! (step >= 1.0)` is
    // written this way so that a NaN step also takes this branch.)
    if (! (step >= 1.0) || ! std::isfinite (settings.offsetSamples))
    {
        result.slices[0] = { 0, lengthSamples };
        result.numSlices = 1;
        return;
    }

    // Grid lines sit at   offset + k * step   for every integer k (k may be negative,
    // so the grid also extends BEFORE the offset). Find the first k whose line lies
    // strictly after frame 0 - a line at exactly 0 would create an empty slice.
    const auto offset = settings.offsetSamples;
    auto k = std::floor (-offset / step) + 1.0;

    int64_t sliceStart = 0; // start of the slice currently being built

    for (;; k += 1.0)
    {
        // Compute each line from k rather than adding `step` repeatedly. Repeated
        // addition would accumulate floating-point error over hundreds of slices
        // (noticeable with triplets, whose steps aren't whole numbers of frames).
        const auto position = offset + k * step;

        if (position >= (double) lengthSamples)
            break; // past the end of the sample: no more cuts

        // Cuts have to land on a whole frame; round to the nearest one.
        const auto boundary = (int64_t) std::llround (position);

        if (boundary - sliceStart < minLength)
            continue; // would create a sliver: skip this line, merging it into the next slice

        if (lengthSamples - boundary < minLength)
            break;    // the final slice would be a sliver: let the current slice run to the end

        // Close the current slice at this grid line and start the next one here.
        result.slices[(size_t) result.numSlices++] = { sliceStart, boundary };
        sliceStart = boundary;

        if (result.numSlices == maxSlices)
        {
            // Out of MIDI notes. Stop here and report (approximately) how many grid
            // slices were left over, so the UI can warn that the grid is too fine.
            // The audio after this point is simply not mapped to any note.
            result.numUnmapped = (int) std::ceil ((double) (lengthSamples - sliceStart) / step);
            return;
        }
    }

    // The last slice always runs to the end of the sample (it may be shorter than a step).
    result.slices[(size_t) result.numSlices++] = { sliceStart, lengthSamples };
}

int findSliceAt (const SliceLayout& layout, int64_t samplePos) noexcept
{
    const auto* first = layout.begin();
    const auto* last = layout.end();

    // Slices are sorted and back-to-back, so binary search for the first slice
    // whose end lies after samplePos. That slice contains it (if any does).
    const auto* it = std::upper_bound (first, last, samplePos,
                                       [] (int64_t pos, const Slice& s) { return pos < s.end; });

    // Not found, or before the first slice (negative positions): no slice here.
    if (it == last || samplePos < it->start)
        return -1;

    return (int) (it - first);
}

} // namespace sliceable
