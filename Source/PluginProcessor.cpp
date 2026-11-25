#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "bridge/Stage.h"
#include "dsp/Geometry.h"

namespace
{
std::atomic<int> nextId { 0 };

const float dbPerDoublingFreeField = 20.0f * std::log10 (2.0f); // 6.02 dB

auto metres (const juce::String& id, const juce::String& name, float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 },
                                                        name,
                                                        juce::NormalisableRange<float> { -25.0f, 25.0f, 0.01f },
                                                        defaultValue,
                                                        juce::AudioParameterFloatAttributes{}.withLabel ("m"));
}
} // namespace

ComposerProcessor::ComposerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      id (nextId++)
{
    registry->add (*this);

    // Join the stage once the host has had its chance to restore a saved state (which
    // marks the instance placed). Captures the id, not `this`: hosts often create and
    // destroy instances in quick succession while scanning.
    juce::MessageManager::callAsync ([instance = id, reg = registry]
                                     {
                                         reg->withMember (instance, [] (ComposerProcessor& p)
                                         {
                                             if (! p.isPlaced())
                                                 stage::join (p, true);
                                         });
                                     });
}

ComposerProcessor::~ComposerProcessor()
{
    // First, before any member goes away: other editors may be reading this instance.
    registry->remove (*this);

    // A track removed in the middle of someone's drag: the host still gets its ends.
    for (const auto& paramId : juce::StringArray (gesturesOpen))
        endGesture (paramId);
}

juce::AudioProcessorValueTreeState::ParameterLayout ComposerProcessor::createParameterLayout()
{
    return {
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "roomSize", 1 },
                                                     "Room size",
                                                     juce::NormalisableRange<float> { 5.0f, 50.0f, 5.0f },
                                                     20.0f,
                                                     juce::AudioParameterFloatAttributes{}.withLabel ("m")),
        metres ("listenerX", "Listener X", 0.0f),
        metres ("listenerY", "Listener Y", 0.0f),
        metres ("playerX", "Player X", 0.0f),
        metres ("playerY", "Player Y", 5.0f),
        std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "outputMode", 1 },
                                                      "Output",
                                                      juce::StringArray { "Stereo", "Binaural" },
                                                      0),

        // Pro panel: project-wide, not part of scenes; neutral defaults.
        // No step: the default must be exactly today's -20·log10(2)/3 dB per doubling.
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "proDistanceLaw", 1 }, "Distance law",
                                                     juce::NormalisableRange<float> { 0.0f, dbPerDoublingFreeField },
                                                     dbPerDoublingFreeField * geometry::distanceLawExponent,
                                                     juce::AudioParameterFloatAttributes{}.withLabel ("dB/doubling")),
    };
}

juce::String ComposerProcessor::getDisplayName() const
{
    if (const auto name = state.state.getProperty ("name").toString(); name.isNotEmpty())
        return name;

    return trackName;
}

void ComposerProcessor::beginGesture (const juce::String& paramId)
{
    if (! gesturesOpen.contains (paramId))
    {
        gesturesOpen.add (paramId);
        state.getParameter (paramId)->beginChangeGesture();
    }
}

void ComposerProcessor::endGesture (const juce::String& paramId)
{
    if (gesturesOpen.contains (paramId))
    {
        gesturesOpen.removeString (paramId);
        state.getParameter (paramId)->endChangeGesture();
    }
}

void ComposerProcessor::writeParam (const juce::String& paramId, float value)
{
    auto* param = state.getParameter (paramId);
    const auto insideGesture = gesturesOpen.contains (paramId);

    if (! insideGesture)
        param->beginChangeGesture();

    param->setValueNotifyingHost (param->convertTo0to1 (value));

    if (! insideGesture)
        param->endChangeGesture();
}

void ComposerProcessor::storeScene()
{
    const juce::ScopedLock lock (stateTreeLock);
    auto scenes = state.state.getOrCreateChildWithName ("scenes", nullptr);
    auto slot = scenes.getOrCreateChildWithName (getScene(), nullptr);

    for (const auto& paramId : sceneParams)
        slot.setProperty (paramId, getParam (paramId), nullptr);
}

bool ComposerProcessor::recallScene (const juce::String& scene)
{
    const auto slot = state.state.getChildWithName ("scenes").getChildWithName (scene);

    if (! slot.isValid())
        return false;

    for (const auto& paramId : sceneParams)
        if (slot.hasProperty (paramId))
            writeParam (paramId, slot[paramId]);

    return true;
}

juce::StringArray ComposerProcessor::getStoredScenes() const
{
    juce::StringArray names;

    for (const auto& slot : state.state.getChildWithName ("scenes"))
        names.add (slot.getType().toString());

    return names;
}

void ComposerProcessor::updateTrackProperties (const TrackProperties& properties)
{
    trackName = properties.name.value_or (juce::String{});
    trackColour = properties.colourARGB ? "#" + juce::Colour (*properties.colourARGB).toDisplayString (false).toLowerCase()
                                        : juce::String{};
}

bool ComposerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

float ComposerProcessor::readInputLevel (juce::uint32 nowMs)
{
    // Falls by elapsed time, not per read: many readers, or a host that stopped calling
    // processBlock, still see the level die away.
    const auto decay = std::exp (-(float) (nowMs - meterReadAt) / 300.0f);
    meterReadAt = nowMs;
    meterLevel = std::max (inputPeak.exchange (0.0f), meterLevel * decay);
    return meterLevel;
}

void ComposerProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // The dry sound in the middle: a mono track's input on both channels (JUCE's own bypass
    // would leave the right one silent). A stereo input passes through as it is.
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    bypassedSinceLastBlock = true;
}

void ComposerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Development monitoring: the whole block, early returns included; offline renders aren't
    // real time, so they don't count.
    struct Timed
    {
        LoadMeter& meter;
        double blockSeconds;
        std::int64_t start = juce::Time::getHighResolutionTicks();
        ~Timed() { meter.add (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start), blockSeconds); }
    } timed { load, isNonRealtime() || preparedRate <= 0 ? 0.0 : buffer.getNumSamples() / preparedRate };

    float peak = 0.0f;

    for (int ch = 0; ch < getTotalNumInputChannels(); ++ch)
        peak = std::max (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    // Keep the loudest block until readInputLevel takes it.
    for (auto prev = inputPeak.load (std::memory_order_relaxed);
         prev < peak && ! inputPeak.compare_exchange_weak (prev, peak, std::memory_order_relaxed);)
        ;

    // A host calling before prepareToPlay, or with no input bus: silence, never a read past
    // an empty history or a channel that isn't there.
    if (! history.isPrepared() || getTotalNumInputChannels() == 0 || buffer.getNumChannels() < 2)
    {
        buffer.clear();
        return;
    }

    // Back from bypass: the sound that was on its way before it would come out now.
    if (std::exchange (bypassedSinceLastBlock, false))
    {
        history.clear();
        std::apply ([] (auto&... stage) { (stage.clear(), ...); }, stages);
        silentFor = 0;
    }

    // Audio depends only on this instance's own parameters (never on UI pushes or other
    // instances), so offline bounces and ahead-of-time rendering stay correct.
    Acoustics acoustics;
    acoustics.roomSize = audioParams.roomSize->load();
    acoustics.listener = geometry::clampToRoom ({ audioParams.listenerX->load(), audioParams.listenerY->load() }, acoustics.roomSize);
    acoustics.player = geometry::clampToRoom ({ audioParams.playerX->load(), audioParams.playerY->load() }, acoustics.roomSize);
    acoustics.mode = audioParams.outputMode->load() > 0.5f ? OutputMode::binaural : OutputMode::stereo;

    // Head tracking is a live monitoring aid: never in exports, never in stereo.
    if (acoustics.mode == OutputMode::binaural && ! isNonRealtime())
        acoustics.headYaw = headTracking->yaw();

    auto& adjust = acoustics.adjust;
    adjust.distanceExponent = audioParams.proDistanceLaw->load() / dbPerDoublingFreeField;

    // A track silent for longer than the tail produces silence: skip all the work. When
    // sound returns, everything inside is silent too, so stages jump straight to the
    // current position instead of fading from where they were.
    if (peak <= 0.0f)
    {
        if (silentFor >= skipAfter)
        {
            buffer.clear();
            return;
        }

        silentFor += buffer.getNumSamples();
    }
    else
    {
        if (silentFor >= skipAfter)
            std::apply ([] (auto&... stage) { (stage.jumpToTargets(), ...); }, stages);

        silentFor = 0;
    }

    std::apply ([&] (auto&... stage) { (stage.setAcoustics (acoustics), ...); }, stages);

    const auto numInputs = getTotalNumInputChannels();
    const auto* inL = buffer.getReadPointer (0);
    const auto* inR = buffer.getReadPointer (numInputs > 1 ? 1 : 0);
    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);

    // A few dozen samples at a time: the input into the History, then each stage over all of them.
    for (int start = 0; start < buffer.getNumSamples(); start += History::maxBlock)
    {
        const auto n = std::min (History::maxBlock, buffer.getNumSamples() - start);

        // A stereo input is summed to mono (read before the outputs overwrite it).
        for (int i = start; i < start + n; ++i)
            history.write (numInputs > 1 ? 0.5f * (inL[i] + inR[i]) : inL[i]);

        alignas (32) float left[History::maxBlock] {}, right[History::maxBlock] {};
        std::apply ([&] (auto&... stage) { (stage.process (history, left, right, n), ...); }, stages);

        std::copy (left, left + n, outL + start);
        std::copy (right, right + n, outR + start);
    }
}

void ComposerProcessor::prepareToPlay (double sampleRate, int)
{
    preparedRate = sampleRate;
    history.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * sampleRate));
    std::apply ([&] (auto&... stage) { (stage.prepare (sampleRate), ...); }, stages);
    skipAfter = (int) (tailSeconds * sampleRate);
    silentFor = 0;
}

juce::AudioProcessorEditor* ComposerProcessor::createEditor()
{
    return new ComposerEditor (*this);
}

void ComposerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto copy = [this]
    {
        const juce::ScopedLock lock (stateTreeLock);
        return state.copyState();
    }();

    if (auto xml = copy.createXml())
        copyXmlToBinary (*xml, destData);
}

void ComposerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes); xml != nullptr && xml->hasTagName (state.state.getType()))
    {
        const juce::ScopedLock lock (stateTreeLock);
        state.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ComposerProcessor();
}
