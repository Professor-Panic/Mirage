#pragma once
#include "entt/entt.hpp"
#include "mirage_core/components.h"
#include "mirage_core/runtime.h"
#include "mirage_editor/editor_state.h"

// Only creates the demo scene when nothing was loaded.
inline void CreateDemoSceneIfEmpty(MirageCoreRuntime& core)
{
    if (!core.registry.view<IdComponent>().empty()) return;

    const auto entity1 = core.entityBus.Create();
    const auto entity2 = core.entityBus.Create();

    core.registry.emplace<GameComponent>(entity1, "object_1");
    core.registry.emplace<TransformComponent>(entity1);
    core.registry.emplace<ScriptComponent>(entity1, "scripts/test.lua");
    core.registry.emplace<SpriteComponent>(entity1);

    core.registry.emplace<GameComponent>(entity2, "object_2");
    core.registry.emplace<ScriptComponent>(entity2, "scripts/spawner.lua");
}

// Selects the first entity that has a GameComponent (if there is one).
inline void SelectFirstGame(MirageCoreRuntime& core, EditorState& state)
{
    auto games = core.registry.view<GameComponent>();
    if (!games.empty()) state.selected = *games.begin();
}
