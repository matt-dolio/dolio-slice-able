#include "SliceGrid.h"

#include <algorithm>
#include <cmath>

namespace sliceable
{

double TimeSignature::quarterNotesPerBar() const noexcept
{
    const auto num = std::max (1, numerator);
    const auto den = std::max (1, denominator);
    return num * 4.0 / den;
}

double clampBpm (double bpm) noexcept
{
    if (! std::isfinite (bpm))
        return 120.0;

    return std::clamp (bpm, minBpm, maxBpm);
}

double gridStepSamples (const GridSettings& settings, double sampleRate) noexcept
{
    const auto index = std::clamp (settings.divisionIndex, 0, (int) gridDivisions.size() - 1);
    const auto& division = gridDivisions[(size_t) index];

    const auto quarterNotes = division.bars > 0
                                ? division.bars * settings.timeSignature.quarterNotesPerBar()
                                : division.quarterNotes;

    const auto samplesPerQuarter = sampleRate * 60.0 / clampBpm (settings.bpm);
    return quarterNotes * samplesPerQuarter;
}

double bpmToFitBars (int64_t lengthSamples, double sampleRate, double bars, TimeSignature timeSignature) noexcept
{
    if (lengthSamples <= 0 || sampleRate <= 0.0 || bars <= 0.0)
        return 120.0;

    const auto seconds = (double) lengthSamples / sampleRate;
    const auto quarterNotes = bars * timeSignature.quarterNotesPerBar();
    return clampBpm (quarterNotes * 60.0 / seconds);
}

void computeSlices (const GridSettings& settings,
                    int64_t lengthSamples,
                    double sampleRate,
                    SliceLayout& result) noexcept
{
    result.numSlices = 0;
    result.numUnmapped = 0;
    result.stepSamples = 0.0;

    if (lengthSamples <= 0 || sampleRate <= 0.0)
        return;

    const auto step = gridStepSamples (settings, sampleRate);
    result.stepSamples = step;

    const auto minLength = std::max<int64_t> (1, (int64_t) std::ceil (settings.minSliceSamples));

    if (! (step >= 1.0) || ! std::isfinite (settings.offsetSamples))
    {
        result.slices[0] = { 0, lengthSamples };
        result.numSlices = 1;
        return;
    }

    // Grid lines sit at offset + k * step for every integer k. Start from the first one after 0.
    const auto offset = settings.offsetSamples;
    auto k = std::floor (-offset / step) + 1.0;

    int64_t sliceStart = 0;

    for (;; k += 1.0)
    {
        const auto position = offset + k * step;

        if (position >= (double) lengthSamples)
            break;

        const auto boundary = (int64_t) std::llround (position);

        if (boundary - sliceStart < minLength)
            continue; // would create a sliver: merge it into the next slice

        if (lengthSamples - boundary < minLength)
            break;    // the final slice would be a sliver: let this one run to the end

        result.slices[(size_t) result.numSlices++] = { sliceStart, boundary };
        sliceStart = boundary;

        if (result.numSlices == maxSlices)
        {
            // Out of MIDI notes. Report how much of the grid is left unmapped.
            result.numUnmapped = (int) std::ceil ((double) (lengthSamples - sliceStart) / step);
            return;
        }
    }

    result.slices[(size_t) result.numSlices++] = { sliceStart, lengthSamples };
}

int findSliceAt (const SliceLayout& layout, int64_t samplePos) noexcept
{
    const auto* first = layout.begin();
    const auto* last = layout.end();

    // Slices are contiguous and sorted, so binary search on the end position.
    const auto* it = std::upper_bound (first, last, samplePos,
                                       [] (int64_t pos, const Slice& s) { return pos < s.end; });

    if (it == last || samplePos < it->start)
        return -1;

    return (int) (it - first);
}

} // namespace sliceable
