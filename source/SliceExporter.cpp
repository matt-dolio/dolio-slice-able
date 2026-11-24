#include "SliceExporter.h"

namespace sliceable::SliceExporter
{

juce::String fileNameForSlice (const SampleData& sample, int sliceIndex, int rootNote)
{
    auto base = sample.getName();

    if (base.isEmpty())
        base = "Sample";

    const auto note = rootNote + sliceIndex;
    const auto noteName = juce::isPositiveAndBelow (note, 128)
                            ? juce::MidiMessage::getMidiNoteName (note, true, true, 3).replace ("#", "s")
                            : juce::String ("unmapped");

    return juce::File::createLegalFileName (base + "_" + juce::String (sliceIndex + 1).paddedLeft ('0', 2)
                                            + "_" + noteName + ".wav");
}

juce::String writeSlice (const SampleData& sample, Slice slice, const juce::File& destination, int bitsPerSample)
{
    const auto total = sample.getLengthInSamples();
    slice.start = juce::jlimit<int64_t> (0, total, slice.start);
    slice.end = juce::jlimit<int64_t> (slice.start, total, slice.end);

    if (slice.length() <= 0)
        return "Slice is empty";

    if (! destination.getParentDirectory().createDirectory())
        return "Can't create folder " + destination.getParentDirectory().getFullPathName();

    destination.deleteFile();

    std::unique_ptr<juce::OutputStream> stream = destination.createOutputStream();

    if (stream == nullptr)
        return "Can't write to " + destination.getFullPathName();

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (sample.sampleRate)
                             .withNumChannels (sample.audio.getNumChannels())
                             .withBitsPerSample (bitsPerSample);

    auto writer = wav.createWriterFor (stream, options);

    if (writer == nullptr)
        return "WAV writer could not be created for " + destination.getFileName();

    if (! writer->writeFromAudioSampleBuffer (sample.audio, (int) slice.start, (int) slice.length()))
        return "Failed writing " + destination.getFileName();

    return {};
}

juce::Array<juce::File> writeAllSlices (const SampleData& sample, const SliceLayout& layout, int rootNote,
                                        const juce::File& folder, juce::String& error)
{
    juce::Array<juce::File> written;
    error.clear();

    for (int i = 0; i < layout.numSlices; ++i)
    {
        const auto file = folder.getChildFile (fileNameForSlice (sample, i, rootNote));
        const auto result = writeSlice (sample, layout.slices[(size_t) i], file);

        if (result.isNotEmpty())
        {
            error = result;
            break;
        }

        written.add (file);
    }

    return written;
}

juce::File getDragExportFolder (const SampleData& sample)
{
    auto name = juce::File::createLegalFileName (sample.getName());

    if (name.isEmpty())
        name = "Sample";

    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
        .getChildFile ("Slice-able")
        .getChildFile ("Slices")
        .getChildFile (name);
}

} // namespace sliceable::SliceExporter
