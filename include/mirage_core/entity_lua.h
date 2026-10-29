#pragma once
#include <iostream>
#include <string>

#include <sol/sol.hpp>
#include "entt/entt.hpp"

#include "mirage_core/entity_bus.h"
#include "mirage_core/reflection.h"
#include "mirage_core/lua_proxy.h"
#include "mirage_core/script_system.h"   // ScriptComponent

// Registers the entity-reference API with Lua. Call once, before any script
// runs (and before ScriptSystem is used).
//
// Lua API
// -------
//   EntityRef()                      empty reference
//   ref:IsNull()                     true if empty
//   ref:IsValid()                    true if the target exists right now
//   ref:GetId()                      uuid string ("" if empty)
//   ref:GetName()                    name, or nil if the target is missing
//   ref:HasComponent("Transform")    bool
//   ref:GetComponent("Transform")    same live proxy as self:GetComponent, or nil
//   ref == other                     compares uuids
//
//   ref:Destroy()                    destroys the entity; true if it existed
//   ref:AddComponent("Sprite")       adds a reflected component with its defaults
//                                    (true if added, false if already there /
//                                    unknown / needs arguments: use SetScript)
//   ref:RemoveComponent("Sprite")    true if it was there
//   ref:SetScript("scripts/a.lua")   adds (or re-points) the Script component;
//                                    it starts on the next update tick
//   ref:Clone()                      copy of the entity (fields, script and
//                                    exposed script fields); returns the new ref
//
//   Entities.Find("name")            EntityRef or nil
//   Entities.FindAll("name")         array of EntityRef
//   Entities.FromId("uuid")          EntityRef or nil (if it doesn't resolve)
//   Entities.Exists(ref)             bool
//   Entities.Destroy(ref)            bool
//   Entities.Create(["name"])        new entity with Transform and Game(name);
//                                    returns its EntityRef
//   Entities.Clone(ref)              same as ref:Clone(); nil if ref is missing
//
// Spawning/destroying is safe inside Start/Update: a new entity's script is
// loaded on the NEXT update tick (it never runs half-way through this one), and
// a destroyed entity's script simply stops. Refs to a destroyed entity stay
// valid values but resolve to "missing" (IsValid() == false).
//
// All of these fail soft (nil / false) for null, missing or destroyed targets.
inline void BindEntityLua(sol::state& lua, EntityBus& bus, const Reflection& reflection)
{
    entt::registry& registry = bus.Registry();

    // Resolves ref + component name to a live proxy; nil on any failure.
    auto getComponent = [&lua, &bus, &registry, &reflection](
        const EntityRef& ref, const std::string& name) -> sol::object
    {
        entt::entity e = bus.Resolve(ref);
        if (e == entt::null)
            return sol::make_object(lua, sol::lua_nil);

        const ComponentInfo* info = reflection.Find(name);
        if (!info || !info->find(registry, e))
            return sol::make_object(lua, sol::lua_nil);

        return sol::make_object(lua, CreateComponentProxy(lua, registry, e, *info));
    };

    // Adds `name` to `e` using the reflected default constructor.
    auto addComponent = [&registry, &reflection](entt::entity e, const std::string& name)
    {
        const ComponentInfo* info = reflection.Find(name);

        if (!info)
        {
            std::cerr << "AddComponent: unknown component '" << name << "'\n";
            return false;
        }
        if (!info->add)
        {
            std::cerr << "AddComponent: '" << name
                      << "' cannot be added from Lua with defaults\n";
            return false;
        }
        if (info->find(registry, e))
            return false;                                 // already has it

        info->add(registry, e);
        return true;
    };

    // Copies every reflected component, the script and its exposed fields.
    auto cloneEntity = [&bus, &registry, &reflection](const EntityRef& ref)
        -> sol::optional<EntityRef>
    {
        entt::entity src = bus.Resolve(ref);
        if (src == entt::null)
            return sol::nullopt;

        entt::entity copy = bus.Create();

        for (const auto& [name, info] : reflection.All())
        {
            void* from = info.find(registry, src);
            if (!from || !info.add)
                continue;                                 // absent, or Script (below)

            info.add(registry, copy);
            void* to = info.find(registry, copy);
            if (!to)
                continue;

            for (const FieldInfo& field : info.fields)
                field.set(to, field.get(from));
        }

        if (const auto* script = registry.try_get<ScriptComponent>(src))
        {
            ScriptComponent duplicate(script->path);
            duplicate.fields = script->fields;
            registry.emplace<ScriptComponent>(copy, std::move(duplicate));
        }

        return bus.Register(copy);
    };

    lua.new_usertype<EntityRef>(
        "EntityRef",
        sol::constructors<EntityRef()>(),                 // EntityRef.new()

        // Makes the table itself callable: EntityRef()
        sol::call_constructor, sol::constructors<EntityRef()>(),

        "IsNull",  [](const EntityRef& r) { return r.IsNull(); },
        "IsValid", [&bus](const EntityRef& r) { return bus.IsValid(r); },
        "GetId",   [](const EntityRef& r) { return r.ToString(); },

        "GetName", [&bus](const EntityRef& r) -> sol::optional<std::string>
        {
            entt::entity e = bus.Resolve(r);
            if (e == entt::null) return sol::nullopt;
            return bus.NameOf(e);
        },

        "HasComponent", [&bus, &registry, &reflection](
            const EntityRef& r, const std::string& name)
        {
            entt::entity e = bus.Resolve(r);
            const ComponentInfo* info = reflection.Find(name);
            return e != entt::null && info && info->find(registry, e) != nullptr;
        },

        "GetComponent", getComponent,

        "Destroy", [&bus](const EntityRef& r) { return bus.Destroy(r); },

        "AddComponent", [&bus, addComponent](const EntityRef& r, const std::string& name)
        {
            entt::entity e = bus.Resolve(r);
            return e != entt::null && addComponent(e, name);
        },

        "RemoveComponent", [&bus, &registry, &reflection](
            const EntityRef& r, const std::string& name)
        {
            entt::entity e = bus.Resolve(r);
            const ComponentInfo* info = reflection.Find(name);
            if (e == entt::null || !info || !info->find(registry, e))
                return false;

            info->remove(registry, e);
            return true;
        },

        "SetScript", [&bus, &registry](const EntityRef& r, const std::string& path)
        {
            entt::entity e = bus.Resolve(r);
            if (e == entt::null || path.empty())
                return false;

            if (auto* script = registry.try_get<ScriptComponent>(e))
                script->path = path;                      // ScriptSystem reloads on change
            else
                registry.emplace<ScriptComponent>(e, path);
            return true;
        },

        "Clone", cloneEntity,

        sol::meta_function::equal_to,
        [](const EntityRef& a, const EntityRef& b) { return a == b; },

        sol::meta_function::to_string,
        [](const EntityRef& r) { return r.IsNull() ? std::string("EntityRef(null)")
                                                   : "EntityRef(" + r.ToString() + ")"; });

    sol::table entities = lua.create_named_table("Entities");

    entities.set_function("Find",
        [&bus](const std::string& name) -> sol::optional<EntityRef>
        {
            EntityRef r = bus.FindByName(name);
            if (r.IsNull()) return sol::nullopt;
            return r;
        });

    entities.set_function("FindAll",
        [&lua, &bus](const std::string& name)
        {
            sol::table result = lua.create_table();
            int i = 1;
            for (const EntityRef& r : bus.FindAllByName(name))
                result[i++] = r;
            return result;
        });

    entities.set_function("FromId",
        [&bus](const std::string& id) -> sol::optional<EntityRef>
        {
            EntityRef r = EntityRef::FromString(id);
            if (!bus.IsValid(r)) return sol::nullopt;
            return r;
        });

    entities.set_function("Exists",
        [&bus](const EntityRef& r) { return bus.IsValid(r); });

    entities.set_function("Destroy",
        [&bus](const EntityRef& r) { return bus.Destroy(r); });

    entities.set_function("Clone", cloneEntity);

    // Entities.Create([name])
    auto create = [&bus, addComponent, &registry, &reflection](const std::string& name) -> EntityRef
    {
        entt::entity e = bus.Create();

        addComponent(e, "Transform");
        addComponent(e, "Game");

        // Name it through reflection, so this file needs no component headers.
        if (const ComponentInfo* game = reflection.Find("Game"))
            if (void* component = game->find(registry, e))
                for (const FieldInfo& field : game->fields)
                    if (field.name == "name")
                        field.set(component, Value{name});

        return bus.Register(e);
    };

    entities.set_function("Create",
        sol::overload(
            create,
            [create]() { return create("Entity"); }));
}