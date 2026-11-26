#include "Scenes.h"

#include <map>

namespace stage
{
void setScene (ComposerProcessor& self, const juce::String& scene)
{
    if (scene.length() != 1 || ! juce::String ("ABCDEFGH").contains (scene))
        return;

    self.registry->withMembers ([&] (const juce::Array<ComposerProcessor*>& members)
    {
        // The scene's room and listener are the same for everyone; a place is each track's own.
        // A track that has never been in this scene (added later) keeps its place and takes
        // the room and listener from a track that has.
        ComposerProcessor* hasBeenThere = nullptr;
        juce::Array<ComposerProcessor*> newToScene;

        for (auto* p : members)
        {
            if (p->getScene() == scene)
            {
                if (hasBeenThere == nullptr)
                    hasBeenThere = p;
                continue;
            }

            p->storeScene();

            if (p->recallScene (scene))
                hasBeenThere = p;
            else
                newToScene.add (p);

            p->setActiveScene (scene);
        }

        if (hasBeenThere != nullptr)
            for (auto* p : newToScene)
                for (const auto& paramId : params::ofScene)
                    if (params::ofStage.contains (paramId))
                        p->writeParam (paramId, hasBeenThere->getParam (paramId));

        // A player's tracks stand in one place. They store the same place in every scene
        // (joinPlayer, then edits to all of them), unless an automation lane was edited by
        // hand: then the first track's place wins.
        std::map<juce::String, ComposerProcessor*> first;

        for (auto* p : members)
            if (const auto key = p->getPlayerKey(); key.isNotEmpty())
                if (const auto [it, isFirst] = first.emplace (key, p); ! isFirst)
                    for (const auto& paramId : params::ofPlayer)
                        if (p->getParam (paramId) != it->second->getParam (paramId))
                            p->writeParam (paramId, it->second->getParam (paramId));
    });
}
} // namespace stage
