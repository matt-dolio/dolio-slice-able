#pragma once

#include "SampleData.h"
#include "SliceGrid.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace sliceable
{

enum class PlayMode { oneShot = 0, gate = 1 };
enum class VoiceMode { poly = 0, mono = 1 };

struct PlaybackParams
{
    float attackMs = 0.5f;
    float releaseMs = 15.0f;
    float gainLinear = 1.0f;
    float pitchSemitones = 0.0f;
    PlayMode playMode = PlayMode::oneShot;
    VoiceMode voiceMode = VoiceMode::poly;
    bool reverse = false;
};

// Plays slices of one sample in response to MIDI. Audio-thread only, except where noted.
class SliceEngine
{
public:
    static constexpr int maxVoices = 32;

    void prepare (double hostSampleRate);

    // Swaps in a new sample and silences all voices. Never frees memory, so the
    // previous sample is handed back for the caller to release off the audio thread.
    SampleData::Ptr swapSample (SampleData::Ptr newSample) noexcept;

    const SampleData* getSample() const noexcept { return sample.get(); }

    // Renders voices into `output` (added on top of what's there), consuming note events from `midi`.
    void process (juce::AudioBuffer<float>& output,
                  const juce::MidiBuffer& midi,
                  const SliceLayout& layout,
                  int rootNote,
                  const PlaybackParams& params) noexcept;

    void stopAllVoices() noexcept;

    int getNumActiveVoices() const noexcept;

    // --- Readable from any thread (for the UI) ---
    bool isSliceSounding (int sliceIndex) const noexcept;
    // Play position (in sample frames) of the most recently started voice, or -1 if silent.
    double getLatestVoicePosition() const noexcept { return latestPosition.load (std::memory_order_relaxed); }

private:
    struct Voice
    {
        bool active = false;
        int note = -1;
        int sliceIndex = -1;
        double position = 0.0;
        int64_t sliceStart = 0;
        int64_t sliceEnd = 0;
        float velocityGain = 1.0f;
        uint64_t startedAt = 0;
        juce::ADSR envelope;
        int chokeRemaining = 0; // > 0 while fading out after being choked or retriggered
        bool reverse = false;
    };

    void startVoice (int note, float velocity, const SliceLayout& layout, int rootNote, const PlaybackParams&) noexcept;
    void stopNote (int note, const PlaybackParams&) noexcept;
    void chokeVoice (Voice&) noexcept;
    Voice& findFreeVoice() noexcept;
    void renderVoices (juce::AudioBuffer<float>& output, int startSample, int numSamples, const PlaybackParams&) noexcept;
    void renderVoice (Voice&, juce::AudioBuffer<float>& output, int startSample, int numSamples,
                      double increment, float gain) noexcept;
    void publishUiState() noexcept;

    SampleData::Ptr sample;
    double hostRate = 44100.0;
    int chokeFadeSamples = 64;
    std::array<Voice, maxVoices> voices;
    uint64_t voiceCounter = 0;

    std::array<std::atomic<uint64_t>, 2> soundingMask {};
    std::atomic<double> latestPosition { -1.0 };
};

} // namespace sliceable
