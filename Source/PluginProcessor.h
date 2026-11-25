#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "stage/StageRegistry.h"
#include "stage/HeadTracking.h"
#include "dsp/DirectPath.h"
#include "dsp/LoadMeter.h"

class ComposerProcessor final : public juce::AudioProcessor
{
public:
    ComposerProcessor();
    ~ComposerProcessor() override;

    void prepareToPlay (double sampleRate, int) override;
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
    double getTailLengthSeconds() const override        { return tailSeconds; }

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

    // Input meter (linear gain): the loudest peak so far, falling ~300 ms after it.
    // Message thread only; any number of editors may read it (each shows every track).
    float readInputLevel (juce::uint32 nowMs);

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
    juce::SharedResourcePointer<HeadTracking> headTracking;

    // Gestures (message thread). Hosts record automation only for writes inside one, and want
    // every begin ended exactly once: a second begin, or an end without a begin, does nothing.
    void beginGesture (const juce::String& paramId);
    void endGesture (const juce::String& paramId);
    // Writes one parameter, in its real units: inside the gesture that is open on it, else as
    // a complete gesture of its own.
    void writeParam (const juce::String& paramId, float value);

    // How long the output goes on after the input stops (reported to the host; also how long a
    // silent track keeps processing): with the direct sound alone, the longest delay.
    static constexpr double tailSeconds = Acoustics::maxDelaySeconds;

    // Development monitoring: processBlock's share of real time (realtime playback only).
    LoadMeter load;

private:
    juce::StringArray gesturesOpen; // parameter ids, message thread only

    // Cached for the audio thread (looking up by name would allocate a String).
    struct AudioParams
    {
        std::atomic<float>* roomSize;
        std::atomic<float>* listenerX;
        std::atomic<float>* listenerY;
        std::atomic<float>* playerX;
        std::atomic<float>* playerY;
        std::atomic<float>* outputMode;
        std::atomic<float>* proDistanceLaw;
    };

    const AudioParams audioParams { state.getRawParameterValue ("roomSize"),
                                    state.getRawParameterValue ("listenerX"),
                                    state.getRawParameterValue ("listenerY"),
                                    state.getRawParameterValue ("playerX"),
                                    state.getRawParameterValue ("playerY"),
                                    state.getRawParameterValue ("outputMode"),
                                    state.getRawParameterValue ("proDistanceLaw") };

    // The player's mono input, read by every stage.
    History history;

    // The processing chain: every stage reads `history` and adds its stereo output.
    // Plug a stage in or out here. For now the direct sound alone (reflections and a reverb
    // tail come back once they are affordable).
    std::tuple<DirectPath> stages;

    // Samples of silent input so far; a track silent for longer than the tail skips all work.
    int silentFor = 0, skipAfter = 0;

    double preparedRate = 0; // from prepareToPlay (the load meter's block length)

    juce::String trackName, trackColour;

    std::atomic<float> inputPeak { 0.0f };
    static_assert (std::atomic<float>::is_always_lock_free);
    float meterLevel = 0.0f;       // message thread
    juce::uint32 meterReadAt = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComposerProcessor)
};
