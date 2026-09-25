#include "PluginProcessor.h"
#include "PluginEditor.h"

ComposerProcessor::ComposerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

juce::AudioProcessorValueTreeState::ParameterLayout ComposerProcessor::createParameterLayout()
{
    return {
        std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "roomSize", 1 },
                                                     "Room size",
                                                     juce::NormalisableRange<float> { 5.0f, 50.0f, 5.0f },
                                                     20.0f,
                                                     juce::AudioParameterFloatAttributes{}.withLabel ("m")),
    };
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

    // Pass-through; a mono input is copied to the right channel.
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
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
