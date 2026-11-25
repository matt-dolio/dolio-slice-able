// =============================================================================
// SampleData.cpp - decoding audio files into memory. See SampleData.h.
// =============================================================================

#include "SampleData.h"

namespace sliceable
{

SampleData::LoadResult SampleData::load (const juce::File& file, juce::AudioFormatManager& formats)
{
    // Each check below returns a message meant to be shown to the user as-is,
    // so keep them short and specific.
    if (! file.existsAsFile())
        return { nullptr, "File not found: " + file.getFullPathName() };

    // Ask every registered decoder whether it can read this file.
    // (createReaderFor returns a raw pointer that we own, so wrap it at once.)
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return { nullptr, "Unsupported or unreadable audio file: " + file.getFileName() };

    if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return { nullptr, "The file contains no audio: " + file.getFileName() };

    if ((double) reader->lengthInSamples / reader->sampleRate > maxLengthSeconds)
        return { nullptr, "File is longer than 15 minutes: " + file.getFileName() };

    // Keep at most two channels. A surround file is loaded as its first two
    // channels (front left / right), which the playback engine treats as stereo.
    const auto numChannels = (int) juce::jlimit (1u, 2u, reader->numChannels);

    // AudioBuffer sizes are ints. The 15-minute cap above keeps us far below
    // INT_MAX frames even at 192 kHz, so this cast is safe.
    const auto numSamples = (int) reader->lengthInSamples;

    Ptr sample = new SampleData();
    sample->audio.setSize (numChannels, numSamples);
    sample->sampleRate = reader->sampleRate;
    sample->file = file;

    // Decode the whole file into the buffer in one go.
    // The last two arguments say which source channels to use: always the left,
    // plus the right one only when the file has two or more channels.
    if (! reader->read (&sample->audio, 0, numSamples, 0, true, numChannels > 1))
        return { nullptr, "Failed to read audio from " + file.getFileName() };

    return { sample, {} };
}

SampleData::Ptr SampleData::fromBuffer (juce::AudioBuffer<float> buffer, double sampleRate, juce::File file)
{
    Ptr sample = new SampleData();
    sample->audio = std::move (buffer); // take ownership without copying the audio
    sample->sampleRate = sampleRate;
    sample->file = std::move (file);
    return sample;
}

bool isSupportedAudioFile (const juce::File& file, juce::AudioFormatManager& formats)
{
    // Extension check only (cheap, no file I/O). A file with the right extension
    // can still fail to decode; SampleData::load reports that case.
    return formats.findFormatForFileExtension (file.getFileExtension()) != nullptr;
}

} // namespace sliceable
