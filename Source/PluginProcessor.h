#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "stage/StageRegistry.h"

class ComposerProcessor final : public juce::AudioProcessor
{
public:
    ComposerProcessor();
    ~ComposerProcessor() override;

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

    void updateTrackProperties (const TrackProperties&) override;

    // Single source of truth for parameters and saved scene data.
    juce::AudioProcessorValueTreeState state { *this, nullptr, "Composer", createParameterLayout() };

    // Peak input level since the last call; read by the editor's timer (message thread).
    float takeInputPeak() noexcept                      { return inputPeak.exchange (0.0f); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static inline const juce::StringArray sceneParams { "roomSize", "listenerX", "listenerY", "playerX", "playerY" };

    // Stage data (message thread). Positions are unclamped, in metres.
    float getParam (const juce::String& paramId) const  { return state.getRawParameterValue (paramId)->load(); }
    // Scenes A-H: alternative layouts of the whole ensemble. The parameters hold the
    // active scene; the others are stored in the state tree. See stage::setScene.
    juce::String getScene() const                       { return state.state.getProperty ("scene", "A"); }
    void storeScene();                                  // parameters -> active scene's slot
    bool recallScene (const juce::String& scene);       // that scene's slot -> parameters; false if never stored
    juce::StringArray getStoredScenes() const;

    // False for a brand-new insert until it has joined the stage (copied the shared
    // room/listener and scene, taken a seat). Saved, so a reloaded project never re-joins.
    bool isPlaced() const                               { return state.state.getProperty ("placed", false); }
    void markPlaced()                                   { state.state.setProperty ("placed", true, nullptr); }
    void setActiveScene (const juce::String& scene)     { state.state.setProperty ("scene", scene, nullptr); }
    juce::String getDisplayName() const;                // name override, else host track name, else empty
    void setNameOverride (const juce::String& name)     { state.state.setProperty ("name", name.trim(), nullptr); }

    // Host track colour as "#rrggbb", or empty when the host doesn't send one.
    juce::String getTrackColour() const                 { return trackColour; }

    // Unique within the process; identifies this player to the web UI.
    const int id;

    juce::SharedResourcePointer<StageRegistry> registry;

    // A complete gesture on one parameter, in its real units.
    void writeParam (const juce::String& paramId, float value);

private:
    juce::String trackName, trackColour;

    std::atomic<float> inputPeak { 0.0f };
    static_assert (std::atomic<float>::is_always_lock_free);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComposerProcessor)
};
