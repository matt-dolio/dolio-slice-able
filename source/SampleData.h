#pragma once

// =============================================================================
// SampleData.h - a loaded (decoded) audio file, held entirely in memory.
//
// When the user loads a sample, the file is decoded once into a float buffer and
// wrapped in a SampleData object. That object is then SHARED, read-only, between:
//   - the audio thread (SliceEngine plays slices from it),
//   - the UI (WaveformView draws it),
//   - the exporter (SliceExporter writes slices from it).
//
// Sharing works through reference counting (SampleData::Ptr): the object stays
// alive as long as anyone holds a Ptr to it. Because nobody modifies a SampleData
// after it is created, no locking is needed to read it from several threads.
// To "change" the sample, a brand-new SampleData is created and swapped in
// (see SliceAbleProcessor::loadSample).
//
// Common changes:
//   - Allow longer files ......... maxLengthSeconds below (memory use grows with it:
//                                  15 min of 48 kHz stereo is about 330 MB of floats).
//   - Support more channels ...... SampleData::load clamps to 2 channels; the engine
//                                  and waveform would also need updating.
//   - Support more file types .... formats are registered in the processor's
//                                  constructor (registerBasicFormats).
// =============================================================================

#include <juce_audio_formats/juce_audio_formats.h>

namespace sliceable
{

/** An immutable, decoded audio file shared between threads via reference counting. */
struct SampleData final : juce::ReferenceCountedObject
{
    // Smart pointer type. Copying a Ptr adds a reference; the SampleData is
    // deleted automatically when the last Ptr to it goes away.
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::AudioBuffer<float> audio; // the decoded audio: 1 (mono) or 2 (stereo) channels
    double sampleRate = 44100.0;    // the FILE's sample rate (may differ from the host's)
    juce::File file;                // where it was loaded from (saved with the session)

    int64_t getLengthInSamples() const noexcept { return audio.getNumSamples(); }
    double getLengthInSeconds() const noexcept  { return audio.getNumSamples() / sampleRate; }

    /** File name without folder or extension, e.g. "Break 120". Used to name exported slices. */
    juce::String getName() const                { return file.getFileNameWithoutExtension(); }

    /** What load() returns: either a sample, or an error message for the user. */
    struct LoadResult
    {
        Ptr sample;          // nullptr if loading failed
        juce::String error;  // empty on success; otherwise a human-readable reason
    };

    // Longest file we agree to load. Protects against accidentally loading a
    // whole album into memory.
    static constexpr double maxLengthSeconds = 15.0 * 60.0;

    /** Decodes an audio file into memory.
        Runs on whichever thread calls it (normally the message thread) and may
        take a moment for long files - never call it from the audio thread.

        @param file     the audio file to read
        @param formats  the set of decoders to try (WAV, AIFF, FLAC, MP3, ...)
    */
    static LoadResult load (const juce::File& file, juce::AudioFormatManager& formats);

    /** Wraps an existing buffer as a SampleData. Used by the unit tests, which
        build synthetic audio instead of reading files.
    */
    static Ptr fromBuffer (juce::AudioBuffer<float> buffer, double sampleRate, juce::File file = {});
};

/** True if one of the registered decoders recognises this file's extension.
    Used to decide whether to accept a file being dragged onto the waveform.
*/
bool isSupportedAudioFile (const juce::File& file, juce::AudioFormatManager& formats);

} // namespace sliceable
