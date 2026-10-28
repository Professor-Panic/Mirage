#pragma once
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "entt.hpp"
#include "entity_bus.h"
#include "reflection.h"
#include "script_system.h"   // ScriptComponent::fields

// ---------------------------------------------------------------------------
// PlaySnapshot - Unity-style "numbers are forgotten when Play stops".
//
//   Capture()  when Play starts: records, for every entity that has an
//              IdComponent, each reflected component's field values plus the
//              exposed script fields.
//   Restore()  when Play stops:
//                - entities created during Play are destroyed
//                - entities destroyed during Play are recreated (same Uuid)
//                - components added during Play are removed, components
//                  removed during Play are added back
//                - every captured field value is written back
//
// Only components registered in Reflection are captured, and only entities
// with an IdComponent (create them with EntityBus::Create). Anything else is
// left exactly as Play left it.
//
// Limits:
//  - A recreated entity gets a NEW entt::entity handle (its Uuid is the same).
//    EntityRef fields keep working; raw entt::entity fields pointing at it do
//    not. Re-resolve any handle you hold across Restore() by Uuid.
//  - A component can only be added back if it has an `add` function (default
//    constructible, or registered with DefaultArgs).
//  - Pool order can change for recreated entities, so list order may differ.
// ---------------------------------------------------------------------------
class PlaySnapshot {
public:
    PlaySnapshot(entt::registry& registry, EntityBus& bus, const Reflection& reflection)
        : reg_(registry), bus_(bus), refl_(reflection) {}

    bool HasData() const { return valid_; }

    void Capture() {
        data_.clear();

        for (entt::entity e : reg_.view<IdComponent>()) {
            EntityData d;
            d.id = reg_.get<IdComponent>(e).uuid;

            for (const auto& [name, info] : refl_.All()) {
                void* c = info.find(reg_, e);
                if (!c) continue;

                ComponentData cd;
                cd.info = &info;
                for (const FieldInfo& f : info.fields)
                    cd.values.push_back(f.get(c));
                d.components.push_back(std::move(cd));
            }

            if (const auto* sc = reg_.try_get<ScriptComponent>(e)) {
                d.hasScript = true;
                d.scriptFields = sc->fields;
            }

            data_.push_back(std::move(d));
        }

        valid_ = true;
    }

    void Restore() {
        if (!valid_) return;

        // 1. Destroy entities that did not exist when Play started.
        std::unordered_set<Uuid> kept;
        for (const EntityData& d : data_) kept.insert(d.id);

        std::vector<entt::entity> spawned;
        for (entt::entity e : reg_.view<IdComponent>())
            if (!kept.count(reg_.get<IdComponent>(e).uuid))
                spawned.push_back(e);
        for (entt::entity e : spawned) reg_.destroy(e);

        // 2. Bring every captured entity back to its captured state.
        for (const EntityData& d : data_) {
            entt::entity e = bus_.Resolve(EntityRef{d.id});

            if (e == entt::null) {
                e = bus_.Create(d.id);
                if (bus_.IdOf(e) != d.id)
                    std::cerr << "PlaySnapshot: Uuid " << d.id.ToString()
                              << " was taken, entity got a new one\n";
            }

            // Components added during Play.
            std::unordered_set<const ComponentInfo*> had;
            for (const ComponentData& cd : d.components) had.insert(cd.info);

            for (const auto& [name, info] : refl_.All())
                if (!had.count(&info) && info.find(reg_, e))
                    info.remove(reg_, e);

            // Components that existed: re-add if missing, then write fields.
            for (const ComponentData& cd : d.components) {
                void* c = cd.info->find(reg_, e);

                if (!c) {
                    if (!cd.info->add) {
                        std::cerr << "PlaySnapshot: cannot re-add '" << cd.info->name
                                  << "' (no default constructor or DefaultArgs)\n";
                        continue;
                    }
                    cd.info->add(reg_, e);
                    c = cd.info->find(reg_, e);
                    if (!c) continue;
                }

                const auto& fields = cd.info->fields;
                for (std::size_t i = 0; i < fields.size() && i < cd.values.size(); ++i)
                    fields[i].set(c, cd.values[i]);
            }

            if (d.hasScript)
                if (auto* sc = reg_.try_get<ScriptComponent>(e))
                    sc->fields = d.scriptFields;
        }
    }

private:
    struct ComponentData {
        const ComponentInfo* info = nullptr;
        std::vector<Value> values;               // one per info->fields entry
    };

    struct EntityData {
        Uuid id;
        std::vector<ComponentData> components;
        bool hasScript = false;
        std::map<std::string, Value> scriptFields;
    };

    entt::registry& reg_;
    EntityBus& bus_;
    const Reflection& refl_;
    std::vector<EntityData> data_;
    bool valid_ = false;
};