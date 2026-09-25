#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "bridge/Stage.h"
#include "dsp/Geometry.h"

namespace
{
std::atomic<int> nextId { 0 };

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

void ComposerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    float peak = 0.0f;

    for (int ch = 0; ch < getTotalNumInputChannels(); ++ch)
        peak = std::max (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));

    // Keep the loudest block until the editor takes it.
    for (auto prev = inputPeak.load (std::memory_order_relaxed);
         prev < peak && ! inputPeak.compare_exchange_weak (prev, peak, std::memory_order_relaxed);)
        ;

    // Audio depends only on this instance's own parameters (never on UI pushes or other
    // instances), so offline bounces and ahead-of-time rendering stay correct.
    const auto room = audioParams.roomSize->load();
    const auto listener = geometry::clampToRoom ({ audioParams.listenerX->load(), audioParams.listenerY->load() }, room);
    const auto player = geometry::clampToRoom ({ audioParams.playerX->load(), audioParams.playerY->load() }, room);

    spatializer.setMode (audioParams.outputMode->load() > 0.5f ? Spatializer::Mode::binaural : Spatializer::Mode::stereo);
    spatializer.setTarget (geometry::azimuthDegrees (player, listener), geometry::delayMs (player, listener) / 1000.0f);
    spatializer.process (buffer.getArrayOfReadPointers(),
                         getTotalNumInputChannels(),
                         buffer.getWritePointer (0),
                         buffer.getWritePointer (1),
                         buffer.getNumSamples());
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
