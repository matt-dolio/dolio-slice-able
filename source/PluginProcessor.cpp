// =============================================================================
// PluginProcessor.cpp - the plug-in's main object. Read PluginProcessor.h first:
// it has the overview of how the files fit together and the threading rules.
// =============================================================================

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace sliceable
{

namespace
{
    // Key under which the sample's file path is saved in the plug-in state.
    const juce::Identifier samplePathProperty { "samplePath" };

    // Grid lines closer than this to a neighbouring cut are skipped, so no slice
    // is shorter than 2 ms (see GridSettings::minSliceSamples).
    constexpr double minSliceMs = 2.0;
}

SliceAbleProcessor::SliceAbleProcessor()
    // Declare the audio buses: no input, one stereo output (it's an instrument).
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      // Create all parameters. "SliceAble" is the name of the saved state's root
      // XML element; changing it would make old projects fail to load.
      parameters (*this, nullptr, "SliceAble", createParameterLayout())
{
    // WAV, AIFF, FLAC, Ogg, MP3 (enabled in CMakeLists.txt), plus Core Audio
    // formats on macOS. Add more decoders here if needed.
    formatManager.registerBasicFormats();

    // Cache direct pointers to each parameter's value for fast, lock-free
    // reads on the audio thread (see the member declarations in the header).
    division     = parameters.getRawParameterValue (ParamIDs::division);
    tempoMode    = parameters.getRawParameterValue (ParamIDs::tempoMode);
    manualBpm    = parameters.getRawParameterValue (ParamIDs::manualBpm);
    fitBars      = parameters.getRawParameterValue (ParamIDs::fitBars);
    gridOffsetMs = parameters.getRawParameterValue (ParamIDs::gridOffset);
    rootNote     = parameters.getRawParameterValue (ParamIDs::rootNote);
    playMode     = parameters.getRawParameterValue (ParamIDs::playMode);
    voiceMode    = parameters.getRawParameterValue (ParamIDs::voiceMode);
    reverse      = parameters.getRawParameterValue (ParamIDs::reverse);
    attackMs     = parameters.getRawParameterValue (ParamIDs::attack);
    releaseMs    = parameters.getRawParameterValue (ParamIDs::release);
    pitch        = parameters.getRawParameterValue (ParamIDs::pitch);
    gainDb       = parameters.getRawParameterValue (ParamIDs::gain);

    // 4 times a second, free any sample the audio thread has finished with.
    startTimerHz (4);
}

SliceAbleProcessor::~SliceAbleProcessor()
{
    stopTimer();
}

//==============================================================================
// Host callbacks
//==============================================================================

void SliceAbleProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    audioLayoutLength = -1; // force the slices to be recomputed on the next block

    // Gain changes glide over 20 ms instead of jumping, which would click.
    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));

    keyboardState.reset(); // forget any keys stuck "down" from before
}

void SliceAbleProcessor::releaseResources()
{
    engine.stopAllVoices();
}

bool SliceAbleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // The engine can render mono or stereo; anything else (e.g. 5.1) is refused.
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // Instruments have no audio input.
    return layouts.getMainInputChannelSet().isDisabled();
}

//==============================================================================
// Parameters -> engine settings
//==============================================================================

GridSettings SliceAbleProcessor::buildGridSettings (const SampleData& sample, double bpm, TimeSignature ts) const
{
    // `bpm` is the host tempo (or the Manual BPM fallback when the host has none).
    // Called from BOTH the audio thread and the UI, so it only reads atomics.
    GridSettings g;
    g.divisionIndex = (int) division->load();
    g.timeSignature = ts;

    // Pick the tempo according to the Tempo Source menu.
    switch ((TempoMode) (int) tempoMode->load())
    {
        case TempoMode::host:    g.bpm = bpm; break;
        case TempoMode::manual:  g.bpm = manualBpm->load(); break;
        case TempoMode::fitBars: g.bpm = bpmToFitBars (sample.getLengthInSamples(), sample.sampleRate,
                                                       fitBars->load(), ts); break;
    }

    g.bpm = clampBpm (g.bpm);

    // The grid works in sample frames of the FILE, so convert ms using the file's rate.
    g.offsetSamples = gridOffsetMs->load() * sample.sampleRate / 1000.0;
    g.minSliceSamples = minSliceMs * sample.sampleRate / 1000.0;
    return g;
}

PlaybackParams SliceAbleProcessor::readPlaybackParams() const
{
    // Choice parameters arrive as floats holding the menu index (0.0, 1.0, ...),
    // so they're cast to int and then to the matching enum.
    PlaybackParams p;
    p.attackMs = attackMs->load();
    p.releaseMs = releaseMs->load();
    p.gainLinear = 1.0f; // applied (smoothed) to the whole output instead
    p.pitchSemitones = pitch->load();
    p.playMode = (PlayMode) (int) playMode->load();
    p.voiceMode = (VoiceMode) (int) voiceMode->load();
    p.reverse = reverse->load() >= 0.5f; // bool parameters are stored as 0.0 / 1.0
    return p;
}

//==============================================================================
// The audio callback
//==============================================================================

void SliceAbleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Treat tiny "denormal" floats as zero; they can make some CPUs very slow
    // when a sound decays towards silence.
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    buffer.clear(); // we generate all the output ourselves (no audio input)

    // --- 1. Pick up a newly loaded sample --------------------------------------
    // Only TRY the lock: if the message thread happens to hold it right now, skip
    // and try again next block rather than waiting. The old sample is parked in
    // pendingSample and freed later by the timer, never here.
    {
        const juce::SpinLock::ScopedTryLockType lock (handOffLock);

        if (lock.isLocked() && pendingIsNew)
        {
            pendingSample = engine.swapSample (std::move (pendingSample));
            pendingIsNew = false;
            audioLayoutLength = -1; // new sample -> recompute slices below
        }
    }

    // --- 2. Merge notes played on the on-screen keyboard / waveform into the MIDI
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    // --- 3. Ask the host for its tempo and time signature ----------------------
    // Follow the session tempo and time signature, like the grid in a DAW.
    auto bpm = (double) manualBpm->load(); // fallback when the host doesn't report a tempo
    TimeSignature ts { hostNumerator.load(), hostDenominator.load() };
    auto tempoValid = false;

    if (auto* transport = getPlayHead()) // null in some hosts / the standalone app
    {
        if (const auto position = transport->getPosition())
        {
            if (const auto hostTempo = position->getBpm(); hostTempo.hasValue() && *hostTempo > 0.0)
            {
                bpm = *hostTempo;
                tempoValid = true;
            }

            if (const auto sig = position->getTimeSignature(); sig.hasValue() && sig->numerator > 0 && sig->denominator > 0)
                ts = { sig->numerator, sig->denominator };
        }
    }

    // Publish what we learned for the UI (it can't ask the host itself).
    hostBpmValid.store (tempoValid);
    if (tempoValid)
        hostBpm.store (bpm);
    hostNumerator.store (ts.numerator);
    hostDenominator.store (ts.denominator);

    if (const auto* sample = engine.getSample())
    {
        // --- 4. Re-slice if anything that affects the grid changed --------------
        // computeSlices is cheap, but skipping it when nothing changed keeps
        // processBlock lean. Exact float comparison is intended: any change at
        // all should trigger a recompute.
        const auto settings = buildGridSettings (*sample, bpm, ts);
        const auto length = sample->getLengthInSamples();

        const auto changed = length != audioLayoutLength
                          || ! juce::exactlyEqual (settings.bpm, audioLayoutSettings.bpm)
                          || settings.divisionIndex != audioLayoutSettings.divisionIndex
                          || ! juce::exactlyEqual (settings.offsetSamples, audioLayoutSettings.offsetSamples)
                          || settings.timeSignature.numerator != audioLayoutSettings.timeSignature.numerator
                          || settings.timeSignature.denominator != audioLayoutSettings.timeSignature.denominator;

        if (changed)
        {
            computeSlices (settings, length, sample->sampleRate, audioLayout);
            audioLayoutSettings = settings;
            audioLayoutLength = length;
        }

        // --- 5. Play the slices -------------------------------------------------
        engine.process (buffer, midi, audioLayout, (int) rootNote->load(), readPlaybackParams());

        // --- 6. Output level (smoothed). -48 dB and below counts as silence. -----
        outputGain.setTargetValue (juce::Decibels::decibelsToGain (gainDb->load(), -48.0f));
        outputGain.applyGain (buffer, numSamples);
    }

    // We don't send MIDI anywhere; clear it so the host doesn't receive our input back.
    midi.clear();
}

//==============================================================================
// Grid queries for the UI
//==============================================================================

GridInfo SliceAbleProcessor::getGridInfo (const SampleData* sample) const
{
    GridInfo info;
    info.tempoMode = (TempoMode) (int) tempoMode->load();
    info.hostTempoAvailable = hostBpmValid.load();

    // Same fallback rule as processBlock: no host tempo -> use the Manual BPM knob.
    const auto bpm = info.hostTempoAvailable ? hostBpm.load() : (double) manualBpm->load();
    const TimeSignature ts { hostNumerator.load(), hostDenominator.load() };

    if (sample != nullptr)
        info.settings = buildGridSettings (*sample, bpm, ts);

    return info;
}

void SliceAbleProcessor::computeLayout (const SampleData* sample, SliceLayout& out) const
{
    if (sample == nullptr)
    {
        out.numSlices = 0;
        out.numUnmapped = 0;
        return;
    }

    computeSlices (getGridInfo (sample).settings, sample->getLengthInSamples(), sample->sampleRate, out);
}

int SliceAbleProcessor::getRootNote() const
{
    return (int) rootNote->load();
}

//==============================================================================
// Loading samples and handing them to the audio thread
//==============================================================================

juce::String SliceAbleProcessor::loadSample (const juce::File& file)
{
    // Decode the file here on the message thread (may take a moment).
    auto result = SampleData::load (file, formatManager);

    if (result.sample == nullptr)
        return result.error; // the current sample stays loaded

    {
        const juce::ScopedLock sl (sampleLock);
        messageThreadSample = result.sample;
        missingSamplePath.clear();
    }

    handOverToAudioThread (result.sample);
    sendChangeMessage(); // tells the editor to redraw (asynchronously, on the message thread)
    return {};
}

void SliceAbleProcessor::clearSample()
{
    {
        const juce::ScopedLock sl (sampleLock);
        messageThreadSample = nullptr;
        missingSamplePath.clear();
    }

    handOverToAudioThread (nullptr); // the engine will swap in "no sample"
    sendChangeMessage();
}

SampleData::Ptr SliceAbleProcessor::getSample() const
{
    const juce::ScopedLock sl (sampleLock);
    return messageThreadSample;
}

juce::String SliceAbleProcessor::getMissingSamplePath() const
{
    const juce::ScopedLock sl (sampleLock);
    return missingSamplePath;
}

void SliceAbleProcessor::handOverToAudioThread (SampleData::Ptr newSample)
{
    SampleData::Ptr previous;

    {
        // Hold the lock only long enough to swap pointers (no allocation inside).
        const juce::SpinLock::ScopedLockType sl (handOffLock);
        previous = std::move (pendingSample); // a retired or never-collected sample
        pendingSample = std::move (newSample);
        pendingIsNew = true;                   // processBlock picks it up next block
    }
    // `previous` is released here, outside the lock, on this (non-audio) thread.
}

void SliceAbleProcessor::timerCallback()
{
    // If the audio thread has taken the pending sample, what's left in
    // pendingSample is the OLD one it swapped out. Free it here, safely off
    // the audio thread.
    SampleData::Ptr retired;

    {
        const juce::SpinLock::ScopedLockType sl (handOffLock);

        if (! pendingIsNew)
            retired = std::move (pendingSample);
    }
    // `retired` is released here, off the audio thread.
}

//==============================================================================
// Saving and restoring with the project
//==============================================================================
//
// The state is the APVTS's parameter tree plus one extra property, "samplePath",
// stored as XML (inside JUCE's binary wrapper). Only the PATH of the sample is
// saved, not the audio, so projects stay small - but if the file is moved or
// deleted, the sample can't be reloaded (the UI then shows "Sample not found").
//
// To embed the audio in the project instead, you would write the sample data
// (e.g. as FLAC) into the state here and read it back in setStateInformation.

void SliceAbleProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState(); // a copy: safe to modify

    {
        const juce::ScopedLock sl (sampleLock);
        // Keep a missing sample's path too, so re-saving the session doesn't lose the reference.
        const auto path = messageThreadSample != nullptr ? messageThreadSample->file.getFullPathName()
                                                         : missingSamplePath;
        state.setProperty (samplePathProperty, path, nullptr);
    }

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SliceAbleProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    // Ignore data that isn't ours (wrong root element) rather than half-loading it.
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    // Pull our extra property out before handing the tree to the APVTS.
    auto tree = juce::ValueTree::fromXml (*xml);
    const auto path = tree.getProperty (samplePathProperty).toString();
    tree.removeProperty (samplePathProperty, nullptr);
    parameters.replaceState (tree); // restores every knob / menu

    if (path.isEmpty())
    {
        clearSample(); // the project had no sample loaded
        return;
    }

    // Sessions may come from another machine or OS, so don't assume the path is valid here.
    // (e.g. a Windows path "C:\..." is not an absolute path on macOS.)
    const auto file = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();

    if (file != juce::File() && file.existsAsFile() && loadSample (file).isEmpty())
        return; // reloaded successfully

    // File missing or unreadable: start empty but remember the path for the UI
    // (and so saving again keeps it).
    clearSample();

    const juce::ScopedLock sl (sampleLock);
    missingSamplePath = path;
}

juce::AudioProcessorEditor* SliceAbleProcessor::createEditor()
{
    return new SliceAbleEditor (*this);
}

} // namespace sliceable

//==============================================================================
// Entry point: JUCE's plug-in wrappers (VST3, AU, Standalone) call this to
// create a new instance of the plug-in. Must be outside any namespace.
//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sliceable::SliceAbleProcessor();
}
