#include "SliceEngine.h"

#include <cmath>

namespace sliceable
{

namespace
{
    // 4-point, 3rd-order Hermite interpolation. Indices outside the buffer are clamped.
    inline float readInterpolated (const float* data, int64_t length, double position) noexcept
    {
        const auto index = (int64_t) std::floor (position);
        const auto t = (float) (position - (double) index);

        const auto at = [data, length] (int64_t i) noexcept
        {
            return data[i < 0 ? 0 : (i >= length ? length - 1 : i)];
        };

        const auto xm1 = at (index - 1);
        const auto x0  = at (index);
        const auto x1  = at (index + 1);
        const auto x2  = at (index + 2);

        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);

        return ((c3 * t + c2) * t + c1) * t + x0;
    }

    constexpr double endFadeMs = 1.5;
    constexpr double chokeFadeMs = 3.0;
}

void SliceEngine::prepare (double hostSampleRate)
{
    hostRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    chokeFadeSamples = juce::jmax (1, (int) std::round (hostRate * chokeFadeMs / 1000.0));

    for (auto& v : voices)
        v.envelope.setSampleRate (hostRate);

    stopAllVoices();
}

SampleData::Ptr SliceEngine::swapSample (SampleData::Ptr newSample) noexcept
{
    stopAllVoices();
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

    publishUiState();
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

    const auto word = soundingMask[(size_t) (sliceIndex / 64)].load (std::memory_order_relaxed);
    return ((word >> (sliceIndex % 64)) & 1u) != 0;
}

void SliceEngine::process (juce::AudioBuffer<float>& output,
                           const juce::MidiBuffer& midi,
                           const SliceLayout& layout,
                           int rootNote,
                           const PlaybackParams& params) noexcept
{
    const auto numSamples = output.getNumSamples();
    int cursor = 0;

    for (const auto metadata : midi)
    {
        const auto eventPos = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (eventPos > cursor)
        {
            renderVoices (output, cursor, eventPos - cursor, params);
            cursor = eventPos;
        }

        const auto message = metadata.getMessage();

        if (message.isNoteOn())
            startVoice (message.getNoteNumber(), message.getFloatVelocity(), layout, rootNote, params);
        else if (message.isNoteOff())
            stopNote (message.getNoteNumber(), params);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            for (auto& v : voices)
                if (v.active)
                    chokeVoice (v);
    }

    if (cursor < numSamples)
        renderVoices (output, cursor, numSamples - cursor, params);

    publishUiState();
}

void SliceEngine::startVoice (int note, float velocity, const SliceLayout& layout, int rootNote,
                              const PlaybackParams& params) noexcept
{
    if (sample == nullptr)
        return;

    const auto sliceIndex = note - rootNote;

    if (sliceIndex < 0 || sliceIndex >= layout.numSlices)
        return;

    const auto& slice = layout.slices[(size_t) sliceIndex];

    if (slice.length() <= 0)
        return;

    // Retriggering a note cuts its previous instance; mono mode cuts everything (like a choke group).
    for (auto& v : voices)
        if (v.active && v.chokeRemaining == 0 && (params.voiceMode == VoiceMode::mono || v.note == note))
            chokeVoice (v);

    auto& v = findFreeVoice();
    v.active = true;
    v.note = note;
    v.sliceIndex = sliceIndex;
    v.sliceStart = slice.start;
    v.sliceEnd = slice.end;
    v.reverse = params.reverse;
    v.position = v.reverse ? (double) (slice.end - 1) : (double) slice.start;
    v.velocityGain = velocity;
    v.startedAt = ++voiceCounter;
    v.chokeRemaining = 0;

    v.envelope.reset();
    v.envelope.setParameters ({ params.attackMs / 1000.0f, 0.0f, 1.0f, params.releaseMs / 1000.0f });
    v.envelope.noteOn();
}

void SliceEngine::stopNote (int note, const PlaybackParams& params) noexcept
{
    if (params.playMode != PlayMode::gate)
        return; // one-shot slices always play to their end

    for (auto& v : voices)
        if (v.active && v.note == note && v.chokeRemaining == 0)
            v.envelope.noteOff();
}

void SliceEngine::chokeVoice (Voice& v) noexcept
{
    if (v.chokeRemaining == 0)
        v.chokeRemaining = chokeFadeSamples;
}

SliceEngine::Voice& SliceEngine::findFreeVoice() noexcept
{
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

    const auto increment = (sample->sampleRate / hostRate) * std::pow (2.0, (double) params.pitchSemitones / 12.0);
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

    if (sourceChannels == 0 || sourceLength == 0 || output.getNumChannels() == 0)
    {
        v.active = false;
        return;
    }

    const auto* srcL = audio.getReadPointer (0);
    const auto* srcR = audio.getReadPointer (sourceChannels > 1 ? 1 : 0);

    auto* outL = output.getWritePointer (0, startSample);
    auto* outR = output.getNumChannels() > 1 ? output.getWritePointer (1, startSample) : nullptr;

    // Short fade at the slice boundary so cut points never click.
    const auto sliceLength = (double) (v.sliceEnd - v.sliceStart);
    const auto endFade = juce::jmax (1.0, juce::jmin (sample->sampleRate * endFadeMs / 1000.0, sliceLength * 0.25));
    const auto direction = v.reverse ? -1.0 : 1.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto remaining = v.reverse ? (v.position - (double) v.sliceStart + 1.0)
                                         : ((double) v.sliceEnd - v.position);

        if (remaining <= 0.0)
        {
            v.active = false;
            break;
        }

        const auto env = v.envelope.getNextSample();

        if (! v.envelope.isActive())
        {
            v.active = false;
            break;
        }

        auto g = gain * v.velocityGain * env;

        if (remaining < endFade)
            g *= (float) (remaining / endFade);

        if (v.chokeRemaining > 0)
        {
            g *= (float) v.chokeRemaining / (float) chokeFadeSamples;

            if (--v.chokeRemaining == 0)
                v.active = false;
        }

        const auto l = readInterpolated (srcL, sourceLength, v.position);
        const auto r = sourceChannels > 1 ? readInterpolated (srcR, sourceLength, v.position) : l;

        if (outR != nullptr)
        {
            outL[i] += l * g;
            outR[i] += r * g;
        }
        else
        {
            outL[i] += 0.5f * (l + r) * g;
        }

        v.position += direction * increment;

        if (! v.active)
            break;
    }
}

void SliceEngine::publishUiState() noexcept
{
    uint64_t mask[2] { 0, 0 };
    const Voice* newest = nullptr;

    for (const auto& v : voices)
    {
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
