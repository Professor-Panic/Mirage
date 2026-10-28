#include "raylib.h"
#include "rui.h"
#include "entt/entt.hpp"
#include <string>
#include <vector>

#include <sol/sol.hpp>
#include "mirage_core/inspector_ui.h"
#include "mirage_core/script_system.h"
#include "mirage_core/components.h"

#include "mirage_core/play_snapshot.h"
#include "mirage_core/runtime.h"
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
    rui::Context ui;

    // Keep the Lua state alive for the entire ScriptSystem lifetime.
    MirageCoreRuntime core;
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
    // Remembers the scene when Play starts and puts it back when Play stops.
    PlaySnapshot snapshot(core.registry, core.entityBus, core.reflection);
    Uuid selectionAtPlayStart;

    // ---------------------------------------------------------
    // Script system
    // ---------------------------------------------------------
    // Registers EntityRef and the Entities table with Lua. Must run before scripts.
    // A script that exposes a field after the Inspector list was built.
    core.scriptSystem.OnFieldsChanged([&] { ui.runtime.Invalidate("Entity_Fields"); });

    // ---------------------------------------------------------
    // UI
    // ---------------------------------------------------------
    std::string consoleText;

    ui.LoadMarkupFile("ui/main.rxml");
    ui.LoadStyleFile("ui/main.rcss");

    Inspector inspector(ui, core.registry, core.reflection, selected, core.entityBus);
    inspector.Watch<TransformComponent>();
    inspector.Watch<GameComponent>();
    inspector.Watch<ScriptComponent>();

    // ---------------------------------------------------------
    // Game component list
    // ---------------------------------------------------------

    ComponentWatch<GameComponent> watchGameList(
        core.registry,
        ui.runtime,
        "GameComponents"
    );

    ui.runtime.Bind("console_text",consoleText);
    ui.runtime.Function("refreshUI", [&]()
    {
        ui.runtime.Invalidate("GameComponents");
    });
    ui.runtime.Function("clearLog",[&](){
        core.scriptLog.Clear();
    });
    ui.runtime.BindList(
        "GameComponents",
        [&]()
        {
            std::vector<Row> rows;

            for (auto entity : core.registry.view<GameComponent>())
                rows.push_back({entity, core.entityBus.IdOf(entity).ToString()});

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
        return core.registry.get<GameComponent>(row.id);
    })
    .Ref("name", &GameComponent::name);

    // ---------------------------------------------------------
    // Test entities
    // ---------------------------------------------------------
    const auto entity1 = core.entityBus.Create();
    core.registry.emplace<GameComponent>(entity1, "object_1");
    core.registry.emplace<TransformComponent>(entity1);
    core.registry.emplace<ScriptComponent>(
        entity1,
        "scripts/test.lua"
    );

    core.registry.emplace<GameComponent>(
        core.entityBus.Create(),
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
                core.scriptLog.Clear();
                core.scriptSystem.ReloadAll();

                snapshot.Capture();
                selectionAtPlayStart = core.entityBus.IdOf(selected);
            }
            else
            {
                // Play -> Edit. The selection is tracked by Uuid because a
                // recreated entity gets a new entt handle. If the selected
                // entity was destroyed during Play its handle is already dead,
                // so fall back to what was selected when Play started.
                Uuid selectedId = core.entityBus.IdOf(selected);
                if (selectedId.IsNull()) selectedId = selectionAtPlayStart;

                core.scriptSystem.UnloadAll();   // drop Lua runtimes before entities change
                snapshot.Restore();         // numbers set during Play are forgotten
                selected = core.entityBus.Resolve(EntityRef{selectedId});
                core.scriptSystem.ReloadAll();   // fresh, not started, fields exposed

                ui.runtime.Invalidate("GameComponents");
                ui.runtime.Invalidate("Entity_Components");
                ui.runtime.Invalidate("Entity_Fields");
            }

            lastMode = mode;
        }

        if (mode == Mode::Play)
        {
            // Start each script once, then Update(dt) every frame.
            core.scriptSystem.UpdateScripts(dt);
        }
        else
        {
            // Run each script's factory so expose() registers its fields for
            // the Inspector, but never call Start() or Update().
            core.scriptSystem.EnsureLoaded();
        }
        if (core.scriptLog.ConsumeDirty()) consoleText = core.scriptLog.Text();

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
    core.scriptSystem.UnloadAll();

    CloseWindow();

    return 0;
}