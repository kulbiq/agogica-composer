#pragma once

#include <juce_core/juce_core.h>
#include <functional>

class ComposerProcessor;

// Every Composer instance in this process, so each editor can show the whole ensemble.
// ponytail: one stage per process - two projects open in the same host process (e.g.
// Reaper tabs) see each other's tracks. Add a per-project key if that matters.
// Held through juce::SharedResourcePointer: created with the first instance, gone with
// the last. Instances in other processes (sandboxed hosts, AU vs VST3) never meet.
//
// Rule: a pointer to another instance is only used inside these callbacks, while the
// lock is held; instances unregister under the same lock before they are destroyed.
// Message-thread data only - the audio thread never touches the registry.
class StageRegistry
{
public:
    void add (ComposerProcessor&);
    void remove (ComposerProcessor&);

    // All members, ordered by id (i.e. insertion order).
    void withMembers (const std::function<void (const juce::Array<ComposerProcessor*>&)>&) const;

    // Calls `fn` only if instance `id` still exists.
    void withMember (int id, const std::function<void (ComposerProcessor&)>& fn) const;

private:
    juce::CriticalSection lock;
    juce::Array<ComposerProcessor*> members;
};
