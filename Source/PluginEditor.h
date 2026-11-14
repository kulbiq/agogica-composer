#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// Hosts the web UI; all UI logic lives in ui/.
class ComposerEditor final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit ComposerEditor (ComposerProcessor&);

    void resized() override;

private:
    struct Browser final : juce::WebBrowserComponent
    {
        using WebBrowserComponent::WebBrowserComponent;

        // The UI is a single page; never navigate anywhere else.
        bool pageAboutToLoad (const juce::String& url) override;

        // Events sent before the page's bridge exists are lost (and assert).
        void pageFinishedLoading (const juce::String&) override   { onPageLoaded(); }

        std::function<void()> onPageLoaded;
    };

    void timerCallback() override;

    // Opens an app by bundle id without waiting on the host's message thread. macOS: `open -b`,
    // and the timer completes the call when `open` has finished (or after 5 s), true if it worked.
    // Windows: the installed app's exe (bridge/Apps.h), started at once; only the Hub for now.
    // `ifFailed` runs before completing with false.
    void openApp (const juce::String& bundleId, juce::WebBrowserComponent::NativeFunctionCompletion,
                  std::function<void()> ifFailed = {});
    void finishOpening();

    struct Opening
    {
        std::unique_ptr<juce::ChildProcess> process;
        juce::WebBrowserComponent::NativeFunctionCompletion complete;
        std::function<void()> ifFailed;
        juce::uint32 startedAt;
    };
    std::vector<Opening> opening;

    ComposerProcessor& composer;
    Browser browser;
    juce::String lastStage;
    float lastHeadYaw = 0.0f; // the page starts at 0
    int loadTicks = 0; // the DSP load goes out every 8th tick

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComposerEditor)
};
