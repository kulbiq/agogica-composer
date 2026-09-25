#include "StageRegistry.h"
#include "../PluginProcessor.h"

void StageRegistry::add (ComposerProcessor& p)
{
    const juce::ScopedLock sl (lock);
    members.add (&p);
}

void StageRegistry::remove (ComposerProcessor& p)
{
    const juce::ScopedLock sl (lock);
    members.removeFirstMatchingValue (&p);
}

void StageRegistry::withMembers (const std::function<void (const juce::Array<ComposerProcessor*>&)>& fn) const
{
    const juce::ScopedLock sl (lock);
    fn (members);
}

void StageRegistry::withMember (int id, const std::function<void (ComposerProcessor&)>& fn) const
{
    const juce::ScopedLock sl (lock);

    for (auto* p : members)
        if (p->id == id)
            return fn (*p);
}
