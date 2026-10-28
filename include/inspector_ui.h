#pragma once
#include "raylib.h"
#include "rui.h"
#include "entt.hpp"
#include "reflection.h"
#include "script_system.h"   // ScriptComponent::fields
#include <memory>
#include <optional>
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

// One row per editable field on the selected entity.
struct FieldRow {
    entt::entity entity;            // captured at build time, not read from `selected`
    std::string component;
    std::string label;              // exposed rows: also the key in ScriptComponent::fields
    const ComponentInfo* comp;
    const FieldInfo* field;         // nullptr for exposed script fields
    bool first;
    bool exposed = false;           // true: backed by ScriptComponent::fields
    FieldType exposedType = FieldType::Float;
    bool isRef = false;             // EntityRef row: lets the markup use the picker
};

inline FieldType TypeOfValue(const Value& v) {
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
        : ui_(ui), registry_(registry), selected_(selected), bus_(bus) {

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
            [&] {
                    std::vector<FieldRow> rows;

                    if (registry.valid(selected))
                        for (auto* info : ComponentsOf(reflection, registry, selected)) {
                            bool first = true;

                            for (auto& f : info->fields) {
                                if (f.hidden) continue;
                                rows.push_back({selected, info->name, f.name, info, &f, first,
                                                false, f.type, f.type == FieldType::EntityRef});
                                first = false;
                            }

                            if (info->name == "Script")
                                if (auto* sc = registry.try_get<ScriptComponent>(selected))
                                    for (auto& [key, value] : sc->fields) {
                                        const FieldType t = TypeOfValue(value);
                                        rows.push_back({selected, info->name, key, info, nullptr, first,
                                                        true, t, t == FieldType::EntityRef});
                                        first = false;
                                    }
                        }

                    return rows;
                },
            [&](auto& list) {
                list.Field("component", &FieldRow::component)
                    .Field("first",     &FieldRow::first)
                    .Field("isRef",     &FieldRow::isRef)
                    .Field("exposed",   &FieldRow::exposed)
                    .Field("label",     &FieldRow::label)
                    .template Property<UIXValue>("value",

                        // getter: read the live component every time
                        [&](FieldRow& r) {
                            if (r.exposed) {
                                auto* sc = registry.valid(r.entity)
                                    ? registry.try_get<ScriptComponent>(r.entity) : nullptr;
                                if (!sc) return UIXValue();
                                auto it = sc->fields.find(r.label);
                                return it == sc->fields.end() ? UIXValue() : ToUI(it->second);
                            }
                            void* c = registry.valid(r.entity)
                                ? r.comp->find(registry, r.entity) : nullptr;
                            return c ? ToUI(r.field->get(c)) : UIXValue();
                        },

                        // setter: write the edited value back
                        [&](FieldRow& r, UIXValue v) {
                            if (r.exposed) {
                                auto* sc = registry.valid(r.entity)
                                    ? registry.try_get<ScriptComponent>(r.entity) : nullptr;
                                Value out;
                                if (sc && FromUI(r.exposedType, v, out)) {
                                    auto it = sc->fields.find(r.label);
                                    if (it != sc->fields.end()) it->second = out;
                                }
                                return;
                            }
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
    }

    // Refresh both lists when component C is added to or removed from any entity.
    template <typename C>
    void Watch() {
        watches_.push_back(std::make_shared<ComponentWatch<C>>(registry_, ui_.runtime, "Entity_Components"));
        watches_.push_back(std::make_shared<ComponentWatch<C>>(registry_, ui_.runtime, "Entity_Fields"));
    }

    // Call once per frame: refreshes both lists when the selection changes.
    void Update() {
        if (selected_ == previous_) return;

        ui_.runtime.Invalidate("Entity_Components");
        ui_.runtime.Invalidate("Entity_Fields");
        previous_ = selected_;
    }

private:
    // Current value of a row, whether it is a reflected field or an exposed
    // script field.
    std::optional<Value> LiveValue(const FieldRow& r) {
        if (!registry_.valid(r.entity)) return std::nullopt;

        if (r.exposed) {
            auto* sc = registry_.try_get<ScriptComponent>(r.entity);
            if (!sc) return std::nullopt;
            auto it = sc->fields.find(r.label);
            if (it == sc->fields.end()) return std::nullopt;
            return it->second;
        }

        void* c = r.comp->find(registry_, r.entity);
        if (!c || !r.field) return std::nullopt;
        return r.field->get(c);
    }

    rui::Context& ui_;
    entt::registry& registry_;
    entt::entity& selected_;
    EntityBus& bus_;
    entt::entity previous_ = entt::null;
    std::vector<std::shared_ptr<void>> watches_;   // type-erased ComponentWatch<C>
};