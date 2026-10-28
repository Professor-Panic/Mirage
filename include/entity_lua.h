#pragma once
#include <string>

#include <sol/sol.hpp>
#include "entt.hpp"

#include "entity_bus.h"
#include "reflection.h"
#include "lua_proxy.h"

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
//   Entities.Find("name")            EntityRef or nil
//   Entities.FindAll("name")         array of EntityRef
//   Entities.FromId("uuid")          EntityRef or nil (if it doesn't resolve)
//   Entities.Exists(ref)             bool
//   Entities.Destroy(ref)            bool
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
}