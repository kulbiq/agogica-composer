#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "bridge/Stage.h"
#include "dsp/Geometry.h"

namespace
{
std::atomic<int> nextId { 0 };

constexpr float proOffDb = -24.0f;                     // bottom of a level control = off
const float dbPerDoublingFreeField = 20.0f * std::log10 (2.0f); // 6.02 dB

float levelFromDb (float db) { return db <= proOffDb + 0.05f ? 0.0f : juce::Decibels::decibelsToGain (db); }

// Pro panel parameter helpers (see Adjustments in dsp/Acoustics.h).
auto decibels (const juce::String& id, const juce::String& name)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                        juce::NormalisableRange<float> { proOffDb, 6.0f, 0.1f }, 0.0f,
                                                        juce::AudioParameterFloatAttributes{}.withLabel ("dB"));
}

auto amount (const juce::String& id, const juce::String& name, float max, float defaultValue)
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                        juce::NormalisableRange<float> { 0.0f, max, 0.01f }, defaultValue,
                                                        juce::AudioParameterFloatAttributes{}.withLabel ("%")
                                                            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)); }));
}

auto factor (const juce::String& id, const juce::String& name)
{
    juce::NormalisableRange<float> range { 0.5f, 2.0f };
    range.setSkewForCentre (1.0f);
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, range, 1.0f,
                                                        juce::AudioParameterFloatAttributes{}.withLabel ("x"));
}

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
        std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "hall", 1 },
                                                      "Hall",
                                                      [] { juce::StringArray names; for (const auto& h : halls) names.add (h.name); return names; }(),
                                                      defaultHall),

        // Pro panel: project-wide, not part of scenes; neutral defaults.
        decibels ("proReflections", "Reflections"),
        amount ("proDiffusion", "Diffusion", 1.0f, 1.0f),
        amount ("proFloor", "Floor reflection", 1.0f, 1.0f),
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "proBrightness", 1 }, "Brightness",
                                                     juce::NormalisableRange<float> { -12.0f, 12.0f, 0.1f }, 0.0f,
                                                     juce::AudioParameterFloatAttributes{}.withLabel ("st")),
        decibels ("proReverb", "Reverb"),
        factor ("proDecay", "Decay"),
        factor ("proPreDelay", "Pre-delay"),
        // No step: the default must be exactly today's -20·log10(2)/3 dB per doubling.
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "proDistanceLaw", 1 }, "Distance law",
                                                     juce::NormalisableRange<float> { 0.0f, dbPerDoublingFreeField },
                                                     dbPerDoublingFreeField * geometry::distanceLawExponent,
                                                     juce::AudioParameterFloatAttributes{}.withLabel ("dB/doubling")),
        amount ("proAir", "Air absorption", 2.0f, 1.0f),
    };
}

juce::String ComposerProcessor::getDisplayName() const
{
    if (const auto name = state.state.getProperty ("name").toString(); name.isNotEmpty())
        return name;

    return trackName;
}

void ComposerProcessor::writeParam (const juce::String& paramId, float value)
{
    auto* param = state.getParameter (paramId);
    param->beginChangeGesture();
    param->setValueNotifyingHost (param->convertTo0to1 (value));
    param->endChangeGesture();
}

void ComposerProcessor::storeScene()
{
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

void ComposerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    float peak = 0.0f;

    for (int ch = 0; ch < getTotalNumInputChannels(); ++ch)
        peak = std::max (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    // Keep the loudest block until readInputLevel takes it.
    for (auto prev = inputPeak.load (std::memory_order_relaxed);
         prev < peak && ! inputPeak.compare_exchange_weak (prev, peak, std::memory_order_relaxed);)
        ;

    // Not activated on this Mac (or the licence ran out): pass the input through untouched.
    if (! licence->isValid())
    {
        for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
            if (getTotalNumInputChannels() > 0)
                buffer.copyFrom (ch, 0, buffer, 0, 0, buffer.getNumSamples());
            else
                buffer.clear (ch, 0, buffer.getNumSamples());
        return;
    }

    // Audio depends only on this instance's own parameters (never on UI pushes or other
    // instances), so offline bounces and ahead-of-time rendering stay correct.
    Acoustics acoustics;
    acoustics.roomSize = audioParams.roomSize->load();
    acoustics.listener = geometry::clampToRoom ({ audioParams.listenerX->load(), audioParams.listenerY->load() }, acoustics.roomSize);
    acoustics.player = geometry::clampToRoom ({ audioParams.playerX->load(), audioParams.playerY->load() }, acoustics.roomSize);
    acoustics.mode = audioParams.outputMode->load() > 0.5f ? OutputMode::binaural : OutputMode::stereo;
    acoustics.hall = &halls[(size_t) juce::jlimit (0, (int) halls.size() - 1, (int) audioParams.hall->load())];

    // Head tracking is a live monitoring aid: never in exports, never in stereo.
    if (acoustics.mode == OutputMode::binaural && ! isNonRealtime())
        acoustics.headYaw = headTracking->yaw();

    auto& adjust = acoustics.adjust;
    adjust.reflections = levelFromDb (audioParams.proReflections->load());
    adjust.diffusion = audioParams.proDiffusion->load();
    adjust.floor = audioParams.proFloor->load();
    adjust.brightness = std::exp2 (audioParams.proBrightness->load() / 12.0f);
    adjust.reverb = levelFromDb (audioParams.proReverb->load());
    adjust.decay = audioParams.proDecay->load();
    adjust.preDelay = audioParams.proPreDelay->load();
    adjust.distanceExponent = audioParams.proDistanceLaw->load() / dbPerDoublingFreeField;
    adjust.air = audioParams.proAir->load();

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

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        // A stereo input is summed to mono (read before the outputs overwrite it).
        history.write (numInputs > 1 ? 0.5f * (inL[i] + inR[i]) : inL[i]);

        float left = 0.0f, right = 0.0f;
        std::apply ([&] (auto&... stage) { (stage.processSample (history, left, right), ...); }, stages);

        outL[i] = left;
        outR[i] = right;
    }
}

void ComposerProcessor::prepareToPlay (double sampleRate, int)
{
    history.prepare ((int) std::ceil (Acoustics::maxDelaySeconds * sampleRate));
    std::apply ([&] (auto&... stage) { (stage.prepare (sampleRate), ...); }, stages);
    skipAfter = (int) (Acoustics::tailSeconds * sampleRate);
    silentFor = 0;
}

juce::AudioProcessorEditor* ComposerProcessor::createEditor()
{
    return new ComposerEditor (*this);
}

void ComposerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ComposerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes); xml != nullptr && xml->hasTagName (state.state.getType()))
        state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ComposerProcessor();
}
