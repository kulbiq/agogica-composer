#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ComposerProcessor final : public juce::AudioProcessor
{
public:
    ComposerProcessor();

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Single source of truth for parameters and saved scene data.
    juce::AudioProcessorValueTreeState state { *this, nullptr, "Composer", createParameterLayout() };

    // Peak input level since the last call; read by the editor's timer (message thread).
    float takeInputPeak() noexcept                      { return inputPeak.exchange (0.0f); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    std::atomic<float> inputPeak { 0.0f };
    static_assert (std::atomic<float>::is_always_lock_free);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComposerProcessor)
};
