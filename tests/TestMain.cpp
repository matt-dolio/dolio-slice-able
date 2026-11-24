// =============================================================================
// TestMain.cpp - unit tests for the slicing maths, playback engine and WAV export.
//
// These test the core logic WITHOUT a DAW or a window: they call SliceGrid,
// SliceEngine and SliceExporter directly with synthetic audio and check the
// results. Run them after any change to those files:
//
//     cmake --build build --target SliceAbleTests
//     ctest --test-dir build --output-on-failure
//
// It's a plain executable that returns non-zero if any check fails (which is
// how CTest and the CI workflow know the tests failed). Failures are printed
// with the test name and line number.
//
// HOW TO ADD A TEST
//   Write a block anywhere below, inside the anonymous namespace:
//
//       TEST (myNewBehaviourWorks)
//       {
//           SliceLayout layout;
//           computeSlices (grid (120.0, "1/8"), 48000, 48000.0, layout);
//           CHECK_EQ (layout.numSlices, 4);
//       }
//
//   TEST registers it automatically; main() runs every registered test.
//   CHECK (condition) and CHECK_EQ (a, b) record a failure but keep going.
//
// Most engine tests use a sample rate of 1000 Hz so that 1 frame = 1 ms, which
// makes the expected numbers easy to read.
// =============================================================================

#include "SliceEngine.h"
#include "SliceExporter.h"
#include "SliceGrid.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace sliceable;

// Several checks compare exact sample values on purpose (e.g. "silence is exactly 0").
JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")

namespace
{
// ---------------------------------------------------------------------------
// Minimal test harness (no external test framework needed)
// ---------------------------------------------------------------------------

int failures = 0;        // number of failed checks so far
int checks = 0;          // number of checks run so far
std::string currentTest; // name of the running test, for failure messages

// Records one check; prints a message if it failed. Use the CHECK macros below.
void check (bool condition, const std::string& what, int line)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::cerr << "FAIL [" << currentTest << "] line " << line << ": " << what << "\n";
    }
}

// CHECK (x)       - fails if x is false; prints the expression's source text.
// CHECK_EQ (a, b) - fails if a != b; also prints both values (numbers only).
#define CHECK(cond) check ((cond), #cond, __LINE__)
#define CHECK_EQ(a, b) check ((a) == (b), std::string (#a " == " #b " (got ") + std::to_string (a) + " vs " + std::to_string (b) + ")", __LINE__)

// Every TEST adds itself to this list (via a static Register object) before main() runs.
struct TestCase
{
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Register
{
    Register (std::string name, std::function<void()> body) { registry().push_back ({ std::move (name), std::move (body) }); }
};

// TEST (name) { ... } declares a test function and registers it under its name.
#define TEST(name) \
    static void name(); \
    static Register name##_registration (#name, name); \
    static void name()

// ---------------------------------------------------------------------------
// Helpers shared by the tests
// ---------------------------------------------------------------------------

// Index of a division by its menu name, e.g. "1/16" -> 12. -1 if not found.
// Lets tests say grid (120.0, "1/16") instead of relying on table positions.
int divisionIndex (std::string_view name)
{
    for (size_t i = 0; i < gridDivisions.size(); ++i)
        if (gridDivisions[i].name == name)
            return (int) i;

    return -1;
}

// Builds GridSettings for a tempo and division (offset in frames, default 0).
GridSettings grid (double bpm, std::string_view division, double offset = 0.0)
{
    GridSettings s;
    s.bpm = bpm;
    s.divisionIndex = divisionIndex (division);
    s.offsetSamples = offset;
    s.minSliceSamples = 1.0;
    return s;
}

// True if the slices start at 0, follow each other with no gaps or overlaps,
// and end at `length` (or stop early because of the 128-slice cap).
bool contiguous (const SliceLayout& layout, int64_t length)
{
    if (layout.numSlices == 0)
        return length == 0;

    if (layout.slices[0].start != 0)
        return false;

    for (int i = 1; i < layout.numSlices; ++i)
        if (layout.slices[(size_t) i].start != layout.slices[(size_t) i - 1].end)
            return false;

    return layout.numUnmapped > 0 || layout.slices[(size_t) layout.numSlices - 1].end == length;
}

// A sample whose value rises steadily from 0.25 (frame 0) towards 0.75 (last frame).
// Because every frame has a different value, a test can tell exactly which frame
// the engine played just by looking at the output value.
SampleData::Ptr rampSample (int length, double sampleRate, int channels = 1)
{
    juce::AudioBuffer<float> buffer (channels, length);

    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < length; ++i)
            buffer.setSample (ch, i, 0.25f + 0.5f * (float) i / (float) length);

    return SampleData::fromBuffer (std::move (buffer), sampleRate);
}

// A MIDI buffer holding a single note-on at `position` (frames into the block).
juce::MidiBuffer noteOn (int note, int position = 0, float velocity = 1.0f)
{
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), position);
    return midi;
}

// Loudest absolute sample value in a buffer (0 = silence).
float peak (const juce::AudioBuffer<float>& b)
{
    return b.getMagnitude (0, b.getNumSamples());
}

// ---------------------------------------------------------------------------
// Grid maths (SliceGrid)
// Most of these use 60 BPM at 1000 Hz, where one quarter note = 1000 frames.
// ---------------------------------------------------------------------------

TEST (sixteenthsAt120BpmSliceOneBarIntoSixteen)
{
    // 1 bar of 4/4 at 120 BPM = 2 seconds.
    const double sr = 48000.0;
    const int64_t length = 96000;
    SliceLayout layout;
    computeSlices (grid (120.0, "1/16"), length, sr, layout);

    CHECK_EQ (layout.numSlices, 16);
    CHECK (contiguous (layout, length));
    CHECK_EQ (layout.slices[0].length(), (int64_t) 6000);
    CHECK_EQ (layout.slices[15].end, length);
    CHECK_EQ (layout.numUnmapped, 0);
}

TEST (stepLengthsForEveryDivision)
{
    GridSettings s;
    s.bpm = 60.0; // one quarter note = one second

    const double sr = 1000.0;
    const std::vector<std::pair<std::string_view, double>> expected {
        { "4 Bars", 16000.0 }, { "2 Bars", 8000.0 }, { "1 Bar", 4000.0 },
        { "1/2", 2000.0 }, { "1/2 Dotted", 3000.0 }, { "1/2 Triplet", 4000.0 / 3.0 },
        { "1/4", 1000.0 }, { "1/4 Dotted", 1500.0 }, { "1/4 Triplet", 2000.0 / 3.0 },
        { "1/8", 500.0 }, { "1/8 Dotted", 750.0 }, { "1/8 Triplet", 1000.0 / 3.0 },
        { "1/16", 250.0 }, { "1/16 Dotted", 375.0 }, { "1/16 Triplet", 1000.0 / 6.0 },
        { "1/32", 125.0 }, { "1/32 Triplet", 1000.0 / 12.0 }, { "1/64", 62.5 },
    };

    CHECK_EQ (expected.size(), gridDivisions.size());

    for (const auto& [name, samples] : expected)
    {
        s.divisionIndex = divisionIndex (name);
        CHECK (s.divisionIndex >= 0);
        CHECK (std::abs (gridStepSamples (s, sr) - samples) < 1e-9);
    }
}

TEST (barsFollowTheTimeSignature)
{
    GridSettings s = grid (60.0, "1 Bar");
    s.timeSignature = { 3, 4 };
    CHECK (std::abs (gridStepSamples (s, 1000.0) - 3000.0) < 1e-9);

    s.timeSignature = { 6, 8 };
    CHECK (std::abs (gridStepSamples (s, 1000.0) - 3000.0) < 1e-9);

    s.timeSignature = { 7, 8 };
    CHECK (std::abs (gridStepSamples (s, 1000.0) - 3500.0) < 1e-9);
}

TEST (partialLastSliceIsKept)
{
    // 2.5 quarter notes long, quarter-note grid -> 3 slices, last one half length.
    SliceLayout layout;
    computeSlices (grid (60.0, "1/4"), 2500, 1000.0, layout);

    CHECK_EQ (layout.numSlices, 3);
    CHECK (contiguous (layout, 2500));
    CHECK_EQ (layout.slices[2].length(), (int64_t) 500);
}

TEST (offsetShiftsGridAndCreatesPickupSlice)
{
    // Grid anchored at 300: lines at 300, 1300, 2300 inside a 3000-frame sample.
    SliceLayout layout;
    computeSlices (grid (60.0, "1/4", 300.0), 3000, 1000.0, layout);

    CHECK_EQ (layout.numSlices, 4);
    CHECK (contiguous (layout, 3000));
    CHECK_EQ (layout.slices[0].end, (int64_t) 300);
    CHECK_EQ (layout.slices[1].end, (int64_t) 1300);
    CHECK_EQ (layout.slices[2].end, (int64_t) 2300);
}

TEST (offsetLargerThanStepWrapsAround)
{
    // The grid extends both ways from the offset, so offsets one or more whole
    // steps apart (300 and 2300 with a 1000-frame step) give the same slices.
    SliceLayout a, b;
    computeSlices (grid (60.0, "1/4", 300.0), 5000, 1000.0, a);
    computeSlices (grid (60.0, "1/4", 2300.0), 5000, 1000.0, b);

    CHECK_EQ (a.numSlices, b.numSlices);

    for (int i = 0; i < a.numSlices; ++i)
        CHECK (a.slices[(size_t) i] == b.slices[(size_t) i]);
}

TEST (sliversAreMergedIntoNeighbours)
{
    auto s = grid (60.0, "1/4", 2.0); // grid line 2 frames in
    s.minSliceSamples = 10.0;

    SliceLayout layout;
    computeSlices (s, 3001, 1000.0, layout); // last line at 3002 is outside; 2002 is the last inside

    CHECK (contiguous (layout, 3001));

    for (const auto& slice : layout)
        CHECK (slice.length() >= 10);

    CHECK_EQ (layout.slices[0].end, (int64_t) 1002); // the 2-frame pickup was absorbed

    // A line 3 frames before the end is dropped too.
    SliceLayout tail;
    computeSlices (s, 2005, 1000.0, tail);
    CHECK_EQ (tail.slices[(size_t) tail.numSlices - 1].end, (int64_t) 2005);
    CHECK (tail.slices[(size_t) tail.numSlices - 1].length() >= 10);
}

TEST (fractionalStepsRoundWithoutDrift)
{
    // Triplet steps are not whole numbers of frames; boundaries must not accumulate error.
    const double sr = 44100.0;
    auto s = grid (97.0, "1/16 Triplet");
    const auto step = gridStepSamples (s, sr);

    SliceLayout layout;
    computeSlices (s, 44100 * 4, sr, layout);
    CHECK (contiguous (layout, 44100 * 4));

    for (int i = 0; i + 1 < layout.numSlices; ++i)
        CHECK_EQ (layout.slices[(size_t) i].end, (int64_t) std::llround ((i + 1) * step));
}

TEST (sliceCountIsCappedAtOneMidiNoteEach)
{
    // 1/64 at 120 BPM for 60 s = 1920 grid steps.
    SliceLayout layout;
    computeSlices (grid (120.0, "1/64"), 48000 * 60, 48000.0, layout);

    CHECK_EQ (layout.numSlices, maxSlices);
    CHECK (layout.numUnmapped > 1700);
    CHECK (contiguous (layout, 48000 * 60));
}

TEST (degenerateInputsAreSafe)
{
    SliceLayout layout;
    computeSlices (grid (120.0, "1/16"), 0, 48000.0, layout);
    CHECK_EQ (layout.numSlices, 0);

    computeSlices (grid (120.0, "1/16"), 1000, 0.0, layout);
    CHECK_EQ (layout.numSlices, 0);

    // Tiny sample: one slice covering all of it.
    computeSlices (grid (120.0, "1/16"), 10, 48000.0, layout);
    CHECK_EQ (layout.numSlices, 1);
    CHECK_EQ (layout.slices[0].end, (int64_t) 10);

    // Silly tempos get clamped rather than producing infinite loops.
    auto s = grid (1.0e9, "1/64");
    computeSlices (s, 48000, 48000.0, layout);
    CHECK (layout.numSlices > 0 && layout.numSlices <= maxSlices);

    s.bpm = std::nan ("");
    computeSlices (s, 48000, 48000.0, layout);
    CHECK (layout.numSlices > 0);

    s = grid (120.0, "1/16");
    s.divisionIndex = 999;
    computeSlices (s, 48000, 48000.0, layout);
    CHECK (layout.numSlices > 0);
}

TEST (fitToBarsGivesTheLoopTempo)
{
    // 2 bars of 4/4 lasting 4 s -> 120 BPM.
    CHECK (std::abs (bpmToFitBars (4 * 44100, 44100.0, 2.0, {}) - 120.0) < 1e-9);

    // 1 bar of 3/4 lasting 2 s -> 90 BPM.
    CHECK (std::abs (bpmToFitBars (2 * 48000, 48000.0, 1.0, { 3, 4 }) - 90.0) < 1e-9);

    // Fitting then slicing by 1/16 gives exactly 16 slices per bar.
    const int64_t length = 123457; // deliberately awkward
    auto s = grid (bpmToFitBars (length, 44100.0, 1.0, {}), "1/16");
    SliceLayout layout;
    computeSlices (s, length, 44100.0, layout);
    CHECK_EQ (layout.numSlices, 16);
    CHECK (contiguous (layout, length));
}

TEST (findSliceAtLocatesSlices)
{
    // 4 slices of 1000 frames; check positions on and around the boundaries.
    SliceLayout layout;
    computeSlices (grid (60.0, "1/4"), 4000, 1000.0, layout);

    CHECK_EQ (findSliceAt (layout, 0), 0);
    CHECK_EQ (findSliceAt (layout, 999), 0);
    CHECK_EQ (findSliceAt (layout, 1000), 1);
    CHECK_EQ (findSliceAt (layout, 3999), 3);
    CHECK_EQ (findSliceAt (layout, 4000), -1);
    CHECK_EQ (findSliceAt (layout, -5), -1);
}

// ---------------------------------------------------------------------------
// Playback engine (SliceEngine)
// ---------------------------------------------------------------------------

// Common setup for engine tests: a 4000-frame ramp sample at 1000 Hz, cut into
// 4 quarter-note slices of 1000 frames each, root note C1 (36), and no attack /
// release so output values can be compared exactly with the source.
struct EngineFixture
{
    SliceEngine engine;
    SliceLayout layout;
    PlaybackParams params;
    int root = 36;

    explicit EngineFixture (int length = 4000, double sampleRate = 1000.0)
    {
        engine.prepare (sampleRate);
        engine.swapSample (rampSample (length, sampleRate));
        computeSlices (grid (60.0, "1/4"), length, sampleRate, layout); // 4 slices of 1000 frames
        params.attackMs = 0.0f;
        params.releaseMs = 0.0f;
    }

    // Runs one block of `numSamples` frames through the engine and returns the output.
    juce::AudioBuffer<float> render (const juce::MidiBuffer& midi, int numSamples)
    {
        juce::AudioBuffer<float> out (2, numSamples);
        out.clear();
        engine.process (out, midi, layout, root, params);
        return out;
    }
};

TEST (noteMapsToSliceAndPlaysItsAudio)
{
    EngineFixture f;
    auto out = f.render (noteOn (f.root + 2), 100); // slice 2 starts at frame 2000

    const auto expectedFirst = 0.25f + 0.5f * 2000.0f / 4000.0f;
    CHECK (std::abs (out.getSample (0, 0) - expectedFirst) < 1e-4f);
    CHECK (std::abs (out.getSample (0, 50) - (0.25f + 0.5f * 2050.0f / 4000.0f)) < 1e-4f);
    CHECK (std::abs (out.getSample (1, 50) - out.getSample (0, 50)) < 1e-6f); // mono -> both channels
    CHECK (f.engine.isSliceSounding (2));
    CHECK (! f.engine.isSliceSounding (1));
}

TEST (notesOutsideTheSliceRangeAreIgnored)
{
    // One key below the root, and one key past the last (4th) slice.
    EngineFixture f;
    CHECK (peak (f.render (noteOn (f.root - 1), 100)) == 0.0f);
    CHECK (peak (f.render (noteOn (f.root + 4), 100)) == 0.0f);
    CHECK_EQ (f.engine.getNumActiveVoices(), 0);
}

TEST (oneShotStopsAtSliceEndAndIgnoresNoteOff)
{
    EngineFixture f;
    juce::MidiBuffer midi = noteOn (f.root);
    midi.addEvent (juce::MidiMessage::noteOff (1, f.root), 10);

    auto out = f.render (midi, 1200);

    CHECK (std::abs (out.getSample (0, 500)) > 0.2f);  // still playing after note-off
    CHECK (out.getSample (0, 1000) == 0.0f);           // nothing from slice 1 leaks in
    CHECK (out.getSample (0, 999) < 0.8f * out.getSample (0, 997)); // end-of-slice fade
    CHECK_EQ (f.engine.getNumActiveVoices(), 0);
}

TEST (gateModeReleasesOnNoteOff)
{
    EngineFixture f;
    f.params.playMode = PlayMode::gate;
    f.params.releaseMs = 10.0f; // 10 frames at 1 kHz

    juce::MidiBuffer midi = noteOn (f.root);
    midi.addEvent (juce::MidiMessage::noteOff (1, f.root), 100);

    auto out = f.render (midi, 400);

    CHECK (std::abs (out.getSample (0, 99)) > 0.2f);
    CHECK (out.getSample (0, 200) == 0.0f);
    CHECK_EQ (f.engine.getNumActiveVoices(), 0);
}

TEST (monoModeChokesPreviousSlice)
{
    // A second note 100 frames later should cut the first; after the 3 ms choke
    // fade (3 frames here) only the second voice is left.
    EngineFixture f;
    f.params.voiceMode = VoiceMode::mono;

    juce::MidiBuffer midi = noteOn (f.root);
    midi.addEvent (juce::MidiMessage::noteOn (1, f.root + 1, 1.0f), 100);
    f.render (midi, 300);

    CHECK_EQ (f.engine.getNumActiveVoices(), 1);
    CHECK (f.engine.isSliceSounding (1));
    CHECK (! f.engine.isSliceSounding (0));
}

TEST (polyModeLayersSlices)
{
    // Same as above but in the default Poly mode: both slices keep playing.
    EngineFixture f;

    juce::MidiBuffer midi = noteOn (f.root);
    midi.addEvent (juce::MidiMessage::noteOn (1, f.root + 1, 1.0f), 100);
    f.render (midi, 300);

    CHECK_EQ (f.engine.getNumActiveVoices(), 2);
}

TEST (retriggeringTheSameNoteCutsTheOldInstance)
{
    // Even in Poly mode, hitting the same key twice doesn't stack two copies.
    EngineFixture f;

    juce::MidiBuffer midi = noteOn (f.root);
    midi.addEvent (juce::MidiMessage::noteOn (1, f.root, 1.0f), 100);
    f.render (midi, 300);

    CHECK_EQ (f.engine.getNumActiveVoices(), 1);
}

TEST (velocityScalesOutput)
{
    EngineFixture loud, soft;
    auto a = loud.render (noteOn (loud.root, 0, 1.0f), 50);
    auto b = soft.render (noteOn (soft.root, 0, 0.5f), 50);
    // MIDI velocity is 7-bit, so 0.5 arrives as 64/127.
    CHECK (std::abs (b.getSample (0, 20) / a.getSample (0, 20) - 64.0f / 127.0f) < 1e-4f);
}

TEST (pitchAndSampleRateChangePlaybackSpeed)
{
    // Sample recorded at 2 kHz played on a 1 kHz host -> advances 2 frames per output sample.
    SliceEngine engine;
    engine.prepare (1000.0);
    engine.swapSample (rampSample (4000, 2000.0));

    SliceLayout layout;
    computeSlices (grid (60.0, "1 Bar"), 4000, 2000.0, layout); // 1 bar = 8000 frames -> one slice

    PlaybackParams params;
    params.attackMs = 0.0f;
    juce::AudioBuffer<float> out (2, 100);
    out.clear();
    engine.process (out, noteOn (36), layout, 36, params);
    CHECK (std::abs (out.getSample (0, 10) - (0.25f + 0.5f * 20.0f / 4000.0f)) < 1e-4f);

    // +12 semitones doubles it again.
    engine.stopAllVoices();
    params.pitchSemitones = 12.0f;
    out.clear();
    engine.process (out, noteOn (36), layout, 36, params);
    CHECK (std::abs (out.getSample (0, 10) - (0.25f + 0.5f * 40.0f / 4000.0f)) < 1e-4f);
}

TEST (reversePlaysSliceBackwards)
{
    EngineFixture f;
    f.params.reverse = true;
    auto out = f.render (noteOn (f.root + 1), 50); // slice 1 = frames 1000..1999

    CHECK (std::abs (out.getSample (0, 0) - (0.25f + 0.5f * 1999.0f / 4000.0f)) < 1e-4f);
    CHECK (out.getSample (0, 10) < out.getSample (0, 0));
}

TEST (midiEventsAreSampleAccurate)
{
    // A note at frame 37 of the block must start exactly on frame 37.
    EngineFixture f;
    auto out = f.render (noteOn (f.root, 37), 100);

    CHECK (out.getSample (0, 36) == 0.0f);
    CHECK (out.getSample (0, 37) > 0.2f);
}

TEST (swappingSampleSilencesVoices)
{
    // Loading a new sample stops everything and hands back the old sample
    // (so the caller can free it off the audio thread).
    EngineFixture f;
    f.render (noteOn (f.root), 10);
    CHECK_EQ (f.engine.getNumActiveVoices(), 1);

    auto old = f.engine.swapSample (rampSample (100, 1000.0));
    CHECK (old != nullptr);
    CHECK_EQ (f.engine.getNumActiveVoices(), 0);
    CHECK (f.engine.getLatestVoicePosition() < 0.0);
}

TEST (allNotesOffFadesEverythingOut)
{
    // Hosts send "all notes off" on transport stop; even one-shot voices must stop.
    EngineFixture f;
    f.render (noteOn (f.root), 10);

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    f.render (midi, 100);

    CHECK_EQ (f.engine.getNumActiveVoices(), 0);
}

TEST (voiceStealingNeverExceedsTheVoiceLimit)
{
    // 100 notes in one block, far more than maxVoices: old voices get stolen
    // rather than the engine running out of voices or overrunning its array.
    EngineFixture f (200000, 48000.0);
    computeSlices (grid (120.0, "1/64"), 200000, 48000.0, f.layout);

    juce::MidiBuffer midi;
    for (int i = 0; i < 100; ++i)
        midi.addEvent (juce::MidiMessage::noteOn (1, f.root + (i % 80), 1.0f), i);

    f.render (midi, 256);
    CHECK (f.engine.getNumActiveVoices() <= SliceEngine::maxVoices);
}

// ---------------------------------------------------------------------------
// Export and loading (SliceExporter, SampleData)
// ---------------------------------------------------------------------------

TEST (exportWritesOneWavPerSlice)
{
    // Writes 4 stereo slices to a temporary folder, checks the file names, then
    // reads one back to confirm its length, format and audio content.
    auto sample = rampSample (4000, 1000.0, 2);
    sample->file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Drum Loop.wav");

    SliceLayout layout;
    computeSlices (grid (60.0, "1/4"), 4000, 1000.0, layout);

    auto folder = juce::File::createTempFile ("sliceable-export-test");
    juce::String error;
    const auto files = SliceExporter::writeAllSlices (*sample, layout, 36, folder, error);

    CHECK (error.isEmpty());
    CHECK_EQ (files.size(), 4);

    if (files.size() == 4)
    {
        CHECK (files[0].getFileName() == "Drum Loop_01_C1.wav");
        CHECK (files[3].getFileName() == "Drum Loop_04_Ds1.wav");

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (files[2]));
        CHECK (reader != nullptr);

        if (reader != nullptr)
        {
            CHECK_EQ ((int64_t) reader->lengthInSamples, (int64_t) 1000);
            CHECK_EQ ((int) reader->numChannels, 2);
            CHECK (std::abs (reader->sampleRate - 1000.0) < 1e-9);

            juce::AudioBuffer<float> data (2, 1000);
            reader->read (&data, 0, 1000, 0, true, true);
            CHECK (std::abs (data.getSample (0, 0) - sample->audio.getSample (0, 2000)) < 1e-5f);
        }
    }

    folder.deleteRecursively();
}

TEST (loadRejectsMissingFiles)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto missing = juce::File::getSpecialLocation (juce::File::tempDirectory)
                             .getChildFile ("sliceable-definitely-missing").getChildFile ("here.wav");
    const auto result = SampleData::load (missing, formats);
    CHECK (result.sample == nullptr);
    CHECK (result.error.isNotEmpty());
}

} // namespace

// Runs every registered test in the order they appear in this file, then
// prints a summary. Exit code 0 = all passed, 1 = at least one failure.
int main()
{
    for (const auto& test : registry())
    {
        currentTest = test.name;
        test.body();
    }

    std::cout << registry().size() << " tests, " << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}

JUCE_END_IGNORE_WARNINGS_GCC_LIKE
