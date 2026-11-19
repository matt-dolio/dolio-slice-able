#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace sliceable
{

// An immutable, decoded audio file. Shared between the message thread (UI, export)
// and the audio thread (playback) via reference counting.
struct SampleData final : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::AudioBuffer<float> audio; // 1 or 2 channels
    double sampleRate = 44100.0;
    juce::File file;

    int64_t getLengthInSamples() const noexcept { return audio.getNumSamples(); }
    double getLengthInSeconds() const noexcept  { return audio.getNumSamples() / sampleRate; }
    juce::String getName() const                { return file.getFileNameWithoutExtension(); }

    struct LoadResult
    {
        Ptr sample;
        juce::String error;
    };

    static constexpr double maxLengthSeconds = 15.0 * 60.0;

    static LoadResult load (const juce::File& file, juce::AudioFormatManager& formats);

    // For tests and programmatic use.
    static Ptr fromBuffer (juce::AudioBuffer<float> buffer, double sampleRate, juce::File file = {});
};

// File extensions the plug-in will try to open (for drag & drop filtering).
bool isSupportedAudioFile (const juce::File& file, juce::AudioFormatManager& formats);

} // namespace sliceable
