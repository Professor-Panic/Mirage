#include "raylib.h"
#include "rui.h"
#include "entt/entt.hpp"
#include <raymath.h>
#include <string>
#include <vector>
#include <filesystem>

#include <sol/sol.hpp>
#include "mirage_core/serialise.h"
#include "mirage_editor/inspector_ui.h"
#include "mirage_core/script_system.h"
#include "mirage_core/components.h"

#include "mirage_editor/player_process.h"
#include "mirage_core/line_socket.h"
#include "mirage_editor/file_dialog.h"
#include "mirage_editor/editor_menus.h"
#include "mirage_core/runtime.h"
#include "mirage_core/asset_manager.h"
#include "mirage_core/sprite_renderer.h"
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
    InitWindow(1600, 900, "Scriptable Objects");
    SetTargetFPS(60);
    // One world unit is 64 pixels. Positions AND scales are in world units.
    constexpr int kPixelsPerUnit = 64;
    rui::Context ui;

    // Keep the Lua state alive for the entire ScriptSystem lifetime.
    MirageCoreRuntime core;
    // Empty means this is a new/unsaved scene. If the starter scene exists,
    // remember its path so Save overwrites that scene rather than prompting.
    std::string scenePath;
    const std::string startupScene = "scenes/test.scene";
    {
        auto r = Mirage::LoadSceneFile(core, startupScene);
        if (!r.ok) core.scriptLog.Add("[scene] " + r.error);
        else scenePath = startupScene;
        for (const auto& w : r.warnings) core.scriptLog.Add("[scene] " + w);
    }
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
    // Play runs the scene in a separate Mirage-Player process, so the editor's
    // scene is never touched while playing (no snapshot / restore needed).
    PlayerProcess player;
    const std::string playerExe  = "build-player/Mirage-Player.exe";  // relative to the project root
    const std::string playScene  = ".mirage/play.scene";              // temp copy handed to the player

    // Local connection from the player (it connects to a free port we pick).
    LineListener listener;
    LineSocket   playerConn;
    {
        std::string err;
        if (!listener.Listen(0, &err))
            core.scriptLog.Add("[play] cannot open a local port: " + err);
    }

    // One message from the player. Game -> editor: log, error, ready.
    const auto handlePlayerLine = [&](const std::string& line)
    {
        const nlohmann::json m = nlohmann::json::parse(line, nullptr, false);
        if (!m.is_object() || !m.contains("type") || !m["type"].is_string()) return;

        const std::string type = m["type"].get<std::string>();
        const std::string text = (m.contains("text") && m["text"].is_string())
                                     ? m["text"].get<std::string>() : std::string{};

        if      (type == "log")   core.scriptLog.Add(text);
        else if (type == "error") core.scriptLog.Add("[error] " + text);
        else if (type == "ready") core.scriptLog.Add("[play] player ready");
    };

    core.scriptSystem.OnFieldsChanged([&] { ui.runtime.Invalidate("Entity_Fields"); });

    // ---------------------------------------------------------
    // UI
    // ---------------------------------------------------------
    std::string consoleText;

    ui.LoadMarkupFile("ui/main.rxml");
    ui.LoadStyleFile("ui/main.rcss");

    // RUI-backed file picker. Update it once per frame before ui.Update().
    FileDialog fileDialog(ui);
    int fileMenuOpen = 0;
    ui.runtime.Bind("file_menu_open", fileMenuOpen);
    // Changing a bound value does not rebuild RUI's conditional markup by itself.
    // Explicitly request a rebuild so the File dropdown's `if` becomes visible.
    ui.runtime.Function("toggleFileMenu", [&]()
    {
        fileMenuOpen = fileMenuOpen == 1 ? 0 : 1;
        ui.RequestRebuild();
    });
    ui.runtime.Function("deleteEntity",[&](){
        core.entityBus.Destroy(core.entityBus.FindByName(core.entityBus.NameOf(selected)));
        ui.RequestRebuild();
    });
    Inspector inspector(ui, core.registry, core.reflection, selected, core.entityBus);
    inspector.Watch<TransformComponent>();
    inspector.Watch<GameComponent>();
    inspector.Watch<ScriptComponent>();
    inspector.Watch<SpriteComponent>();

    // ---------------------------------------------------------
    // Right-click menus and the Add Component dropdown
    // ---------------------------------------------------------
    EditorMenus menus(ui, core.registry, core.reflection, core.entityBus, selected);

    // A modal file dialog is open: don't pop menus over it.
    menus.SetBlockedCheck([&]() { return fileDialog.IsOpen(); });
    menus.OnMessage([&](const std::string& message) { core.scriptLog.Add(message); });

    // "Add Component > Script" asks which .lua file to attach. Choosing another
    // file adds another script to the same entity.
    menus.OnAddScript([&](entt::entity target)
    {
        fileDialog.Open(".lua", "scripts", [&, target](const std::string& path)
        {
            if (!core.registry.valid(target)) return;

            if (!core.registry.get_or_emplace<ScriptComponent>(target).Add(path))
            {
                core.scriptLog.Add("[editor] " + path + " is already attached");
                return;
            }

            // Adding a script to an existing component fires no entt signal.
            ui.runtime.Invalidate("Entity_Components");
            ui.runtime.Invalidate("Entity_Fields");
        }, "Add Script");
    });

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
    const auto sceneDirectory = [&]() -> std::string
    {
        if (!scenePath.empty())
        {
            const auto parent = std::filesystem::path(scenePath).parent_path();
            if (!parent.empty()) return parent.generic_string();
        }
        return "scenes";
    };

    const auto saveSceneToPath = [&](const std::string& path)
    {
        if (mode == Mode::Play) return;
        std::string err;
        if (Mirage::SaveSceneFile(core, path, &err))
        {
            scenePath = path;
            core.scriptLog.Add("[scene] saved " + scenePath);
        }
        else
        {
            core.scriptLog.Add("[scene] save failed: " + err);
        }
    };

    ui.runtime.Function("openSceneDialog", [&]()
    {
        if (mode == Mode::Play) return; // never swap a scene while playing
        fileMenuOpen = 0;
        ui.RequestRebuild();
        fileDialog.Open(".scene", sceneDirectory(), [&](const std::string& path)
        {
            // Preserve selection by stable UUID across the scene replacement.
            const Uuid selectedId = core.entityBus.IdOf(selected);
            auto r = Mirage::LoadSceneFile(core, path);
            if (!r.ok)
            {
                core.scriptLog.Add("[scene] open failed: " + r.error);
                return;
            }
            for (const auto& w : r.warnings) core.scriptLog.Add("[scene] " + w);
            scenePath = path;

            selected = core.entityBus.Resolve(EntityRef{selectedId});
            if (selected == entt::null)
            {
                auto games = core.registry.view<GameComponent>();
                if (!games.empty()) selected = *games.begin();
            }

            ui.runtime.Invalidate("GameComponents");
            ui.runtime.Invalidate("Entity_Components");
            ui.runtime.Invalidate("Entity_Fields");
        });
    });

    ui.runtime.Function("saveScene", [&]()
    {
        if (mode == Mode::Play) return; // Play has changed the values
        fileMenuOpen = 0;
        ui.RequestRebuild();
        if (scenePath.empty())
        {
            const std::string defaultName = "untitled.scene";
            fileDialog.Save(".scene", sceneDirectory(), defaultName,
                [&](const std::string& path) { saveSceneToPath(path); });
            return;
        }
        saveSceneToPath(scenePath);
    });

    ui.runtime.Function("saveSceneAs", [&]()
    {
        if (mode == Mode::Play) return;
        fileMenuOpen = 0;
        ui.RequestRebuild();
        std::string defaultName = scenePath.empty()
            ? "untitled.scene"
            : std::filesystem::path(scenePath).filename().string();
        fileDialog.Save(".scene", sceneDirectory(), defaultName,
            [&](const std::string& path) { saveSceneToPath(path); });
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
    // Only create the demo scene when nothing was loaded.
    if (core.registry.view<IdComponent>().empty())
    {
        const auto entity1 = core.entityBus.Create();
        const auto entity2=core.entityBus.Create();
        core.registry.emplace<GameComponent>(entity1, "object_1");
        core.registry.emplace<TransformComponent>(entity1);
        core.registry.emplace<ScriptComponent>(
            entity1,
            "scripts/test.lua"
        );
        core.registry.emplace<SpriteComponent>(
            entity1
        );

        core.registry.emplace<GameComponent>(
            entity2,
            "object_2"
        );
        core.registry.emplace<ScriptComponent>(
            entity2,
            "scripts/spawner.lua"
        );
    }

    {
        auto games = core.registry.view<GameComponent>();
        if (!games.empty()) selected = *games.begin();
    }

    // The markup was mounted before all bindings were registered.
    ui.RequestRebuild();
    AssetManager assetManager;
    SpriteRenderer spriteRenderer=SpriteRenderer(assetManager);

    // ---------------------------------------------------------
    // Scene viewport (the <viewport> in ui/main.rxml)
    // ---------------------------------------------------------
    // The viewport owns pan (middle mouse) and wheel zoom, and clips drawing to
    // its panel. Its draw handlers run inside its transform, so everything
    // drawn here is in world pixels (world units * kPixelsPerUnit).
    //
    // Markup: <viewport id="scene_view" minZoom="0.1" maxZoom="10"
    //                   onUpdate="sceneViewUpdate"
    //                   onBackgroundDraw="sceneViewDraw"/>
    bool sceneViewCentered = false;
    ui.runtime.Function("sceneViewUpdate", [&](const rui::Event& e)
    {
        auto* view = static_cast<rui::Viewport*>(e.native_widget);
        if (!view) return;

        // Show world (0,0) at the panel centre, like the old Camera2D did.
        // Done once, as soon as layout has given the panel a real size.
        // (A markup rebuild keeps the camera, so this does not repeat.)
        if (!sceneViewCentered && view->rect.transform.width > 1.0f)
        {
            view->CenterOn({ 0.0f, 0.0f });
            sceneViewCentered = true;
        }

        // Later: picking, hover and gizmo dragging go here, using
        // view->world_mouse (world pixels), view->hovered and
        // view->camera.zoom. Divide by kPixelsPerUnit for world units.
    });
    ui.runtime.Function("sceneViewDraw", [&](const rui::Event& e)
    {
        const auto* view = static_cast<const rui::Viewport*>(e.native_widget);
        const float zoom = view ? view->camera.zoom : 1.0f;

        // No ClearBackground here: that would wipe the whole window. The panel's
        // background comes from the viewport's RCSS (background-color).
        DrawRectangleLinesEx({0, 0, 1600, 900}, 2.0f / zoom, WHITE);
        spriteRenderer.Draw(core.registry, kPixelsPerUnit, kPixelsPerUnit);
    });

    // ---------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------

    while (!WindowShouldClose())
    {
        // Messages from the player. Read before the exit check below, so the
        // last lines it printed are not lost.
        if (mode == Mode::Play)
        {
            if (!playerConn.Connected()) listener.Accept(playerConn);
            for (const std::string& line : playerConn.Poll()) handlePlayerLine(line);
        }

        // The player closed itself (its window, a crash, a script error):
        // drop back to Edit. Only once it has really been launched
        // (lastMode == Play), or this would fire in the frame Play is pressed.
        if (mode == Mode::Play && lastMode == Mode::Play && !player.IsRunning())
        {
            core.scriptLog.Add("[play] player exited (code " +
                               std::to_string(player.LastExitCode()) + ")");
            mode = Mode::Edit;
        }

        if (mode != lastMode)
        {
            if (mode == Mode::Play)
            {
                // Edit -> Play: save the scene to a temp file and launch the player on it.
                core.scriptLog.Clear();
                playerConn.Close();

                std::string err;
                bool ok = std::filesystem::exists(playerExe);
                if (!ok) err = "player not found: " + playerExe + " (build it with run-player.bat)";
                if (ok)  ok = Mirage::SaveSceneFile(core, playScene, &err);
                if (ok)  ok = player.Start(std::filesystem::absolute(playerExe).make_preferred().string(),
                                           std::filesystem::absolute(playScene).make_preferred().string(),
                                           std::filesystem::current_path().string(),
                                           listener.Port(), &err);
                if (ok)
                    core.scriptLog.Add("[play] player started");
                else
                {
                    core.scriptLog.Add("[play] " + err);
                    mode = Mode::Edit;          // could not start: stay in Edit
                }
            }
            else
            {
                // Play -> Edit (Stop pressed, or the player exited).
                for (const std::string& line : playerConn.Poll()) handlePlayerLine(line);
                player.Stop();
                playerConn.Close();
            }

            lastMode = mode;
        }
        core.scriptSystem.EnsureLoaded();
        if (core.scriptLog.ConsumeDirty()) consoleText = core.scriptLog.Text();

        // Refresh inspector data after scripts have run.
        inspector.Update();
        fileDialog.Update();
        menus.Update();

        ui.Update({
            static_cast<float>(GetScreenWidth()),
            static_cast<float>(GetScreenHeight())
        });
        BeginDrawing();
        ClearBackground(BLACK);
        ui.Draw();

        EndDrawing();
    }
    player.Stop();
    // Release Lua script runtimes before the Lua state is destroyed.
    core.scriptSystem.UnloadAll();

    CloseWindow();

    return 0;
}