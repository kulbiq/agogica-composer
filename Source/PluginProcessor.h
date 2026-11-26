#pragma once

#include <map>
#include <set>

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "stage/StageRegistry.h"
#include "stage/HeadTracking.h"
#include "dsp/DelayStretch.h"
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
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlockBypassed;

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
    juce::AudioProcessorValueTreeState state { *this, nullptr, "Composer", params::createLayout() };

    // Input meter (linear gain): the loudest peak so far, falling ~300 ms after it.
    // Message thread only; any number of editors may read it (each shows every track).
    float readInputLevel (juce::uint32 nowMs);

    // Stage data (message thread). Positions are unclamped, in metres.
    // Always a number: for a NaN from the host, the default (a NaN isn't JSON: the page would lose the whole stage).
    float getParam (const juce::String& paramId) const
    {
        const auto v = state.getRawParameterValue (paramId)->load();
        auto* param = state.getParameter (paramId);
        return std::isfinite (v) ? v : param->convertFrom0to1 (param->getDefaultValue());
    }
    // Scenes A-H: alternative layouts of the whole ensemble. The parameters hold the
    // active scene; the others are stored in the state tree. See stage::setScene.
    juce::String getScene() const                       { return stateProperty ("scene", "A"); }
    void storeScene();                                  // parameters -> active scene's slot
    bool recallScene (const juce::String& scene);       // that scene's slot -> parameters; false if never stored
    juce::StringArray getStoredScenes() const;

    // False for a brand-new insert until it has joined the stage (copied the shared
    // room/listener and scene, taken a seat). Saved, so a reloaded project never re-joins.
    bool isPlaced() const                               { return stateProperty ("placed", false); }
    void markPlaced()                                   { setStateProperty ("placed", true); }
    void setActiveScene (const juce::String& scene)     { setStateProperty ("scene", scene); }
    // Two names. The track's: its own override, else the host track name, else empty.
    // The player's (shared by a player's tracks, see getPlayerKey): set when the player has
    // several tracks or was named while it had; else the track's name is the player's.
    juce::String getDisplayName() const;                // the track's name
    void setNameOverride (const juce::String& name)     { setStateProperty ("name", name.trim()); }
    juce::String getPlayerName() const                  { return stateProperty ("playerName").toString(); }
    void setPlayerName (const juce::String& name)       { setStateProperty ("playerName", name.trim()); }
    juce::String getPlayerLabel() const                 { return getPlayerName().isNotEmpty() ? getPlayerName() : getDisplayName(); }
    // Instrument icon key (ui/src/icons.tsx), empty = picked from the name by the page.
    juce::String getIcon() const                        { return stateProperty ("icon").toString(); }
    void setIcon (const juce::String& key)              { setStateProperty ("icon", key); }
    // Whether the tracked head turns this track's sound (Binaural, realtime only). The same on
    // every track (stage::setHeadTracking); saved, on unless turned off.
    bool isHeadTrackingOn() const noexcept              { return headTrackingOn.load (std::memory_order_relaxed); }
    void setHeadTrackingOn (bool on);
    // Where the audience sits (ui/src/audience.ts: a grid of seats as base64, empty = none).
    // The same on every track (stage::setAudience); only drawn.
    juce::String getAudience() const                    { return stateProperty ("audience").toString(); }
    void setAudience (const juce::String& seats)        { setStateProperty ("audience", seats); }

    // A player with several tracks: its tracks share this key (saved; empty = a player of its
    // own) and stand in one place with one name and icon (bridge/StageEdits: joinPlayer).
    juce::String getPlayerKey() const                   { return stateProperty ("player").toString(); }
    void setPlayerKey (const juce::String& key)         { setStateProperty ("player", key); }
    // This track's place in every stored scene becomes `other`'s; where `other` has none,
    // it keeps its current place there too.
    void copyScenePlaces (const ComposerProcessor& other);

    // Host track colour as "#rrggbb", or empty when the host doesn't send one.
    juce::String getTrackColour() const                 { return trackColour; }

    // Unique within the process; identifies this player to the web UI.
    const int id;

    juce::SharedResourcePointer<StageRegistry> registry;
    juce::SharedResourcePointer<HeadTracking> headTracking;

    // Gestures (message thread). Hosts record automation only for writes inside one, and want
    // one begin and one end per parameter, spanning all owners. Each owner can open it once.
    void beginGesture (const juce::String& paramId, const void* owner);
    void endGesture (const juce::String& paramId, const void* owner);
    // Writes one parameter, in its real units: inside the gesture that is open on it, else as
    // a complete gesture of its own.
    void writeParam (const juce::String& paramId, float value);
    // Ends the gestures open on these parameters, whoever began them (a track leaving a player
    // in the middle of a drag would otherwise never get its end).
    void endGestures (const juce::StringArray& paramIds);

    // How long the output goes on after the input stops (reported to the host; also how long a
    // silent track keeps processing): with the direct sound alone, the longest delay.
    static constexpr double tailSeconds = History::maxDelaySeconds;

    // Development monitoring: processBlock's share of real time (realtime playback only).
    LoadMeter load;

private:
    std::map<juce::String, std::set<const void*>> gesturesOpen; // parameter ids and their owners, message thread only

    // The state tree is read and written on the message thread, but a host may save (copy it)
    // and load (replace it) from any thread: every access holds this, and what a reader gets
    // is a copy. Never taken on the audio thread, and never held while the host is called.
    mutable juce::CriticalSection stateTreeLock;
    juce::var stateProperty (const juce::Identifier& name, const juce::var& fallback = {}) const
    {
        const juce::ScopedLock lock (stateTreeLock);
        return state.state.getProperty (name, fallback);
    }
    void setStateProperty (const juce::Identifier& name, const juce::var& value)
    {
        const juce::ScopedLock lock (stateTreeLock);
        state.state.setProperty (name, value, nullptr);
    }
    juce::ValueTree copyOfScenes() const
    {
        const juce::ScopedLock lock (stateTreeLock);
        return state.state.getChildWithName ("scenes").createCopy();
    }

    // A parameter for the audio thread: cached (looking up by name would allocate a String),
    // and always a number - a host or a project file may hand over a NaN, which would go all
    // the way to the sound and stay in the delays and fades; the default stands in for it.
    struct AudioParam
    {
        AudioParam (juce::AudioProcessorValueTreeState& s, const char* paramId)
            : value (s.getRawParameterValue (paramId)),
              fallback (s.getParameter (paramId)->convertFrom0to1 (s.getParameter (paramId)->getDefaultValue())) {}

        float load() const noexcept
        {
            const auto v = value->load (std::memory_order_relaxed);
            return std::isfinite (v) ? v : fallback;
        }

        std::atomic<float>* value;
        float fallback;
    };

    struct AudioParams
    {
        AudioParam roomSize, listenerX, listenerY, listenerYaw, playerX, playerY, outputMode, proDistanceLaw, proDelayFrom, proDelayTo;
    };

    const AudioParams audioParams { { state, params::roomSize },   { state, params::listenerX },      { state, params::listenerY },
                                    { state, params::listenerYaw }, { state, params::playerX },       { state, params::playerY },
                                    { state, params::outputMode }, { state, params::proDistanceLaw }, { state, params::proDelayFrom },
                                    { state, params::proDelayTo } };

    // The player's mono input, and the sound's way from it to the listener's two ears.
    History history;
    DirectPath directPath;
    DelayStretch delayStretch;
    juce::int64 samplesPlayed = 0; // the song's time when the host doesn't say (since prepareToPlay)

    // Samples of silent input so far; a track silent for longer than the tail skips all work.
    int silentFor = 0, skipAfter = 0;
    bool bypassedSinceLastBlock = false; // audio thread

    double preparedRate = 0; // from prepareToPlay (the load meter's block length)

    juce::String trackName, trackColour;

    std::atomic<bool> headTrackingOn { true }; // the "headTracking" state property, for the audio thread
    static_assert (std::atomic<bool>::is_always_lock_free);

    std::atomic<float> inputPeak { 0.0f };
    static_assert (std::atomic<float>::is_always_lock_free);
    float meterLevel = 0.0f;       // message thread
    juce::uint32 meterReadAt = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComposerProcessor)
};
