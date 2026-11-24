// =============================================================================
// SliceEngine.cpp - the sampler voices. See SliceEngine.h for the overview.
//
// Reminder: everything here runs on the audio thread. Don't add anything that
// allocates memory, takes a lock, logs to disk or otherwise might block.
// =============================================================================

#include "SliceEngine.h"

#include <cmath>

namespace sliceable
{

namespace
{
    /** Reads the sample at a fractional position using 4-point, 3rd-order Hermite interpolation.

        A voice's position is usually between two frames (when pitched or when the
        file's sample rate differs from the host's). Hermite interpolation looks at
        the two frames either side and fits a smooth curve through them, which
        sounds much cleaner than just picking the nearest frame or drawing a straight
        line between two. Indices outside the buffer are clamped to its first/last frame.
    */
    inline float readInterpolated (const float* data, int64_t length, double position) noexcept
    {
        const auto index = (int64_t) std::floor (position);   // frame just before the position
        const auto t = (float) (position - (double) index);   // how far towards the next frame (0..1)

        // Bounds-safe read: clamp to the first/last frame instead of reading outside the buffer.
        const auto at = [data, length] (int64_t i) noexcept
        {
            return data[i < 0 ? 0 : (i >= length ? length - 1 : i)];
        };

        // The four neighbouring frames: one before, the two either side, one after.
        const auto xm1 = at (index - 1);
        const auto x0  = at (index);
        const auto x1  = at (index + 1);
        const auto x2  = at (index + 2);

        // Standard Hermite polynomial coefficients (Catmull-Rom tangents).
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);

        // Evaluate c3*t^3 + c2*t^2 + c1*t + x0 (Horner's method).
        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    // Length of the fade applied at the very end of every slice. Cutting audio off
    // mid-waveform produces a click; a 1.5 ms fade is inaudible but removes it.
    constexpr double endFadeMs = 1.5;

    // Length of the fade used when a voice is choked (retrigger, mono mode, all-notes-off).
    constexpr double chokeFadeMs = 3.0;
}

void SliceEngine::prepare (double hostSampleRate)
{
    // Some hosts briefly report 0 Hz; never divide by zero later on.
    hostRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;

    // Convert the choke fade from milliseconds to output frames (at least 1).
    chokeFadeSamples = juce::jmax (1, (int) std::round (hostRate * chokeFadeMs / 1000.0));

    // The envelopes count time in output frames, so they need the host rate.
    for (auto& v : voices)
        v.envelope.setSampleRate (hostRate);

    stopAllVoices();
}

SampleData::Ptr SliceEngine::swapSample (SampleData::Ptr newSample) noexcept
{
    // Voices point into the old sample's frames, so they must stop first.
    stopAllVoices();

    // std::swap MOVES the pointers: no reference counts change and nothing is
    // freed here. `newSample` now holds the old sample, which we hand back.
    std::swap (sample, newSample);
    return newSample;
}

void SliceEngine::stopAllVoices() noexcept
{
    for (auto& v : voices)
    {
        v.active = false;
        v.chokeRemaining = 0;
        v.envelope.reset();
    }

    publishUiState(); // so the UI stops highlighting immediately
}

int SliceEngine::getNumActiveVoices() const noexcept
{
    int count = 0;

    for (const auto& v : voices)
        count += v.active ? 1 : 0;

    return count;
}

bool SliceEngine::isSliceSounding (int sliceIndex) const noexcept
{
    if (sliceIndex < 0 || sliceIndex >= 128)
        return false;

    // Slices 0-63 live in word 0, slices 64-127 in word 1; test the matching bit.
    const auto word = soundingMask[(size_t) (sliceIndex / 64)].load (std::memory_order_relaxed);
    return ((word >> (sliceIndex % 64)) & 1u) != 0;
}

void SliceEngine::process (juce::AudioBuffer<float>& output,
                           const juce::MidiBuffer& midi,
                           const SliceLayout& layout,
                           int rootNote,
                           const PlaybackParams& params) noexcept
{
    // Sample-accurate MIDI: instead of handling every event at the start of the
    // block, render audio UP TO each event's position, handle the event, and
    // continue. A note at position 37 therefore starts exactly on frame 37.
    //
    //   block:  |------render------|ev|----render----|ev|---render---|
    //           0               cursor                           numSamples

    const auto numSamples = output.getNumSamples();
    int cursor = 0; // how far into the block we've rendered so far

    for (const auto metadata : midi)
    {
        // Clamp in case a host sends an event position outside the block.
        const auto eventPos = juce::jlimit (0, numSamples, metadata.samplePosition);

        // Render everything between the previous event and this one.
        if (eventPos > cursor)
        {
            renderVoices (output, cursor, eventPos - cursor, params);
            cursor = eventPos;
        }

        const auto message = metadata.getMessage();

        // Note: a note-on with velocity 0 counts as a note-off (isNoteOn() is false for it).
        if (message.isNoteOn())
            startVoice (message.getNoteNumber(), message.getFloatVelocity(), layout, rootNote, params);
        else if (message.isNoteOff())
            stopNote (message.getNoteNumber(), params);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            // Sent by hosts on transport stop / panic: fade everything out quickly.
            for (auto& v : voices)
                if (v.active)
                    chokeVoice (v);
    }

    // Render the rest of the block after the last event.
    if (cursor < numSamples)
        renderVoices (output, cursor, numSamples - cursor, params);

    publishUiState();
}

void SliceEngine::startVoice (int note, float velocity, const SliceLayout& layout, int rootNote,
                              const PlaybackParams& params) noexcept
{
    if (sample == nullptr)
        return; // nothing loaded

    // Map the note to a slice: rootNote plays slice 0, the next key up slice 1, ...
    const auto sliceIndex = note - rootNote;

    if (sliceIndex < 0 || sliceIndex >= layout.numSlices)
        return; // key outside the mapped range: ignore it

    const auto& slice = layout.slices[(size_t) sliceIndex];

    if (slice.length() <= 0)
        return;

    // Retriggering a note cuts its previous instance; mono mode cuts everything (like a choke group).
    // (Voices already fading out are left alone so their fade isn't restarted.)
    for (auto& v : voices)
        if (v.active && v.chokeRemaining == 0 && (params.voiceMode == VoiceMode::mono || v.note == note))
            chokeVoice (v);

    // Grab a voice and set it up to play this slice.
    auto& v = findFreeVoice();
    v.active = true;
    v.note = note;
    v.sliceIndex = sliceIndex;
    v.sliceStart = slice.start;
    v.sliceEnd = slice.end;
    v.reverse = params.reverse;

    // Forwards: start at the slice's first frame. Reverse: start at its last frame.
    v.position = v.reverse ? (double) (slice.end - 1) : (double) slice.start;

    // Linear velocity -> volume. For a curve, try e.g. velocity * velocity here.
    v.velocityGain = velocity;
    v.startedAt = ++voiceCounter;
    v.chokeRemaining = 0;

    // Restart the envelope from silence. ADSR times are in seconds; decay is 0
    // and sustain is 1, so it is effectively an attack/release envelope.
    v.envelope.reset();
    v.envelope.setParameters ({ params.attackMs / 1000.0f, 0.0f, 1.0f, params.releaseMs / 1000.0f });
    v.envelope.noteOn();
}

void SliceEngine::stopNote (int note, const PlaybackParams& params) noexcept
{
    if (params.playMode != PlayMode::gate)
        return; // one-shot slices always play to their end

    // Gate mode: start the release phase of every voice playing this note.
    for (auto& v : voices)
        if (v.active && v.note == note && v.chokeRemaining == 0)
            v.envelope.noteOff();
}

void SliceEngine::chokeVoice (Voice& v) noexcept
{
    // Begin a short fade-out; renderVoice counts it down and then frees the voice.
    // If the voice is already fading, leave its fade as it is.
    if (v.chokeRemaining == 0)
        v.chokeRemaining = chokeFadeSamples;
}

SliceEngine::Voice& SliceEngine::findFreeVoice() noexcept
{
    // Return the first idle voice. If all are busy, "steal" one, choosing (in order):
    //   1. a voice that is already fading out (least audible to cut), then
    //   2. the voice that started longest ago.
    Voice* oldest = &voices[0];

    for (auto& v : voices)
    {
        if (! v.active)
            return v;

        // Prefer stealing a voice that is already fading out.
        const auto vFading = v.chokeRemaining > 0;
        const auto oldestFading = oldest->chokeRemaining > 0;

        if ((vFading && ! oldestFading) || (vFading == oldestFading && v.startedAt < oldest->startedAt))
            oldest = &v;
    }

    return *oldest;
}

void SliceEngine::renderVoices (juce::AudioBuffer<float>& output, int startSample, int numSamples,
                                const PlaybackParams& params) noexcept
{
    if (sample == nullptr || numSamples <= 0)
        return;

    // How far to move through the sample per output frame:
    //   - file rate / host rate converts sample rates (a 44.1 kHz file on a 48 kHz
    //     host advances ~0.92 frames per output frame, so it plays at the right speed),
    //   - 2^(semitones / 12) applies the Pitch knob (+12 st doubles the speed).
    const auto increment = (sample->sampleRate / hostRate) * std::pow (2.0, (double) params.pitchSemitones / 12.0);

    // Re-apply envelope times every block so turning Attack/Release affects playing notes too.
    const juce::ADSR::Parameters envelope { params.attackMs / 1000.0f, 0.0f, 1.0f, params.releaseMs / 1000.0f };

    for (auto& v : voices)
    {
        if (! v.active)
            continue;

        v.envelope.setParameters (envelope);
        renderVoice (v, output, startSample, numSamples, increment, params.gainLinear);
    }
}

void SliceEngine::renderVoice (Voice& v, juce::AudioBuffer<float>& output, int startSample, int numSamples,
                               double increment, float gain) noexcept
{
    const auto& audio = sample->audio;
    const auto sourceChannels = audio.getNumChannels();
    const auto sourceLength = (int64_t) audio.getNumSamples();

    // Nothing to read from or nowhere to write to: retire the voice.
    if (sourceChannels == 0 || sourceLength == 0 || output.getNumChannels() == 0)
    {
        v.active = false;
        return;
    }

    // Source channels. A mono file reads channel 0 for both left and right.
    const auto* srcL = audio.getReadPointer (0);
    const auto* srcR = audio.getReadPointer (sourceChannels > 1 ? 1 : 0);

    // Output channels, offset so that index 0 is the first frame we render.
    // outR is null when the host gave us a mono output.
    auto* outL = output.getWritePointer (0, startSample);
    auto* outR = output.getNumChannels() > 1 ? output.getWritePointer (1, startSample) : nullptr;

    // Short fade at the slice boundary so cut points never click.
    // Normally 1.5 ms, but never more than a quarter of a (very short) slice.
    const auto sliceLength = (double) (v.sliceEnd - v.sliceStart);
    const auto endFade = juce::jmax (1.0, juce::jmin (sample->sampleRate * endFadeMs / 1000.0, sliceLength * 0.25));
    const auto direction = v.reverse ? -1.0 : 1.0;

    for (int i = 0; i < numSamples; ++i)
    {
        // Source frames left before the voice reaches the end of its slice
        // (the slice START when playing in reverse).
        const auto remaining = v.reverse ? (v.position - (double) v.sliceStart + 1.0)
                                         : ((double) v.sliceEnd - v.position);

        if (remaining <= 0.0)
        {
            v.active = false; // reached the end of the slice
            break;
        }

        const auto env = v.envelope.getNextSample();

        if (! v.envelope.isActive())
        {
            v.active = false; // release finished (gate mode)
            break;
        }

        // Combine all the gain stages for this frame:
        // output level x note velocity x envelope x end-of-slice fade x choke fade.
        auto g = gain * v.velocityGain * env;

        if (remaining < endFade)
            g *= (float) (remaining / endFade); // linear ramp down to 0 at the slice end

        if (v.chokeRemaining > 0)
        {
            g *= (float) v.chokeRemaining / (float) chokeFadeSamples; // linear ramp down

            if (--v.chokeRemaining == 0)
                v.active = false; // fade finished: free the voice after this frame
        }

        // Read the source at the (fractional) position. Mono sources reuse the left value.
        const auto l = readInterpolated (srcL, sourceLength, v.position);
        const auto r = sourceChannels > 1 ? readInterpolated (srcR, sourceLength, v.position) : l;

        // ADD (+=) into the output so several voices mix together.
        if (outR != nullptr)
        {
            outL[i] += l * g;
            outR[i] += r * g;
        }
        else
        {
            outL[i] += 0.5f * (l + r) * g; // mono output: mix both channels down
        }

        v.position += direction * increment;

        if (! v.active)
            break;
    }
}

void SliceEngine::publishUiState() noexcept
{
    // Build the "which slices are sounding" bit mask and find the newest voice,
    // then store them in atomics the UI thread can read at any time.
    // Relaxed ordering is enough: the UI only needs an approximately current
    // picture for drawing, not exact synchronisation with the audio.
    uint64_t mask[2] { 0, 0 };
    const Voice* newest = nullptr;

    for (const auto& v : voices)
    {
        // Fading-out (choked) voices are not shown as playing.
        if (! v.active || v.chokeRemaining > 0 || v.sliceIndex < 0 || v.sliceIndex >= 128)
            continue;

        mask[v.sliceIndex / 64] |= (uint64_t) 1 << (v.sliceIndex % 64);

        if (newest == nullptr || v.startedAt > newest->startedAt)
            newest = &v;
    }

    soundingMask[0].store (mask[0], std::memory_order_relaxed);
    soundingMask[1].store (mask[1], std::memory_order_relaxed);
    latestPosition.store (newest != nullptr ? newest->position : -1.0, std::memory_order_relaxed);
}

} // namespace sliceable
