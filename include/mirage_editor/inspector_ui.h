#pragma once
#include "raylib.h"
#include "rui.h"
#include "entt/entt.hpp"
#include "mirage_core/reflection.h"
#include "mirage_core/script_system.h"   // ScriptComponent::scripts
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>


// ---------------------------------------------------------------------------
// ComponentWatch
// ---------------------------------------------------------------------------

// Marks a source-driven RUI list stale whenever component C is added/removed.
// Invalidate() also requests the rebuild, so nothing else is needed.
// Not copyable/movable: entt keeps this object's address.
template <typename C>
class ComponentWatch {
public:
    ComponentWatch(entt::registry& reg, UIXRuntime& runtime, std::string list)
        : reg_(reg), runtime_(runtime), list_(std::move(list)) {
        reg_.on_construct<C>().template connect<&ComponentWatch::Touch>(*this);
        reg_.on_destroy<C>().template connect<&ComponentWatch::Touch>(*this);
    }

    ~ComponentWatch() {
        reg_.on_construct<C>().template disconnect<&ComponentWatch::Touch>(*this);
        reg_.on_destroy<C>().template disconnect<&ComponentWatch::Touch>(*this);
    }

    ComponentWatch(const ComponentWatch&) = delete;
    ComponentWatch& operator=(const ComponentWatch&) = delete;

private:
    void Touch(entt::registry&, entt::entity) { runtime_.Invalidate(list_); }

    entt::registry& reg_;
    UIXRuntime& runtime_;
    std::string list_;
};


// ---------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------

// One row per component on the selected entity.
struct CompRow {
    std::string name;
    const ComponentInfo* info;
};

// One row of the inspector. A row is either a collapsible HEADER (a component
// or a field group), a script row, or a normal editable field.
struct FieldRow {
    entt::entity entity = entt::null;   // captured at build time, not read from `selected`
    std::string component;
    std::string label;              // exposed rows: also the key in ScriptComponent::scripts[scriptPath]
    const ComponentInfo* comp = nullptr;
    const FieldInfo* field = nullptr;   // nullptr for headers, exposed script fields and script headers
    bool exposed = false;           // true: backed by ScriptComponent::scripts[scriptPath]
    FieldType exposedType = FieldType::Float;
    bool isRef = false;             // EntityRef row: lets the markup use the picker
    std::string scriptPath;         // script rows: which script this row belongs to
    bool isScript = false;          // header row of one script (label = its path, has a remove button)

    // Collapsible section headers (components and groups)
    bool isHeader = false;
    bool open = true;               // state when the row was built
    std::string key;                // "RigidBody2D" or "RigidBody2D/Velocity": key into Inspector::open_
    std::string title;              // text on the header button, includes the v / > marker
    std::string cls;                // extra CSS classes: indentation, header look
};

inline FieldType InspectorTypeOfValue(const Value& v) {
    return std::visit([](const auto& x) { return TypeOf<std::decay_t<decltype(x)>>(); }, v);
}


// ---------------------------------------------------------------------------
// Value converters (reflection Value <-> RUI value)
// ---------------------------------------------------------------------------

inline UIXValue ToUI(const Value& v) {
    return std::visit([](const auto& x) -> UIXValue {
        // Placeholder until the RUI entity picker exists: show the uuid text.
        if constexpr (std::is_same_v<std::decay_t<decltype(x)>, EntityRef>)
            return UIXValue::From(x.ToString());
        else
            return UIXValue::From(x);
    }, v);
}

template <class T>
inline bool Take(const UIXValue& u, Value& out) {
    T v{};
    if (!u.AssignTo(v)) return false;
    out = v;
    return true;
}

inline bool FromUI(FieldType t, const UIXValue& u, Value& out) {
    switch (t) {
        case FieldType::Bool:   return Take<bool>(u, out);
        case FieldType::Int:    return Take<int>(u, out);
        case FieldType::Float:  return Take<float>(u, out);
        case FieldType::String: return Take<std::string>(u, out);
        case FieldType::Vec2:   return Take<Vector2>(u, out);
        case FieldType::Vec3:   return Take<Vector3>(u, out);
        case FieldType::Vec4:   return Take<Vector4>(u, out);
        case FieldType::Color:  return Take<Color>(u, out);
        case FieldType::Entity: return Take<entt::entity>(u, out);
        case FieldType::EntityRef: {
            // Placeholder until the RUI entity picker exists: edit as text.
            std::string s;
            if (!u.AssignTo(s)) return false;
            out = EntityRef::FromString(s);
            return true;
        }
    }
    return false;
}


// ---------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------

// Binds two lists for the selected entity:
//   "Entity_Components" : { name }
//   "Entity_Fields"     : { component, label, value }   (value is editable)
//
// Usage:
//   Inspector inspector(ui, registry, reflection, selected);
//   inspector.Watch<TransformComponent>();
//   inspector.Watch<GameComponent>();
//   ...
//   inspector.Update();      // once per frame
//
// Declare it after the registry so it is destroyed first.
class Inspector {
public:
    Inspector(rui::Context& ui, entt::registry& registry,
              const Reflection& reflection, entt::entity& selected,
              EntityBus& bus)
        : ui_(ui), registry_(registry), reflection_(reflection), selected_(selected), bus_(bus) {

        // 1 while an entity is selected: the markup hides Add Component without one.
        ui_.runtime.Bind("has_selection", hasSelection_);

        // --- Components on the selected entity ------------------------------
        ui_.ui.LoadFont("Inter-Regular", "font/Inter/Inter_18pt-Regular.ttf",18);
        ui_.ui.ActivateFonts();
        ui_.runtime.BindList("Entity_Components",
            [&] {
                std::vector<CompRow> rows;

                if (registry.valid(selected))
                    for (auto* info : ComponentsOf(reflection, registry, selected))
                        rows.push_back({info->name, info});

                return rows;
            },
            [](auto& list) { list.Field("name", &CompRow::name); });


        // --- Editable fields on the selected entity -------------------------

        ui_.runtime.BindList("Entity_Fields",
            [this] { return BuildRows(); },
            [&](auto& list) {
                list.Field("component", &FieldRow::component)
                    .Field("isHeader",  &FieldRow::isHeader)
                    .Field("open",      &FieldRow::open)
                    .Field("title",     &FieldRow::title)
                    .Field("cls",       &FieldRow::cls)
                    .Field("isRef",     &FieldRow::isRef)
                    .Field("exposed",   &FieldRow::exposed)
                    .Field("isScript",  &FieldRow::isScript)
                    .Field("label",     &FieldRow::label)
                    .template Property<UIXValue>("value",

                        // getter: read the live component every time
                        [&](FieldRow& r) {
                            if (r.exposed) {
                                auto* fields = ExposedFields(r);
                                if (!fields) return UIXValue();
                                auto it = fields->find(r.label);
                                return it == fields->end() ? UIXValue() : ToUI(it->second);
                            }
                            if (!r.field) return UIXValue();          // script header rows have no value
                            void* c = registry.valid(r.entity)
                                ? r.comp->find(registry, r.entity) : nullptr;
                            return c ? ToUI(r.field->get(c)) : UIXValue();
                        },

                        // setter: write the edited value back
                        [&](FieldRow& r, UIXValue v) {
                            if (r.exposed) {
                                auto* fields = ExposedFields(r);
                                Value out;
                                if (fields && FromUI(r.exposedType, v, out)) {
                                    auto it = fields->find(r.label);
                                    if (it != fields->end()) it->second = out;
                                }
                                return;
                            }
                            if (!r.field) return;
                            void* c = registry.valid(r.entity)
                                ? r.comp->find(registry, r.entity) : nullptr;
                            Value out;
                            if (c && FromUI(r.field->type, v, out))
                                r.field->set(c, out);
                        })

                    // Read-only: what an EntityRef row should display
                    // ("None", "Missing", or the target's name). Live.
                    .template Property<std::string>("display",
                        [&](FieldRow& r) -> std::string {
                            if (!r.isRef) return {};

                            auto v = LiveValue(r);
                            const auto* ref = v ? std::get_if<EntityRef>(&*v) : nullptr;

                            if (!ref || ref->IsNull()) return "None";

                            entt::entity e = bus_.Resolve(*ref);
                            return e == entt::null ? "Missing" : bus_.NameOf(e);
                        },
                        [](FieldRow&, std::string) {});
            });

        // Click on a component / group header: open or close that section. The state
        // lives in open_ (not in the row), so it survives every rebuild.
        ui_.runtime.ItemFunction<FieldRow>("toggleSection", [this](FieldRow& r) {
            if (!r.isHeader) return;
            open_[r.key] = !r.open;
            ui_.runtime.Invalidate("Entity_Fields");
        });

        // The "x" on a script header: detach that script from the entity.
        ui_.runtime.ItemFunction<FieldRow>("removeScript", [this](FieldRow& r) {
            if (!registry_.valid(r.entity) || r.scriptPath.empty()) return;

            if (auto* sc = registry_.try_get<ScriptComponent>(r.entity))
                sc->Remove(r.scriptPath);   // ScriptSystem drops the runtime on its next pass

            // Removing a script does not add or remove a component, so no entt
            // signal fires; refresh the lists by hand.
            ui_.runtime.Invalidate("Entity_Components");
            ui_.runtime.Invalidate("Entity_Fields");
        });
    }

    // Refresh both lists when component C is added to or removed from any entity.
    template <typename C>
    void Watch() {
        watches_.push_back(std::make_shared<ComponentWatch<C>>(registry_, ui_.runtime, "Entity_Components"));
        watches_.push_back(std::make_shared<ComponentWatch<C>>(registry_, ui_.runtime, "Entity_Fields"));
    }

    // Call once per frame: refreshes both lists when the selection changes.
    void Update() {
        // Conditional markup is only re-evaluated on a rebuild, so rebuild when this flips.
        const int has = registry_.valid(selected_) ? 1 : 0;
        if (has != hasSelection_) {
            hasSelection_ = has;
            ui_.RequestRebuild();
        }

        if (selected_ == previous_) return;

        ui_.runtime.Invalidate("Entity_Components");
        ui_.runtime.Invalidate("Entity_Fields");
        previous_ = selected_;
    }

private:
    // ---- Section state ------------------------------------------------------
    // Components start open, groups start closed. Remembered per section name, not
    // per entity, so the inspector keeps its layout when you click another entity.
    bool IsOpen(const std::string& key, bool defaultOpen) const {
        auto it = open_.find(key);
        return it == open_.end() ? defaultOpen : it->second;
    }

    static std::string DepthClass(int depth) {
        return "inspector-depth-" + std::to_string(std::clamp(depth, 0, 4));
    }

    // "Physics/Velocity" -> { "Physics", "Velocity" }
    static std::vector<std::string> SplitGroup(const std::string& path) {
        std::vector<std::string> out;
        std::string part;
        for (char c : path) {
            if (c == '/') { if (!part.empty()) out.push_back(part); part.clear(); }
            else part += c;
        }
        if (!part.empty()) out.push_back(part);
        return out;
    }

    FieldRow MakeHeader(const ComponentInfo* info, const std::string& key, const std::string& label,
                        const std::string& cls, bool hasContent, bool open) const {
        FieldRow r;
        r.entity = selected_;
        r.component = info->name;
        r.label = label;
        r.comp = info;
        r.isHeader = true;
        r.open = open;
        r.key = key;
        r.title = (hasContent ? (open ? "v  " : ">  ") : "    ") + label;   // plain ASCII: the font has no arrows
        r.cls = cls;
        return r;
    }

    FieldRow MakeField(const ComponentInfo* info, const FieldInfo& f, int depth) const {
        FieldRow r;
        r.entity = selected_;
        r.component = info->name;
        r.label = f.label.empty() ? f.name : f.label;
        r.comp = info;
        r.field = &f;
        r.exposedType = f.type;
        r.isRef = f.type == FieldType::EntityRef;
        r.cls = DepthClass(depth);
        return r;
    }

    // Emits `fields` (all sharing the group prefix above `level`). A field with no
    // group segment left at this level is a row; the others are collected under a
    // header for their segment, whose members are emitted one level deeper.
    void EmitLevel(std::vector<FieldRow>& rows, const ComponentInfo* info,
                   const std::vector<const FieldInfo*>& fields,
                   std::size_t level, const std::string& keyPrefix, int depth) {
        std::set<std::string> done;
        for (const FieldInfo* f : fields) {
            const auto path = SplitGroup(f->group);
            if (path.size() <= level) {
                rows.push_back(MakeField(info, *f, depth));
                continue;
            }

            const std::string& name = path[level];
            if (!done.insert(name).second) continue;       // this group was already emitted

            std::vector<const FieldInfo*> members;
            for (const FieldInfo* g : fields) {
                const auto gp = SplitGroup(g->group);
                if (gp.size() > level && gp[level] == name) members.push_back(g);
            }

            const std::string key = keyPrefix + "/" + name;
            const bool open = IsOpen(key, false);
            rows.push_back(MakeHeader(info, key, name, "inspector-group " + DepthClass(depth), true, open));
            if (open) EmitLevel(rows, info, members, level + 1, key, depth + 1);
        }
    }

    // Script component: one header per script, followed by that script's own
    // exposed fields.
    void AppendScriptRows(std::vector<FieldRow>& rows, const ComponentInfo* info) {
        auto* sc = registry_.try_get<ScriptComponent>(selected_);
        if (!sc) return;

        auto base = [&](std::string label) {
            FieldRow r;
            r.entity = selected_;
            r.component = info->name;
            r.label = std::move(label);
            r.comp = info;
            r.cls = DepthClass(1);
            return r;
        };

        if (sc->scripts.empty()) {
            FieldRow none = base("(no scripts)");
            none.isScript = true;               // empty path: its remove button does nothing
            rows.push_back(none);
        }

        for (auto& [path, fields] : sc->scripts) {
            FieldRow header = base(path);
            header.scriptPath = path;
            header.isScript = true;
            rows.push_back(header);

            for (auto& [key, value] : fields) {
                const FieldType t = InspectorTypeOfValue(value);
                FieldRow row = base(key);
                row.exposed = true;
                row.exposedType = t;
                row.isRef = t == FieldType::EntityRef;
                row.scriptPath = path;
                rows.push_back(row);
            }
        }
    }

    // Everything the inspector shows. Empty when nothing valid is selected.
    std::vector<FieldRow> BuildRows() {
        std::vector<FieldRow> rows;
        if (!registry_.valid(selected_)) return rows;

        for (auto* info : ComponentsOf(reflection_, registry_, selected_)) {
            std::vector<const FieldInfo*> visible;
            for (auto& f : info->fields)
                if (!f.hidden) visible.push_back(&f);

            const bool isScriptComponent = info->name == "Script";
            const bool hasContent = !visible.empty() || isScriptComponent;
            const bool open = IsOpen(info->name, true);

            rows.push_back(MakeHeader(info, info->name, info->name, "inspector-component", hasContent, open));
            if (!open || !hasContent) continue;

            EmitLevel(rows, info, visible, 0, info->name, 1);
            if (isScriptComponent) AppendScriptRows(rows, info);
        }
        return rows;
    }

    // The exposed-field table of the script a row belongs to (nullptr if the
    // entity or that script is gone).
    std::map<std::string, Value>* ExposedFields(const FieldRow& r) {
        if (!registry_.valid(r.entity)) return nullptr;
        auto* sc = registry_.try_get<ScriptComponent>(r.entity);
        if (!sc) return nullptr;
        auto it = sc->scripts.find(r.scriptPath);
        return it == sc->scripts.end() ? nullptr : &it->second;
    }

    // Current value of a row, whether it is a reflected field or an exposed
    // script field.
    std::optional<Value> LiveValue(const FieldRow& r) {
        if (!registry_.valid(r.entity)) return std::nullopt;

        if (r.exposed) {
            auto* fields = ExposedFields(r);
            if (!fields) return std::nullopt;
            auto it = fields->find(r.label);
            if (it == fields->end()) return std::nullopt;
            return it->second;
        }

        void* c = r.comp->find(registry_, r.entity);
        if (!c || !r.field) return std::nullopt;
        return r.field->get(c);
    }

    rui::Context& ui_;
    entt::registry& registry_;
    const Reflection& reflection_;
    entt::entity& selected_;
    EntityBus& bus_;
    entt::entity previous_ = entt::null;
    int hasSelection_ = 0;
    std::map<std::string, bool> open_;           // section key -> open?
    std::vector<std::shared_ptr<void>> watches_;   // type-erased ComponentWatch<C>
};