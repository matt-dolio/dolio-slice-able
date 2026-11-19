#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace sliceable
{

namespace
{
    const juce::Identifier samplePathProperty { "samplePath" };
    constexpr double minSliceMs = 2.0;
}

SliceAbleProcessor::SliceAbleProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "SliceAble", createParameterLayout())
{
    formatManager.registerBasicFormats();

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

    startTimerHz (4);
}

SliceAbleProcessor::~SliceAbleProcessor()
{
    stopTimer();
}

//==============================================================================
void SliceAbleProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    audioLayoutLength = -1;

    outputGain.reset (sampleRate, 0.02);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainDb->load()));

    keyboardState.reset();
}

void SliceAbleProcessor::releaseResources()
{
    engine.stopAllVoices();
}

bool SliceAbleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet().isDisabled();
}

//==============================================================================
GridSettings SliceAbleProcessor::buildGridSettings (const SampleData& sample, double bpm, TimeSignature ts) const
{
    GridSettings g;
    g.divisionIndex = (int) division->load();
    g.timeSignature = ts;

    switch ((TempoMode) (int) tempoMode->load())
    {
        case TempoMode::host:    g.bpm = bpm; break;
        case TempoMode::manual:  g.bpm = manualBpm->load(); break;
        case TempoMode::fitBars: g.bpm = bpmToFitBars (sample.getLengthInSamples(), sample.sampleRate,
                                                       fitBars->load(), ts); break;
    }

    g.bpm = clampBpm (g.bpm);
    g.offsetSamples = gridOffsetMs->load() * sample.sampleRate / 1000.0;
    g.minSliceSamples = minSliceMs * sample.sampleRate / 1000.0;
    return g;
}

PlaybackParams SliceAbleProcessor::readPlaybackParams() const
{
    PlaybackParams p;
    p.attackMs = attackMs->load();
    p.releaseMs = releaseMs->load();
    p.gainLinear = 1.0f; // applied (smoothed) to the whole output instead
    p.pitchSemitones = pitch->load();
    p.playMode = (PlayMode) (int) playMode->load();
    p.voiceMode = (VoiceMode) (int) voiceMode->load();
    p.reverse = reverse->load() >= 0.5f;
    return p;
}

void SliceAbleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    buffer.clear();

    // Pick up a newly loaded sample. The old one is parked in pendingSample and freed by the timer.
    {
        const juce::SpinLock::ScopedTryLockType lock (handOffLock);

        if (lock.isLocked() && pendingIsNew)
        {
            pendingSample = engine.swapSample (std::move (pendingSample));
            pendingIsNew = false;
            audioLayoutLength = -1;
        }
    }

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    // Follow the session tempo and time signature, like the grid in a DAW.
    auto bpm = (double) manualBpm->load(); // fallback when the host doesn't report a tempo
    TimeSignature ts { hostNumerator.load(), hostDenominator.load() };
    auto tempoValid = false;

    if (auto* transport = getPlayHead())
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

    hostBpmValid.store (tempoValid);
    if (tempoValid)
        hostBpm.store (bpm);
    hostNumerator.store (ts.numerator);
    hostDenominator.store (ts.denominator);

    if (const auto* sample = engine.getSample())
    {
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

        engine.process (buffer, midi, audioLayout, (int) rootNote->load(), readPlaybackParams());

        outputGain.setTargetValue (juce::Decibels::decibelsToGain (gainDb->load(), -48.0f));
        outputGain.applyGain (buffer, numSamples);
    }

    midi.clear();
}

//==============================================================================
GridInfo SliceAbleProcessor::getGridInfo (const SampleData* sample) const
{
    GridInfo info;
    info.tempoMode = (TempoMode) (int) tempoMode->load();
    info.hostTempoAvailable = hostBpmValid.load();

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
juce::String SliceAbleProcessor::loadSample (const juce::File& file)
{
    auto result = SampleData::load (file, formatManager);

    if (result.sample == nullptr)
        return result.error;

    {
        const juce::ScopedLock sl (sampleLock);
        messageThreadSample = result.sample;
        missingSamplePath.clear();
    }

    handOverToAudioThread (result.sample);
    sendChangeMessage();
    return {};
}

void SliceAbleProcessor::clearSample()
{
    {
        const juce::ScopedLock sl (sampleLock);
        messageThreadSample = nullptr;
        missingSamplePath.clear();
    }

    handOverToAudioThread (nullptr);
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
        const juce::SpinLock::ScopedLockType sl (handOffLock);
        previous = std::move (pendingSample); // a retired or never-collected sample
        pendingSample = std::move (newSample);
        pendingIsNew = true;
    }
    // `previous` is released here, outside the lock, on this (non-audio) thread.
}

void SliceAbleProcessor::timerCallback()
{
    SampleData::Ptr retired;

    {
        const juce::SpinLock::ScopedLockType sl (handOffLock);

        if (! pendingIsNew)
            retired = std::move (pendingSample);
    }
    // `retired` is released here, off the audio thread.
}

//==============================================================================
void SliceAbleProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();

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

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto path = tree.getProperty (samplePathProperty).toString();
    tree.removeProperty (samplePathProperty, nullptr);
    parameters.replaceState (tree);

    if (path.isEmpty())
    {
        clearSample();
        return;
    }

    // Sessions may come from another machine or OS, so don't assume the path is valid here.
    const auto file = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();

    if (file != juce::File() && file.existsAsFile() && loadSample (file).isEmpty())
        return;

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
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sliceable::SliceAbleProcessor();
}
