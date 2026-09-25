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

    ComposerProcessor& composer;

    // Relays must exist before the browser that takes options from them.
    juce::WebSliderRelay roomSizeRelay { "roomSize" };

    Browser browser;

    juce::WebSliderParameterAttachment roomSizeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ComposerEditor)
};
