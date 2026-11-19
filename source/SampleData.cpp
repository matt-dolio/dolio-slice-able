#include "SampleData.h"

namespace sliceable
{

SampleData::LoadResult SampleData::load (const juce::File& file, juce::AudioFormatManager& formats)
{
    if (! file.existsAsFile())
        return { nullptr, "File not found: " + file.getFullPathName() };

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return { nullptr, "Unsupported or unreadable audio file: " + file.getFileName() };

    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return { nullptr, "The file contains no audio: " + file.getFileName() };

    if ((double) reader->lengthInSamples / reader->sampleRate > maxLengthSeconds)
        return { nullptr, "File is longer than 15 minutes: " + file.getFileName() };

    const auto numChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);
    const auto numSamples = (int) reader->lengthInSamples;

    Ptr sample = new SampleData();
    sample->audio.setSize (numChannels, numSamples);
    sample->sampleRate = reader->sampleRate;
    sample->file = file;

    if (! reader->read (&sample->audio, 0, numSamples, 0, true, numChannels > 1))
        return { nullptr, "Failed to read audio from " + file.getFileName() };

    return { sample, {} };
}

SampleData::Ptr SampleData::fromBuffer (juce::AudioBuffer<float> buffer, double sampleRate, juce::File file)
{
    Ptr sample = new SampleData();
    sample->audio = std::move (buffer);
    sample->sampleRate = sampleRate;
    sample->file = std::move (file);
    return sample;
}

bool isSupportedAudioFile (const juce::File& file, juce::AudioFormatManager& formats)
{
    return formats.findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

} // namespace sliceable
