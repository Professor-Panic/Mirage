#include "raylib.h"
#include "rui.h"
#include "entt.hpp"
#include <string>
#include <vector>

#include <sol/sol.hpp>

#include "reflection.h"
#include "inspector_ui.h"
#include "script_system.h"
#include "components.h"
#include "entity_bus.h"
#include "entity_lua.h"
#include "play_snapshot.h"
#include "script_log.h"
#include "lua_input.h"
// One row per entity that has a GameComponent.
struct Row
{
    entt::entity id;
    std::string  uuid;   // stable id the hierarchy hands to a drop target
};

// Bound to the UI as `mode`. The markup assigns the numbers: Edit = 0, Play = 1.
enum class Mode : int { Edit = 0, Play = 1 };

int main()
{
    // ---------------------------------------------------------
    // Initialize engine
    // ---------------------------------------------------------

    InitWindow(1200, 800, "Scriptable Objects");
    SetTargetFPS(60);

    entt::registry registry;
    EntityBus entityBus(registry);   // after the registry, so it dies first
    rui::Context ui;

    // Keep the Lua state alive for the entire ScriptSystem lifetime.
    sol::state lua;
    lua.open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table
    );

    entt::entity selected = entt::null;
    ui.runtime.Bind("selected", selected);

    // The UI writes this directly (onClick="mode = 1"); the main loop
    // notices the change and reloads the scripts.
    Mode mode = Mode::Edit;
    Mode lastMode = mode;
    ui.runtime.Bind("mode", mode);

    // ---------------------------------------------------------
    // Reflection registration
    // ---------------------------------------------------------

    Reflection reflection;

    reflection.Component<TransformComponent>("Transform")
        .Field("position", &TransformComponent::position)
        .Field("scale", &TransformComponent::scale)
        .Field("rotation", &TransformComponent::rotation);

    reflection.Component<GameComponent>("Game")
        .Field("name", &GameComponent::name)
        .DefaultArgs(std::string("GameComponent"));
    reflection.Component<ScriptComponent>("Script");

    // Remembers the scene when Play starts and puts it back when Play stops.
    PlaySnapshot snapshot(registry, entityBus, reflection);
    Uuid selectionAtPlayStart;

    // ---------------------------------------------------------
    // Script system
    // ---------------------------------------------------------

    entityBus.SetNameProvider(
        [](const entt::registry& r, entt::entity e) -> std::string
        {
            if (const auto* g = r.try_get<GameComponent>(e))
                return g->name;
            return {};
        });

    // Registers EntityRef and the Entities table with Lua. Must run before scripts.
    BindEntityLua(lua, entityBus, reflection);
    ScriptLog scriptLog;
    BindLuaPrint(lua, scriptLog);
    BindLuaInput(lua,scriptLog);
    ScriptSystem scriptSystem(lua, reflection, registry, entityBus);

    // A script that exposes a field after the Inspector list was built.
    scriptSystem.OnFieldsChanged([&] { ui.runtime.Invalidate("Entity_Fields"); });

    // ---------------------------------------------------------
    // UI
    // ---------------------------------------------------------
    std::string consoleText;

    ui.LoadMarkupFile("ui/main.rxml");
    ui.LoadStyleFile("ui/main.rcss");

    Inspector inspector(ui, registry, reflection, selected, entityBus);
    inspector.Watch<TransformComponent>();
    inspector.Watch<GameComponent>();
    inspector.Watch<ScriptComponent>();

    // ---------------------------------------------------------
    // Game component list
    // ---------------------------------------------------------

    ComponentWatch<GameComponent> watchGameList(
        registry,
        ui.runtime,
        "GameComponents"
    );

    ui.runtime.Bind("console_text",consoleText);
    ui.runtime.Function("refreshUI", [&]()
    {
        ui.runtime.Invalidate("GameComponents");
    });
    ui.runtime.Function("clearLog",[&](){
        scriptLog.Clear();
    });
    ui.runtime.BindList(
        "GameComponents",
        [&]()
        {
            std::vector<Row> rows;

            for (auto entity : registry.view<GameComponent>())
                rows.push_back({entity, entityBus.IdOf(entity).ToString()});

            return rows;
        },
        [](auto& list)
        {
            list.Field("id", &Row::id)
                .Field("uuid", &Row::uuid);
        }
    )
    .Via([&](Row& row) -> GameComponent&
    {
        return registry.get<GameComponent>(row.id);
    })
    .Ref("name", &GameComponent::name);

    // ---------------------------------------------------------
    // Test entities
    // ---------------------------------------------------------

    const auto entity1 = entityBus.Create();

    registry.emplace<GameComponent>(entity1, "object_1");
    registry.emplace<TransformComponent>(entity1);
    registry.emplace<ScriptComponent>(
        entity1,
        "scripts/test.lua"
    );

    registry.emplace<GameComponent>(
        entityBus.Create(),
        "object_2"
    );

    selected = entity1;

    // The markup was mounted before all bindings were registered.
    ui.RequestRebuild();

    // ---------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------

    while (!WindowShouldClose())
    {
        const float dt = GetFrameTime();

        if (mode != lastMode)
        {
            if (mode == Mode::Play)
            {
                // Edit -> Play. Reload first (picks up script edits from disk
                // and exposes any new fields), then snapshot the scene as it
                // is the moment Play begins.
                scriptLog.Clear();
                scriptSystem.ReloadAll();

                snapshot.Capture();
                selectionAtPlayStart = entityBus.IdOf(selected);
            }
            else
            {
                // Play -> Edit. The selection is tracked by Uuid because a
                // recreated entity gets a new entt handle. If the selected
                // entity was destroyed during Play its handle is already dead,
                // so fall back to what was selected when Play started.
                Uuid selectedId = entityBus.IdOf(selected);
                if (selectedId.IsNull()) selectedId = selectionAtPlayStart;

                scriptSystem.UnloadAll();   // drop Lua runtimes before entities change
                snapshot.Restore();         // numbers set during Play are forgotten
                selected = entityBus.Resolve(EntityRef{selectedId});
                scriptSystem.ReloadAll();   // fresh, not started, fields exposed

                ui.runtime.Invalidate("GameComponents");
                ui.runtime.Invalidate("Entity_Components");
                ui.runtime.Invalidate("Entity_Fields");
            }

            lastMode = mode;
        }

        if (mode == Mode::Play)
        {
            // Start each script once, then Update(dt) every frame.
            scriptSystem.UpdateScripts(dt);
        }
        else
        {
            // Run each script's factory so expose() registers its fields for
            // the Inspector, but never call Start() or Update().
            scriptSystem.EnsureLoaded();
        }
        if (scriptLog.ConsumeDirty()) consoleText = scriptLog.Text();

        // Refresh inspector data after scripts have run.
        inspector.Update();

        ui.Update({
            static_cast<float>(GetScreenWidth()),
            static_cast<float>(GetScreenHeight())
        });
        BeginDrawing();
        ClearBackground(BLACK);

        ui.Draw();

        EndDrawing();
    }

    // Release Lua script runtimes before the Lua state is destroyed.
    scriptSystem.UnloadAll();

    CloseWindow();

    return 0;
}