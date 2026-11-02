#pragma once
// ---------------------------------------------------------------------------
// EditorMenus - the editor's pop-up menus, drawn with RUI.
//
//   * Right-click in the hierarchy -> a small menu with "Create Entity".
//   * Right-click in the inspector, or the "Add Component" button at the end
//     of the inspector -> a dropdown with a search box and one button per
//     component the selected entity can still receive.
//
//   EditorMenus menus(ui, registry, reflection, entityBus, selected);
//   menus.SetBlockedCheck([&] { return fileDialog.IsOpen(); });   // optional
//   menus.OnAddScript([&](entt::entity target) { ...pick a file... });
//   ...
//   menus.Update();          // every frame, before ui.Update()
//
// The markup side (main.rxml) talks to this class through:
//   hier_menu_open  add_menu_open  add_filter          (bound values)
//   createEntity  openAddMenu  addComponentChoice      (functions)
//   AddComponentChoices                                (list rows: name, cls)
//
// Positioning: RUI has no way to bind a position into markup, so when a menu
// opens this class writes `.mirage/menu_position.rcss` (left/top/width/height
// for .hier-menu and .add-menu) and reloads the stylesheet. If that ever fails
// the menus simply show up at the defaults in main.rcss.
//
// The panel regions used to decide where a right-click lands mirror main.rcss
// (.hierachy, .inspector); keep the constants below in sync with it.
// ---------------------------------------------------------------------------

#include "raylib.h"
#include "rui.h"
#include "entt/entt.hpp"
#include "mirage_core/components.h"
#include "mirage_core/entity_bus.h"
#include "mirage_core/reflection.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

class EditorMenus
{
public:
    // One component the dropdown can offer.
    struct ChoiceRow
    {
        std::string name;
    };

    EditorMenus(rui::Context& ui,
                entt::registry& registry,
                const Reflection& reflection,
                EntityBus& bus,
                entt::entity& selected,
                std::string baseStyle = "ui/main.rcss",
                std::string positionStyle = ".mirage/menu_position.rcss")
        : ui_(ui),
          registry_(registry),
          reflection_(reflection),
          bus_(bus),
          selected_(selected),
          baseStyle_(std::move(baseStyle)),
          positionStyle_(std::move(positionStyle))
    {
        auto& rt = ui_.runtime;

        rt.Bind("hier_menu_open", hierOpen_);
        rt.Bind("add_menu_open", addOpen_);
        rt.Bind("add_filter", filter_);

        rt.Function("createEntity", [this]() { CreateEntity(); });
        rt.Function("openAddMenu", [this]() { OpenAdd(GetMousePosition()); });
        rt.Function("closeMenus", [this]() { CloseAll(); });

        // Every registered component is a row; the ones that don't apply right
        // now (already on the entity, or filtered out by the search box) get the
        // class "add-menu-hidden" (display: none). `cls` is re-read every frame
        // on a sync="true" button, so typing in the search box filters the list
        // without rebuilding the UI (a rebuild would drop the box's focus).
        rt.BindList(
            "AddComponentChoices",
            [this]()
            {
                std::vector<ChoiceRow> rows;
                for (const auto& entry : reflection_.All())
                    rows.push_back({entry.first});
                return rows;
            },
            [this](auto& list)
            {
                list.Field("name", &ChoiceRow::name)
                    .template Property<std::string>(
                        "cls",
                        [this](ChoiceRow& row) -> std::string
                        {
                            return Hidden(row.name) ? "add-menu-hidden" : "";
                        },
                        [](ChoiceRow&, std::string) {});
            });

        rt.ItemFunction<ChoiceRow>(
            "addComponentChoice",
            [this](ChoiceRow& row) { AddComponent(row.name); });
    }

    EditorMenus(const EditorMenus&) = delete;
    EditorMenus& operator=(const EditorMenus&) = delete;

    // While this returns true (a modal dialog is open) no menu opens and any
    // open one is closed.
    void SetBlockedCheck(std::function<bool()> blocked) { blocked_ = std::move(blocked); }

    // "Script" does not add a component with defaults: the editor asks the user
    // which file to attach. This is called with the entity that was selected.
    void OnAddScript(std::function<void(entt::entity)> pick) { pickScript_ = std::move(pick); }

    // Optional: where messages such as "cannot add X" go (the console).
    void OnMessage(std::function<void(const std::string&)> log) { log_ = std::move(log); }

    bool AnyOpen() const { return hierOpen_ == 1 || addOpen_ == 1; }

    // Call once per frame, before ui.Update().
    void Update()
    {
        if (blocked_ && blocked_())
        {
            if (AnyOpen()) CloseAll();
            return;
        }

        const Vector2 mouse = GetMousePosition();

        // Close on Escape, or on a click outside the open menu.
        if (AnyOpen())
        {
            const bool click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                               IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
            const Rectangle area = hierOpen_ == 1 ? HierRect() : AddRect();

            if (IsKeyPressed(KEY_ESCAPE) ||
                (click && !CheckCollisionPointRec(mouse, area)))
                CloseAll();
        }

        // Right-click opens a menu for the panel under the cursor.
        if (!AnyOpen() && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        {
            if (InHierarchy(mouse))      OpenHierarchy(mouse);
            else if (InInspector(mouse)) OpenAdd(mouse);
        }
    }

private:
    // --- layout constants (mirror main.rcss) --------------------------------

    static constexpr float kHierarchyRight = 0.15f;   // .hierachy  width: 15%
    static constexpr float kInspectorLeft  = 0.80f;   // .inspector left: 80%
    static constexpr float kPanelsTop      = 0.10f;   // .hierachy/.inspector top: 10%
    static constexpr float kPanelsBottom   = 0.70f;   // the console starts at 70%

    // Written into the position stylesheet, and used for the "click outside" test.
    static constexpr float kHierW = 170.0f, kHierH = 44.0f;
    static constexpr float kAddW  = 240.0f, kAddH  = 300.0f;

    static constexpr const char* kScriptName = "Script";

    // --- helpers -------------------------------------------------------------

    static std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static std::string Trim(const std::string& s)
    {
        const auto first = s.find_first_not_of(" \t");
        if (first == std::string::npos) return {};
        return s.substr(first, s.find_last_not_of(" \t") - first + 1);
    }

    void Say(const std::string& message)
    {
        if (log_) log_(message);
    }

    Rectangle HierRect() const { return {hierPos_.x, hierPos_.y, kHierW, kHierH}; }
    Rectangle AddRect() const { return {addPos_.x, addPos_.y, kAddW, kAddH}; }

    static bool InPanelBand(const Vector2& p, float left, float right)
    {
        const float w = static_cast<float>(GetScreenWidth());
        const float h = static_cast<float>(GetScreenHeight());
        return p.x >= w * left && p.x < w * right &&
               p.y >= h * kPanelsTop && p.y < h * kPanelsBottom;
    }

    static bool InHierarchy(const Vector2& p) { return InPanelBand(p, 0.0f, kHierarchyRight); }
    static bool InInspector(const Vector2& p) { return InPanelBand(p, kInspectorLeft, 1.0f); }

    // Keeps a menu of size (w, h) fully inside the window.
    static Vector2 Clamp(Vector2 p, float w, float h)
    {
        const float sw = static_cast<float>(GetScreenWidth());
        const float sh = static_cast<float>(GetScreenHeight());
        p.x = std::max(0.0f, std::min(p.x, sw - w));
        p.y = std::max(0.0f, std::min(p.y, sh - h));
        return p;
    }

    // True when a component row should not be shown right now.
    bool Hidden(const std::string& name) const
    {
        if (!registry_.valid(selected_)) return true;

        const ComponentInfo* info = reflection_.Find(name);
        if (!info) return true;

        // Scripts can be added again and again (each is a different file).
        if (name != kScriptName && info->find(registry_, selected_)) return true;

        const std::string needle = Lower(Trim(filter_));
        return !needle.empty() && Lower(name).find(needle) == std::string::npos;
    }

    // --- opening and closing ---------------------------------------------------

    // Writes the position stylesheet and reloads the styles so the markup's
    // menus appear where they were opened.
    void ApplyPositions()
    {
        std::error_code ec;
        const std::filesystem::path path(positionStyle_);
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ec);

        std::ofstream out(positionStyle_, std::ios::trunc);
        if (!out) return;                       // fall back to the defaults in main.rcss

        out << ".hier-menu { left: " << static_cast<int>(hierPos_.x)
            << "px; top: " << static_cast<int>(hierPos_.y)
            << "px; width: " << static_cast<int>(kHierW)
            << "px; height: " << static_cast<int>(kHierH) << "px; }\n";
        out << ".add-menu { left: " << static_cast<int>(addPos_.x)
            << "px; top: " << static_cast<int>(addPos_.y)
            << "px; width: " << static_cast<int>(kAddW)
            << "px; height: " << static_cast<int>(kAddH) << "px; }\n";
        out.close();

        ui_.LoadStyleFile(baseStyle_);
        ui_.AppendStyleFile(positionStyle_);
    }

    void OpenHierarchy(Vector2 at)
    {
        hierPos_ = Clamp(at, kHierW, kHierH);
        addOpen_ = 0;
        hierOpen_ = 1;
        ApplyPositions();
        ui_.RequestRebuild();       // conditional markup is only re-evaluated on a rebuild
    }

    void OpenAdd(Vector2 at)
    {
        if (!registry_.valid(selected_))
        {
            Say("[editor] select an entity first");
            return;
        }

        addPos_ = Clamp(at, kAddW, kAddH);
        hierOpen_ = 0;
        filter_.clear();
        addOpen_ = 1;
        ApplyPositions();
        ui_.RequestRebuild();
    }

    void CloseAll()
    {
        if (!AnyOpen()) return;
        hierOpen_ = 0;
        addOpen_ = 0;
        filter_.clear();
        ui_.RequestRebuild();
    }

    // --- actions -----------------------------------------------------------------

    std::string UniqueName(const std::string& base)
    {
        for (int n = 1;; ++n)
        {
            const std::string candidate = n == 1 ? base : base + " " + std::to_string(n);
            bool taken = false;

            for (auto entity : registry_.view<GameComponent>())
                if (registry_.get<GameComponent>(entity).name == candidate)
                {
                    taken = true;
                    break;
                }

            if (!taken) return candidate;
        }
    }

    void CreateEntity()
    {
        const entt::entity entity = bus_.Create();
        registry_.emplace<GameComponent>(entity, UniqueName("Entity"));
        registry_.emplace<TransformComponent>(entity);
        selected_ = entity;
        CloseAll();
    }

    void AddComponent(const std::string& name)
    {
        const entt::entity target = selected_;
        CloseAll();

        if (!registry_.valid(target)) return;

        if (name == kScriptName)
        {
            if (pickScript_) pickScript_(target);
            else Say("[editor] no script picker is connected");
            return;
        }

        const ComponentInfo* info = reflection_.Find(name);
        if (!info)
        {
            Say("[editor] unknown component '" + name + "'");
            return;
        }
        if (info->find(registry_, target)) return;      // already has it

        if (!info->add)
        {
            Say("[editor] '" + name + "' cannot be added with defaults");
            return;
        }

        info->add(registry_, target);   // the inspector's ComponentWatch refreshes the lists
    }

    // --- state ---------------------------------------------------------------------

    rui::Context& ui_;
    entt::registry& registry_;
    const Reflection& reflection_;
    EntityBus& bus_;
    entt::entity& selected_;
    std::string baseStyle_;
    std::string positionStyle_;

    // Bound to the markup. Plain ints, like FileDialog, so `== 1` works in markup.
    int hierOpen_ = 0;
    int addOpen_ = 0;
    std::string filter_;

    Vector2 hierPos_ = {0.0f, 0.0f};
    Vector2 addPos_ = {0.0f, 0.0f};

    std::function<bool()> blocked_;
    std::function<void(entt::entity)> pickScript_;
    std::function<void(const std::string&)> log_;
};
