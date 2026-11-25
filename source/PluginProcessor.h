#pragma once

// =============================================================================
// PluginProcessor.h - START HERE.
//
// This is the plug-in's main object: the host (DAW) creates one
// SliceAbleProcessor per instance of the plug-in, calls processBlock() for every
// chunk of audio, and asks it to save / restore its state with the project.
//
// ----------------------------------------------------------------------------
// HOW THE PIECES FIT TOGETHER
// ----------------------------------------------------------------------------
//
//   Parameters.*        every knob / menu, stored in an APVTS (see that file)
//        |
//        v
//   SliceAbleProcessor  (this file) owns everything below and talks to the host
//        |
//        +-- SampleData.*     the loaded audio file, decoded into memory
//        +-- SliceGrid.*      maths: tempo + division + offset -> slice boundaries
//        +-- SliceEngine.*    the sampler voices that play slices from MIDI
//        +-- SliceExporter.*  writes slices to WAV (export button, drag-out)
//        |
//        +-- SliceAbleEditor (PluginEditor.*)   the window, created on demand
//               +-- WaveformView.*      waveform + slices, click / drag / drop
//               +-- SliceKeyboard       on-screen keyboard (in PluginEditor.*)
//               +-- SliceLookAndFeel.*  colours and custom drawing of controls
//
// What happens when you play a note:
//   1. The host calls processBlock() with a MIDI buffer (plus any notes from the
//      on-screen keyboard, merged in via keyboardState).
//   2. processBlock() reads the host tempo, builds GridSettings from the
//      parameters, and recomputes the slice layout if anything changed.
//   3. SliceEngine::process() maps each note to a slice and renders the voices.
//   4. The output gain is applied and the audio goes back to the host.
//
// ----------------------------------------------------------------------------
// THREADS (the most important thing to understand before changing code)
// ----------------------------------------------------------------------------
//
//   Audio thread    - processBlock() and everything it calls. Runs in real time:
//                     it must never wait, so no memory allocation, no locks that
//                     could block, no file access. Missing a deadline = a click.
//   Message thread  - the UI, file loading, state save/restore, timers. Can do
//                     slow things, but must not touch audio-thread-only data.
//
//   They communicate through:
//     - parameters:    std::atomic<float> values, safe to read anywhere,
//     - std::atomic members below (host tempo, engine's "sounding" flags),
//     - the sample hand-off (handOverToAudioThread / processBlock / timerCallback),
//       which swaps in a new sample without the audio thread ever allocating,
//       freeing memory or waiting for a lock.
//
// Functions below are tagged with the thread they're meant for.
// =============================================================================

#include "Parameters.h"
#include "SampleData.h"
#include "SliceEngine.h"
#include "SliceGrid.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace sliceable
{

/** The current grid, plus where its tempo came from. Used by the UI's info line. */
struct GridInfo
{
    GridSettings settings;                 // what the grid is built from right now
    TempoMode tempoMode = TempoMode::host; // the Tempo Source setting
    bool hostTempoAvailable = false;       // false in e.g. the standalone app, where
                                           // "Host Tempo" falls back to the BPM knob
};

class SliceAbleProcessor final : public juce::AudioProcessor,
                                 public juce::ChangeBroadcaster, // tells the editor "sample loaded / cleared"
                                 private juce::Timer             // periodically frees retired samples
{
public:
    SliceAbleProcessor();
    ~SliceAbleProcessor() override;

    // --- AudioProcessor: called by the host ----------------------------------

    /** [message thread] Playback is about to start; sample rate / block size may have changed. */
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;

    /** [message thread] Playback stopped; release anything prepareToPlay allocated. */
    void releaseResources() override;

    /** Which channel layouts we accept: no inputs (it's an instrument), mono or stereo out. */
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    /** [audio thread] Renders one block of audio from the incoming MIDI. */
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock; // keep the double-precision overload visible

    // Basic facts the host asks about.
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; } // set by PRODUCT_NAME in CMakeLists.txt
    bool acceptsMidi() const override { return true; }   // instruments take MIDI notes
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // "Programs" (presets) are not used; hosts still require these to exist.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    /** [message thread] Saves parameters + sample file path into the project. */
    void getStateInformation (juce::MemoryBlock&) override;

    /** [message thread] Restores what getStateInformation saved, reloading the sample. */
    void setStateInformation (const void*, int) override;

    // --- Sample management [message thread] ----------------------------------

    /** Loads an audio file and starts using it. Returns an error message, or empty on success. */
    juce::String loadSample (const juce::File&);

    /** Unloads the current sample. */
    void clearSample();

    /** The currently loaded sample (for the UI and exporter), or nullptr. */
    SampleData::Ptr getSample() const;

    /** When a saved project refers to a sample file that no longer exists, this
        returns its path (so the UI can say what's missing). Empty otherwise.
    */
    juce::String getMissingSamplePath() const;

    /** The decoders used to open audio files (WAV, AIFF, FLAC, MP3, ...). */
    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    // --- Grid [any thread except audio; the audio thread keeps its own copy] --

    /** The grid as it would be built right now for `sample`, using the latest
        known host tempo. Same rules as processBlock(), so the UI matches what plays.
    */
    GridInfo getGridInfo (const SampleData* sample) const;

    /** Computes the slices for `sample` into `out` (empty if sample is null). */
    void computeLayout (const SampleData* sample, SliceLayout& out) const;

    /** The Root Note parameter: the MIDI note that plays slice 1. */
    int getRootNote() const;

    /** Access to the parameters, for UI attachments. */
    juce::AudioProcessorValueTreeState& getState() { return parameters; }

    /** Read-only access to the engine, for the UI's "what's playing" display. */
    const SliceEngine& getEngine() const { return engine; }

    /** Notes played from the on-screen keyboard and from clicking slices.
        The UI calls noteOn/noteOff on it (thread-safe); processBlock() merges
        those notes into the host's MIDI each block.
    */
    juce::MidiKeyboardState keyboardState;

private:
    juce::AudioProcessorEditor* createEditor() override;
    void timerCallback() override; // releases retired samples off the audio thread

    /** Turns the current parameter values (+ host tempo) into GridSettings for a sample. */
    GridSettings buildGridSettings (const SampleData& sample, double bpm, TimeSignature ts) const;

    /** Collects the Playback parameters into one struct for the engine. */
    PlaybackParams readPlaybackParams() const;

    /** [message thread] Queues a sample for the audio thread to pick up. */
    void handOverToAudioThread (SampleData::Ptr);

    juce::AudioProcessorValueTreeState parameters; // all parameters (see Parameters.h)
    juce::AudioFormatManager formatManager;        // audio file decoders

    // Direct pointers to each parameter's current value. Reading an atomic is
    // lock-free, so these are how the audio thread reads the knobs. They point
    // into `parameters` and are set once in the constructor.
    std::atomic<float>* division = nullptr;
    std::atomic<float>* tempoMode = nullptr;
    std::atomic<float>* manualBpm = nullptr;
    std::atomic<float>* fitBars = nullptr;
    std::atomic<float>* gridOffsetMs = nullptr;
    std::atomic<float>* rootNote = nullptr;
    std::atomic<float>* playMode = nullptr;
    std::atomic<float>* voiceMode = nullptr;
    std::atomic<float>* reverse = nullptr;
    std::atomic<float>* attackMs = nullptr;
    std::atomic<float>* releaseMs = nullptr;
    std::atomic<float>* pitch = nullptr;
    std::atomic<float>* gainDb = nullptr;

    // Host transport info. Only the audio thread can ask the host for its tempo,
    // so processBlock() stores it here for the UI to read.
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> hostBpmValid { false };   // false until the host reports a tempo
    std::atomic<int> hostNumerator { 4 };
    std::atomic<int> hostDenominator { 4 };

    // --- The message thread's view of the sample ---
    // A CriticalSection is fine here because the audio thread never takes it.
    mutable juce::CriticalSection sampleLock; // guards messageThreadSample / missingSamplePath
    SampleData::Ptr messageThreadSample;      // what the UI shows and exports
    juce::String missingSamplePath;

    // --- Hand-off of a new sample to the audio thread ---
    // The message thread puts a new sample in `pendingSample` and sets
    // `pendingIsNew`. At the start of the next block the audio thread swaps it
    // into the engine and leaves the OLD sample in `pendingSample` (pendingIsNew
    // = false). The timer later frees that old sample on the message thread.
    // The audio thread only ever TRY-locks handOffLock, so it never waits.
    juce::SpinLock handOffLock;
    SampleData::Ptr pendingSample;           // new sample waiting for the audio thread, or the retired one
    bool pendingIsNew = false;

    // --- Audio-thread-only state ---
    SliceEngine engine;
    SliceLayout audioLayout;                 // slices the engine is currently playing from
    GridSettings audioLayoutSettings;        // the settings audioLayout was computed with
    int64_t audioLayoutLength = -1;          // sample length it was computed for; -1 forces a recompute
    juce::SmoothedValue<float> outputGain;   // Gain knob, smoothed so moving it doesn't crackle

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceAbleProcessor)
};

} // namespace sliceable
