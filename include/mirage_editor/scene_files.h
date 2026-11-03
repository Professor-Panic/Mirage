#pragma once
#include <filesystem>
#include <string>
#include <utility>

#include "rui.h"
#include "mirage_core/components.h"
#include "mirage_core/runtime.h"
#include "mirage_core/serialise.h"
#include "mirage_editor/editor_state.h"
#include "mirage_editor/file_dialog.h"

// Loads the scene the editor starts with. Returns its path, or "" when it
// could not be loaded (then the next Save asks for a name).
inline std::string LoadStartupScene(MirageCoreRuntime& core, const std::string& startupScene)
{
    std::string scenePath;
    auto r = Mirage::LoadSceneFile(core, startupScene);
    if (!r.ok) core.scriptLog.Add("[scene] " + r.error);
    else scenePath = startupScene;
    for (const auto& w : r.warnings) core.scriptLog.Add("[scene] " + w);
    return scenePath;
}

// File menu: open, save, save as. Owns the current scene path.
class SceneFiles
{
public:
    // `scenePath`: empty means a new/unsaved scene. Otherwise Save overwrites
    // that scene instead of prompting.
    SceneFiles(rui::Context& ui, MirageCoreRuntime& core, EditorState& state,
               FileDialog& fileDialog, std::string scenePath)
        : ui_(ui), core_(core), state_(state), fileDialog_(fileDialog),
          scenePath_(std::move(scenePath))
    {
        ui_.runtime.Bind("file_menu_open", fileMenuOpen_);

        // Changing a bound value does not rebuild RUI's conditional markup by itself.
        // Explicitly request a rebuild so the File dropdown's `if` becomes visible.
        ui_.runtime.Function("toggleFileMenu", [this]()
        {
            fileMenuOpen_ = fileMenuOpen_ == 1 ? 0 : 1;
            ui_.RequestRebuild();
        });

        ui_.runtime.Function("openSceneDialog", [this]() { Open(); });
        ui_.runtime.Function("saveScene",       [this]() { Save(); });
        ui_.runtime.Function("saveSceneAs",     [this]() { SaveAs(); });
    }

    SceneFiles(const SceneFiles&) = delete;
    SceneFiles& operator=(const SceneFiles&) = delete;

    const std::string& Path() const { return scenePath_; }

private:
    std::string Directory() const
    {
        if (!scenePath_.empty())
        {
            const auto parent = std::filesystem::path(scenePath_).parent_path();
            if (!parent.empty()) return parent.generic_string();
        }
        return "scenes";
    }

    void SaveToPath(const std::string& path)
    {
        if (state_.mode == Mode::Play) return;
        std::string err;
        if (Mirage::SaveSceneFile(core_, path, &err))
        {
            scenePath_ = path;
            core_.scriptLog.Add("[scene] saved " + scenePath_);
        }
        else
        {
            core_.scriptLog.Add("[scene] save failed: " + err);
        }
    }

    void Open()
    {
        if (state_.mode == Mode::Play) return;   // never swap a scene while playing
        fileMenuOpen_ = 0;
        ui_.RequestRebuild();
        fileDialog_.Open(".scene", Directory(), [this](const std::string& path)
        {
            // Preserve selection by stable UUID across the scene replacement.
            const Uuid selectedId = core_.entityBus.IdOf(state_.selected);
            auto r = Mirage::LoadSceneFile(core_, path);
            if (!r.ok)
            {
                core_.scriptLog.Add("[scene] open failed: " + r.error);
                return;
            }
            for (const auto& w : r.warnings) core_.scriptLog.Add("[scene] " + w);
            scenePath_ = path;

            state_.selected = core_.entityBus.Resolve(EntityRef{selectedId});
            if (state_.selected == entt::null)
            {
                auto games = core_.registry.view<GameComponent>();
                if (!games.empty()) state_.selected = *games.begin();
            }

            ui_.runtime.Invalidate("GameComponents");
            ui_.runtime.Invalidate("Entity_Components");
            ui_.runtime.Invalidate("Entity_Fields");
        });
    }

    void Save()
    {
        if (state_.mode == Mode::Play) return;   // Play has changed the values
        fileMenuOpen_ = 0;
        ui_.RequestRebuild();
        if (scenePath_.empty())
        {
            fileDialog_.Save(".scene", Directory(), "untitled.scene",
                [this](const std::string& path) { SaveToPath(path); });
            return;
        }
        SaveToPath(scenePath_);
    }

    void SaveAs()
    {
        if (state_.mode == Mode::Play) return;
        fileMenuOpen_ = 0;
        ui_.RequestRebuild();
        const std::string defaultName = scenePath_.empty()
            ? "untitled.scene"
            : std::filesystem::path(scenePath_).filename().string();
        fileDialog_.Save(".scene", Directory(), defaultName,
            [this](const std::string& path) { SaveToPath(path); });
    }

    rui::Context&      ui_;
    MirageCoreRuntime& core_;
    EditorState&       state_;
    FileDialog&        fileDialog_;
    std::string        scenePath_;
    int                fileMenuOpen_ = 0;   // the UI is bound to this
};
