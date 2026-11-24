#pragma once

// =============================================================================
// SliceEngine.h - the sampler: plays slices of the sample in response to MIDI.
//
// How it works, in one paragraph:
//   Every audio block, the processor hands the engine the incoming MIDI, the
//   current slice layout (from SliceGrid) and the knob settings. A note-on for
//   note N starts a "voice" playing slice (N - rootNote). Each voice walks
//   through its slice frame by frame, applies an envelope and a few short fades,
//   and adds its output into the block. When a voice reaches the end of its
//   slice (or is released / cut off), it becomes free for the next note.
//
// Threading:
//   Almost everything here runs on the AUDIO THREAD, which has strict rules:
//   no memory allocation, no locks, no file access, nothing that can block.
//   The code below follows those rules. Only the methods in the "Readable from
//   any thread" section may be called from the UI; they read atomics that the
//   audio thread updates at the end of every block.
//
// Glossary:
//   voice      - one playing instance of a slice. Up to `maxVoices` play at once.
//   envelope   - attack / release volume curve (juce::ADSR), driven by note on/off.
//   choke      - quickly fading a voice out (a few ms) instead of stopping it dead,
//                which would click. Used for retriggers, mono mode and "all notes off".
//   increment  - how many SOURCE frames a voice advances per OUTPUT frame. It
//                combines sample-rate conversion and pitch: 1.0 = original speed.
//
// Common changes:
//   - More simultaneous voices ... maxVoices.
//   - Different fade lengths ..... endFadeMs / chokeFadeMs in SliceEngine.cpp.
//   - Velocity response .......... velocityGain in startVoice() (currently linear).
//   - New playback option ........ add a field to PlaybackParams, fill it in
//                                  SliceAbleProcessor::readPlaybackParams(), use it here.
// =============================================================================

#include "SampleData.h"
#include "SliceGrid.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace sliceable
{

// The numeric values match the indexes of the "Play Mode" and "Voice Mode"
// parameter choices in Parameters.cpp - keep them in sync.
enum class PlayMode
{
    oneShot = 0, // a slice always plays to its end; note-off is ignored (good for drums)
    gate = 1     // a slice plays only while the key is held; note-off starts the release
};

enum class VoiceMode
{
    poly = 0,    // slices can overlap
    mono = 1     // a new slice cuts off whatever is playing (like a hi-hat choke group)
};

/** The knob settings the engine needs, read fresh from the parameters every block. */
struct PlaybackParams
{
    float attackMs = 0.5f;       // fade-in time at note start
    float releaseMs = 15.0f;     // fade-out time after note-off (gate mode only)
    float gainLinear = 1.0f;     // output level as a multiplier (1.0 = unchanged)
    float pitchSemitones = 0.0f; // transposition; +12 = an octave up (and twice as fast)
    PlayMode playMode = PlayMode::oneShot;
    VoiceMode voiceMode = VoiceMode::poly;
    bool reverse = false;        // play each slice from its end back to its start
};

/** Plays slices of one sample in response to MIDI.
    Audio-thread only, except for the methods marked "readable from any thread".
*/
class SliceEngine
{
public:
    // How many slices can sound at once. When all are busy, the oldest is stolen.
    static constexpr int maxVoices = 32;

    /** Call before playback starts and whenever the host's sample rate changes.
        Silences everything.
    */
    void prepare (double hostSampleRate);

    /** Replaces the sample being played and silences all voices.

        Returns the PREVIOUS sample instead of letting it be deleted here, because
        deleting a large buffer frees memory - not allowed on the audio thread. The
        caller keeps the returned pointer and releases it later on another thread
        (see SliceAbleProcessor::processBlock / timerCallback).
    */
    SampleData::Ptr swapSample (SampleData::Ptr newSample) noexcept;

    /** The sample currently being played, or nullptr if none is loaded. */
    const SampleData* getSample() const noexcept { return sample.get(); }

    /** Renders one block of audio.

        @param output    the block to render into. Voices are ADDED to what is
                         already there, so clear it first if you want only the engine.
        @param midi      note events for this block, each with a sample position;
                         notes start exactly at that position (sample-accurate).
        @param layout    the current slices (from computeSlices)
        @param rootNote  MIDI note that plays slice 0
        @param params    knob settings for this block
    */
    void process (juce::AudioBuffer<float>& output,
                  const juce::MidiBuffer& midi,
                  const SliceLayout& layout,
                  int rootNote,
                  const PlaybackParams& params) noexcept;

    /** Stops every voice immediately (no fade). */
    void stopAllVoices() noexcept;

    /** Number of voices currently producing sound (including ones fading out). */
    int getNumActiveVoices() const noexcept;

    // --- Readable from any thread (for the UI) --------------------------------

    /** True if a voice was playing this slice at the end of the last audio block.
        The waveform and keyboard use this to light up playing slices.
    */
    bool isSliceSounding (int sliceIndex) const noexcept;

    /** Play position (in sample frames) of the most recently started voice, or -1
        if nothing is playing. Used to draw the moving playhead on the waveform.
    */
    double getLatestVoicePosition() const noexcept { return latestPosition.load (std::memory_order_relaxed); }

private:
    /** One playing slice. Voices live in a fixed array and are reused, never allocated. */
    struct Voice
    {
        bool active = false;     // false = free to be used by the next note
        int note = -1;           // MIDI note that started it (for note-off / retrigger)
        int sliceIndex = -1;     // which slice it plays (note - rootNote)
        double position = 0.0;  // current read position in the sample, in frames.
                                 // Fractional because of pitch / sample-rate changes.
        int64_t sliceStart = 0;  // copy of the slice bounds taken at note-on, so a
        int64_t sliceEnd = 0;    // tempo change mid-note doesn't move a playing slice
        float velocityGain = 1.0f; // 0..1, from the note's velocity
        uint64_t startedAt = 0;  // increasing counter: larger = started more recently
        juce::ADSR envelope;     // attack/release curve
        int chokeRemaining = 0;  // > 0 while fading out after being choked or retriggered;
                                 // counts down output frames until the voice stops
        bool reverse = false;    // playing backwards (fixed at note-on)
    };

    void startVoice (int note, float velocity, const SliceLayout& layout, int rootNote, const PlaybackParams&) noexcept;
    void stopNote (int note, const PlaybackParams&) noexcept;
    void chokeVoice (Voice&) noexcept;
    Voice& findFreeVoice() noexcept;
    void renderVoices (juce::AudioBuffer<float>& output, int startSample, int numSamples, const PlaybackParams&) noexcept;
    void renderVoice (Voice&, juce::AudioBuffer<float>& output, int startSample, int numSamples,
                      double increment, float gain) noexcept;
    void publishUiState() noexcept;

    SampleData::Ptr sample;      // what we're playing (shared, read-only)
    double hostRate = 44100.0;   // the host's (output) sample rate
    int chokeFadeSamples = 64;   // choke fade length in output frames (set in prepare())
    std::array<Voice, maxVoices> voices;
    uint64_t voiceCounter = 0;   // source of Voice::startedAt values

    // State published for the UI at the end of every block (see publishUiState):
    //   soundingMask - 128 bits, one per slice: bit i set = slice i is playing.
    //                  Stored as two 64-bit atomics because there is no portable
    //                  lock-free 128-bit atomic.
    //   latestPosition - playhead of the newest voice, or -1.
    std::array<std::atomic<uint64_t>, 2> soundingMask {};
    std::atomic<double> latestPosition { -1.0 };
};

} // namespace sliceable
