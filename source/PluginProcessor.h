#pragma once

#include "Parameters.h"
#include "SampleData.h"
#include "SliceEngine.h"
#include "SliceGrid.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace sliceable
{

// What the grid is currently derived from, for display.
struct GridInfo
{
    GridSettings settings;
    TempoMode tempoMode = TempoMode::host;
    bool hostTempoAvailable = false;
};

class SliceAbleProcessor final : public juce::AudioProcessor,
                                 public juce::ChangeBroadcaster, // sample loaded / cleared
                                 private juce::Timer
{
public:
    SliceAbleProcessor();
    ~SliceAbleProcessor() override;

    // --- AudioProcessor ---
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- Sample management (message thread) ---
    juce::String loadSample (const juce::File&); // returns an error, or empty on success
    void clearSample();
    SampleData::Ptr getSample() const;
    juce::String getMissingSamplePath() const;   // set when a saved session's sample can't be found
    juce::AudioFormatManager& getFormatManager() { return formatManager; }

    // --- Grid (any thread except audio; the audio thread keeps its own copy) ---
    GridInfo getGridInfo (const SampleData* sample) const;
    void computeLayout (const SampleData* sample, SliceLayout& out) const;
    int getRootNote() const;

    juce::AudioProcessorValueTreeState& getState() { return parameters; }
    const SliceEngine& getEngine() const { return engine; }

    // Notes played from the on-screen keyboard and waveform clicks.
    juce::MidiKeyboardState keyboardState;

private:
    juce::AudioProcessorEditor* createEditor() override;
    void timerCallback() override; // releases retired samples off the audio thread

    GridSettings buildGridSettings (const SampleData& sample, double bpm, TimeSignature ts) const;
    PlaybackParams readPlaybackParams() const;
    void handOverToAudioThread (SampleData::Ptr);

    juce::AudioProcessorValueTreeState parameters;
    juce::AudioFormatManager formatManager;

    // Raw parameter pointers (read on the audio thread)
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

    // Host transport info, published by the audio thread
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> hostBpmValid { false };
    std::atomic<int> hostNumerator { 4 };
    std::atomic<int> hostDenominator { 4 };

    // Sample hand-off between message thread and audio thread
    mutable juce::CriticalSection sampleLock; // guards messageThreadSample / missingSamplePath
    SampleData::Ptr messageThreadSample;
    juce::String missingSamplePath;

    juce::SpinLock handOffLock;
    SampleData::Ptr pendingSample;           // new sample waiting for the audio thread, or the retired one
    bool pendingIsNew = false;

    // Audio-thread state
    SliceEngine engine;
    SliceLayout audioLayout;
    GridSettings audioLayoutSettings;
    int64_t audioLayoutLength = -1;
    juce::SmoothedValue<float> outputGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceAbleProcessor)
};

} // namespace sliceable
