#include "raylib.h"
#include "rui.h"

#include "mirage_core/asset_manager.h"
#include "mirage_core/runtime.h"
#include "mirage_core/lua_api_codegen.h"
#include "mirage_core/sprite_renderer.h"

#include "mirage_editor/console_ui.h"
#include "mirage_editor/demo_scene.h"
#include "mirage_editor/editor_menus.h"
#include "mirage_editor/editor_state.h"
#include "mirage_editor/file_dialog.h"
#include "mirage_editor/hierarchy_ui.h"
#include "mirage_editor/inspector_ui.h"
#include "mirage_editor/play_session.h"
#include "mirage_editor/scene_files.h"
#include "mirage_editor/viewport_tools.h"

#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    // Generate LuaLS definitions without creating a Raylib window. The runtime
    // constructor builds the complete reflection + Script API Registry, then
    // the code generator writes the same API metadata used by Sol2 at runtime.
    if (argc >= 2 && std::string(argv[1]) == "--generate-lua-api")
    {
        const std::filesystem::path outputPath =
            (argc >= 3 && argv[2][0] != '-') ? std::filesystem::path(argv[2])
                                             : std::filesystem::path("_engine.lua");
        try
        {
            MirageCoreRuntime core;
            LuaApiCodeGenerator generator;
            const bool changed = generator.WriteFile(core.scriptApi, outputPath);
            std::cout << (changed ? "Generated " : "Already up to date: ")
                      << outputPath.string() << '\n';
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << "Lua API generation failed: " << error.what() << '\n';
            return 1;
        }
    }

    if (argc >= 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))
    {
        std::cout << "Mirage editor options:\n"
                  << "  --generate-lua-api [output-path]  Generate LuaLS definitions (default: _engine.lua)\n"
                  << "  --help                            Show this help\n";
        return 0;
    }

    // ---------------------------------------------------------
    // Engine
    // ---------------------------------------------------------
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1920, 1080, "Scriptable Objects");
    SetWindowPosition(0,0);
    MaximizeWindow();
    SetTargetFPS(60);

    rui::Context ui;
    MirageCoreRuntime core;                 // keeps the Lua state alive for the ScriptSystem

    const std::string scenePath = LoadStartupScene(core, "scenes/test.scene");

    // State the markup reads and writes (`selected`, `mode`).
    EditorState state;
    ui.runtime.Bind("selected", state.selected);
    ui.runtime.Bind("mode", state.mode);
    ui.runtime.Bind("tool", state.tool);      // W / E / R in ViewportTools; the markup may set it too

    core.scriptSystem.OnFieldsChanged([&] { ui.runtime.Invalidate("Entity_Fields"); });

    ui.LoadMarkupFile("ui/main.rxml");
    ui.LoadStyleFile("ui/main.rcss");

    // ---------------------------------------------------------
    // Editor parts. Each registers its own bindings in its constructor.
    // ---------------------------------------------------------
    FileDialog fileDialog(ui);              // RUI-backed file picker; Update() once per frame
    SceneFiles scenes(ui, core, state, fileDialog, scenePath);
    ConsoleUi  console(ui, core);

    Inspector inspector(ui, core.registry, core.reflection, state.selected, core.entityBus);
    inspector.Watch<TransformComponent>();
    inspector.Watch<GameComponent>();
    inspector.Watch<ScriptComponent>();
    inspector.Watch<SpriteComponent>();
    inspector.Watch<TagsComponent>();

    // Right-click menus and the Add Component dropdown.
    EditorMenus menus(ui, core.registry, core.reflection, core.entityBus, state.selected);
    menus.SetBlockedCheck([&]() { return fileDialog.IsOpen(); });   // no menus over a modal dialog
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

    HierarchyUi hierarchy(ui, core, state);
    PlaySession play(core, state);

    AssetManager   assets;
    SpriteRenderer sprites(assets);
    ViewportTools  viewport(ui, core, state, sprites);

    // ---------------------------------------------------------
    // Starting scene
    // ---------------------------------------------------------
    CreateDemoSceneIfEmpty(core);
    SelectFirstGame(core, state);

    // The markup was mounted before all bindings were registered.
    ui.RequestRebuild();

    // ---------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------
    while (!WindowShouldClose())
    {
        play.Update();                      // player messages, Edit <-> Play
        core.scriptSystem.EnsureLoaded();
        console.Update();
        if(IsKeyPressed(KEY_F11)){
            ToggleFullscreen();
        }
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

    play.Shutdown();
    // Release Lua script runtimes before the Lua state is destroyed.
    core.scriptSystem.UnloadAll();

    CloseWindow();
    return 0;
}
