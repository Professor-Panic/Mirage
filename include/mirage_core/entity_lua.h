#pragma once
#include <iostream>
#include <string>

#include <sol/sol.hpp>
#include "entt/entt.hpp"

#include "mirage_core/entity_bus.h"
#include "mirage_core/reflection.h"
#include "mirage_core/lua_binding_backend.h"
#include "mirage_core/lua_native.h"
#include "mirage_core/script_system.h"   // ScriptComponent
#include "mirage_core/components.h"        // TagsComponent

inline ScriptApiParameter EntityApiParam(std::string name, std::string type,
                                         bool optional = false, bool variadic = false)
{
    return { std::move(name), std::move(type), optional, variadic, {} };
}

inline ScriptApiCallable EntityApiCall(std::string name, std::string description,
                                       std::vector<ScriptApiParameter> parameters,
                                       std::vector<std::string> returns,
                                       bool customBinding = false,
                                       std::string bindingNotes = {})
{
    ScriptApiCallable call{ std::move(name), std::move(description),
                            std::move(parameters), std::move(returns), false };
    call.customBinding = customBinding;
    call.bindingNotes = std::move(bindingNotes);
    return call;
}

inline ScriptApiType EntityRefApiType(const Reflection& reflection)
{
    ScriptApiType type;
    type.name = "EntityRef";
    type.cppType = "EntityRef";
    type.description = "Stable reference to an entity. A reference remains safe to hold after its entity is destroyed.";
    type.ownership = "value";
    type.lifetime = "EntityRef stores a stable UUID rather than an EnTT component pointer; IsValid checks whether it currently resolves.";
    type.operators = { "==", "tostring" };
    type.constructors.push_back(EntityApiCall("new", "Create an empty entity reference", {}, { "EntityRef" }));
    auto add = [&](std::string name, std::string description,
                   std::vector<ScriptApiParameter> parameters, std::vector<std::string> returns)
    {
        type.methods.push_back(EntityApiCall(std::move(name), std::move(description),
                                             std::move(parameters), std::move(returns)));
    };
    add("IsNull", "Return whether this reference is empty", {}, { "boolean" });
    add("IsValid", "Return whether the referenced entity currently exists", {}, { "boolean" });
    add("GetId", "Return the entity UUID as a string", {}, { "string" });
    add("GetName", "Return the entity name, or nil when it no longer exists", {}, { "string | nil" });
    add("HasComponent", "Check whether the entity has a script-exposed component",
        { EntityApiParam("name", "string") }, { "boolean" });
    add("GetComponent", "Get a live native handle to a component, or nil",
        { EntityApiParam("name", "string") }, { "ComponentHandle | nil" });
    type.methods.back().customBinding = true;
    type.methods.back().bindingNotes = "Resolves a stable EntityRef and creates a lifetime-checked EnTT component handle.";
    // Literal-name overloads let LuaLS infer the concrete reflected component
    // type for common calls such as ref:GetComponent("Transform").
    for (const auto& [registeredName, component] : reflection.All())
    {
        if (!component.scriptExposed) continue;
        const std::string scriptName = component.scriptName.empty() ? registeredName : component.scriptName;
        type.methods.push_back(EntityApiCall("GetComponent",
            "Get a live " + scriptName + " component handle, or nil",
            { EntityApiParam("name", "\"" + scriptName + "\"") },
            { scriptName + " | nil" }, true,
            "Resolves the named reflected component and returns the generic native component-handle userdata."));
    }
    add("Destroy", "Destroy this entity", {}, { "boolean" });
    add("AddComponent", "Add a script-exposed component using its reflected defaults",
        { EntityApiParam("name", "string") }, { "boolean" });
    add("RemoveComponent", "Remove a component from this entity",
        { EntityApiParam("name", "string") }, { "boolean" });
    add("AddTag", "Add a tag to this entity", { EntityApiParam("tag", "string") }, { "boolean" });
    add("RemoveTag", "Remove a tag from this entity", { EntityApiParam("tag", "string") }, { "boolean" });
    add("HasTag", "Check whether this entity has a tag", { EntityApiParam("tag", "string") }, { "boolean" });
    add("GetTags", "Return the entity's tags as an array of strings", {}, { "string[]" });
    add("SetScript", "Replace the entity's scripts with the specified script",
        { EntityApiParam("path", "string") }, { "boolean" });
    add("AddScript", "Attach another script to this entity",
        { EntityApiParam("path", "string") }, { "boolean" });
    add("RemoveScript", "Detach a script from this entity",
        { EntityApiParam("path", "string") }, { "boolean" });
    add("HasScript", "Check whether a script is attached",
        { EntityApiParam("path", "string") }, { "boolean" });
    add("GetScript", "Get the loaded script instance, optionally selecting a script path",
        { EntityApiParam("path", "string", true) }, { "table | nil" });
    add("Clone", "Clone this entity and its reflected component/script data", {}, { "EntityRef | nil" });
    return type;
}

inline ScriptApiTable EntitiesApiTable()
{
    return { "Entities", "Functions for finding, creating, cloning and destroying entities.", false, {} };
}

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
//   ref:GetComponent("Transform")    native component handle, or nil
//   ref == other                     compares uuids
//
//   ref:Destroy()                    destroys the entity; true if it existed
//   ref:AddComponent("Sprite")       adds a reflected component with its defaults
//                                    (true if added, false if already there /
//                                    unknown / needs arguments: use SetScript)
//   ref:RemoveComponent("Sprite")    true if it was there
//   ref:SetScript("scripts/a.lua")   adds (or re-points) the Script component;
//                                    it starts on the next update tick
//   ref:GetScript()                  the table the entity's script returned (its
//                                    public functions: Start, Update and anything
//                                    else the script put in it); nil if the entity
//                                    has none or several scripts (pass the path),
//                                    or it has not loaded yet
//   ref:GetScript("scripts/a.lua")   the same for one named script
//   ref:AddTag("boid")               add a tag (true if added; false if already there)
//   ref:RemoveTag("boid")            true if it was there
//   ref:HasTag("boid")               bool
//   ref:GetTags()                    array of the entity's tags
//   ref:Clone()                      copy of the entity (fields, script and
//                                    exposed script fields); returns the new ref
//
//   Entities.Find("name")            EntityRef or nil
//   Entities.FindAll("name")         array of EntityRef
//   Entities.FindAll("name", { exclude = self })
//                                    ...without the entity that is asking. `exclude`
//                                    takes a script's `self` or an EntityRef.
//   Entities.FindWithTag("boid")     every entity carrying the tag (array of EntityRef)
//   Entities.FindWithTag("boid", { exclude = self })
//                                    ...except the caller: a boid looking at the others
//   Entities.FromId("uuid")          EntityRef or nil (if it doesn't resolve)
//   Entities.Exists(ref)             bool
//   Entities.Destroy(ref)            bool
//   Entities.Create(["name"])        new entity with Transform and Game(name);
//                                    returns its EntityRef
//   Entities.Create("name", "boid")  ...and with that tag. Several tags: "boid, flock"
//                                    or a table { "boid", "flock" }
//   Entities.Clone(ref)              same as ref:Clone(); nil if ref is missing
//
// Spawning/destroying is safe inside Start/Update: a new entity's script is
// loaded on the NEXT update tick (it never runs half-way through this one), and
// a destroyed entity's script simply stops. Refs to a destroyed entity stay
// valid values but resolve to "missing" (IsValid() == false).
//
// All of these fail soft (nil / false) for null, missing or destroyed targets.
inline void BindEntityLua(sol::state& lua, LuaBindingBackend& api, EntityBus& bus, const Reflection& reflection, ScriptSystem& scripts)
{
    entt::registry& registry = bus.Registry();

    // Resolves ref + component name to a native component handle; nil on failure.
    auto getComponent = [&lua, &bus, &registry, &reflection](
        const EntityRef& ref, const std::string& name) -> sol::object
    {
        entt::entity e = bus.Resolve(ref);
        if (e == entt::null)
            return sol::make_object(lua, sol::lua_nil);

        const ComponentInfo* info = reflection.FindScriptExposed(name);
        if (!info || !info->scriptExposed || !info->find(registry, e))
            return sol::make_object(lua, sol::lua_nil);

        return MakeLuaComponentHandle(lua, registry, e, *info);
    };

    // Adds `name` to `e` using the reflected default constructor.
    auto addComponent = [&registry, &reflection](entt::entity e, const std::string& name)
    {
        const ComponentInfo* info = reflection.FindScriptExposed(name);

        if (!info || !info->scriptExposed)
        {
            std::cerr << "AddComponent: unknown or non-script-exposed component '" << name << "'\n";
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
                continue;                                 // absent, or has no default add

            info.add(registry, copy);
            void* to = info.find(registry, copy);
            if (!to)
                continue;

            for (const FieldInfo& field : info.fields)
                field.set(to, field.get(from));
        }

        if (const auto* script = registry.try_get<ScriptComponent>(src))
        {
            ScriptComponent duplicate;
            duplicate.scripts = script->scripts;          // every script, with its field values
            // The reflection loop above already added an (empty) ScriptComponent
            // to the copy, so replace it: a plain emplace would hit EnTT's
            // "Slot not available" assert.
            registry.emplace_or_replace<ScriptComponent>(copy, std::move(duplicate));
        }

        return bus.Register(copy);
    };

    // `{ exclude = self }` / `{ exclude = someRef }` -> the ref to leave out (null if none).
    // `self` is a script's own table: it carries GetEntity(), so a script can
    // leave itself out without looking up its own ref.
    auto excludedRef = [](const sol::optional<sol::table>& options) -> EntityRef
    {
        if (!options)
            return {};

        sol::object exclude = (*options)["exclude"];
        if (exclude.is<EntityRef>())
            return exclude.as<EntityRef>();

        if (exclude.get_type() == sol::type::table)
        {
            sol::table owner = exclude.as<sol::table>();
            sol::object getEntity = owner["GetEntity"];
            if (getEntity.get_type() == sol::type::function)
            {
                sol::protected_function_result result =
                    getEntity.as<sol::protected_function>()(owner);
                if (result.valid())
                {
                    sol::optional<EntityRef> ref = result.get<sol::optional<EntityRef>>();
                    if (ref) return *ref;
                }
            }
        }
        return {};
    };

    // Adds tags given as "a, b" or { "a", "b" } (anything else is ignored).
    auto applyTags = [&registry](entt::entity e, const sol::object& tags)
    {
        if (tags.get_type() == sol::type::string)
        {
            TagsComponent parsed;
            parsed.tags = tags.as<std::string>();
            for (const std::string& t : parsed.List())
                registry.get_or_emplace<TagsComponent>(e).Add(t);
        }
        else if (tags.get_type() == sol::type::table)
        {
            for (const auto& entry : tags.as<sol::table>())
                if (entry.second.get_type() == sol::type::string)
                    registry.get_or_emplace<TagsComponent>(e).Add(entry.second.as<std::string>());
        }
    };

    api.BindUserType<EntityRef>(
        lua, EntityRefApiType(reflection),
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
            const ComponentInfo* info = reflection.FindScriptExposed(name);
            return e != entt::null && info && info->scriptExposed && info->find(registry, e) != nullptr;
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
            const ComponentInfo* info = reflection.FindScriptExposed(name);
            if (e == entt::null || !info || !info->scriptExposed || !info->find(registry, e))
                return false;

            info->remove(registry, e);
            return true;
        },

        "AddTag", [&bus, &registry](const EntityRef& r, const std::string& tag)
        {
            entt::entity e = bus.Resolve(r);
            return e != entt::null && registry.get_or_emplace<TagsComponent>(e).Add(tag);
        },

        "RemoveTag", [&bus, &registry](const EntityRef& r, const std::string& tag)
        {
            entt::entity e = bus.Resolve(r);
            if (e == entt::null) return false;
            auto* tags = registry.try_get<TagsComponent>(e);
            return tags && tags->Remove(tag);
        },

        "HasTag", [&bus, &registry](const EntityRef& r, const std::string& tag)
        {
            entt::entity e = bus.Resolve(r);
            if (e == entt::null) return false;
            const auto* tags = registry.try_get<TagsComponent>(e);
            return tags && tags->Has(tag);
        },

        "GetTags", [&lua, &bus, &registry](const EntityRef& r) -> sol::table
        {
            sol::table result = lua.create_table();
            entt::entity e = bus.Resolve(r);
            if (e == entt::null) return result;
            if (const auto* tags = registry.try_get<TagsComponent>(e))
            {
                int i = 1;
                for (const std::string& t : tags->List())
                    result[i++] = t;
            }
            return result;
        },

        // Replaces every script on the entity with this one.
        "SetScript", [&bus, &registry](const EntityRef& r, const std::string& path)
        {
            entt::entity e = bus.Resolve(r);
            if (e == entt::null || path.empty())
                return false;

            registry.emplace_or_replace<ScriptComponent>(e, path);
            return true;
        },

        // Attaches another script (false if it is already attached).
        "AddScript", [&bus, &registry](const EntityRef& r, const std::string& path)
        {
            entt::entity e = bus.Resolve(r);
            if (e == entt::null || path.empty())
                return false;

            return registry.get_or_emplace<ScriptComponent>(e).Add(path);
        },

        // Detaches one script (false if it was not attached).
        "RemoveScript", [&bus, &registry](const EntityRef& r, const std::string& path)
        {
            entt::entity e = bus.Resolve(r);
            if (e == entt::null)
                return false;

            auto* script = registry.try_get<ScriptComponent>(e);
            return script && script->Remove(path);
        },

        "HasScript", [&bus, &registry](const EntityRef& r, const std::string& path)
        {
            entt::entity e = bus.Resolve(r);
            if (e == entt::null)
                return false;

            const auto* script = registry.try_get<ScriptComponent>(e);
            return script && script->Has(path);
        },

        // Calls on the result use a dot, not a colon: local function Foo(a) ... end
        // is called as  other:GetScript().Foo(a).
        "GetScript", [&lua, &bus, &scripts](
            const EntityRef& r, sol::optional<std::string> path) -> sol::object
        {
            entt::entity e = bus.Resolve(r);
            if (e == entt::null)
                return sol::make_object(lua, sol::lua_nil);

            return path ? scripts.GetInstance(e, *path) : scripts.GetInstance(e);
        },

        "Clone", cloneEntity,

        sol::meta_function::equal_to,
        [](const EntityRef& a, const EntityRef& b) { return a == b; },

        sol::meta_function::to_string,
        [](const EntityRef& r) { return r.IsNull() ? std::string("EntityRef(null)")
                                                   : "EntityRef(" + r.ToString() + ")"; });

    sol::table entities = api.CreateGlobalTable(lua, EntitiesApiTable());

    api.BindTableFunction(entities, "Entities",
        EntityApiCall("Find", "Find the first entity with the specified name",
            { EntityApiParam("name", "string") }, { "EntityRef | nil" }),
        [&bus](const std::string& name) -> sol::optional<EntityRef>
        {
            EntityRef r = bus.FindByName(name);
            if (r.IsNull()) return sol::nullopt;
            return r;
        });

    api.BindTableFunction(entities, "Entities",
        EntityApiCall("FindAll", "Find all entities with the specified name",
            { EntityApiParam("name", "string"), EntityApiParam("options", "table", true) }, { "EntityRef[]" }),
        [&lua, &bus, excludedRef](const std::string& name, sol::optional<sol::table> options)
        {
            const EntityRef skip = excludedRef(options);
            sol::table result = lua.create_table();
            int i = 1;
            for (const EntityRef& r : bus.FindAllByName(name))
                if (skip.IsNull() || !(r == skip))
                    result[i++] = r;
            return result;
        });

    // Entities.FindWithTag("boid")  /  Entities.FindWithTag("boid", { exclude = self })
    api.BindTableFunction(entities, "Entities",
        EntityApiCall("FindWithTag", "Find all entities carrying a tag",
            { EntityApiParam("tag", "string"), EntityApiParam("options", "table", true) }, { "EntityRef[]" }),
        [&lua, &registry, excludedRef](const std::string& tag, sol::optional<sol::table> options)
        {
            const EntityRef skip = excludedRef(options);
            sol::table result = lua.create_table();
            int i = 1;
            for (auto e : registry.view<IdComponent, TagsComponent>())
            {
                const EntityRef ref{ registry.get<IdComponent>(e).uuid };
                if (!skip.IsNull() && ref == skip)
                    continue;
                if (registry.get<TagsComponent>(e).Has(tag))
                    result[i++] = ref;
            }
            return result;
        });
        // Entities.FindWithComponent("Sprite")              every entity that has it
    // Entities.FindWithComponent("Sprite", "Transform") ...that has ALL of these
    // Returns a table of EntityRefs (empty if none match or a name is unknown).
    api.BindTableFunction(entities, "Entities",
        EntityApiCall("FindWithComponent", "Find entities carrying every named component",
            { EntityApiParam("componentNames", "string", false, true) }, { "EntityRef[]" }),
        [&lua, &registry, &reflection](sol::variadic_args names) -> sol::table
        {
            std::vector<const ComponentInfo*> required;
            for (std::size_t k = 0; k < names.size(); ++k)
            {
                sol::optional<std::string> name = names.get<sol::optional<std::string>>(k);
                const ComponentInfo* info = name ? reflection.Find(*name) : nullptr;
                if (!info || !info->scriptExposed) return lua.create_table();        // unknown name: nothing can match
                required.push_back(info);
            }

            sol::table result = lua.create_table();
            if (required.empty()) return result;

            int i = 1;
            for (auto e : registry.view<IdComponent>())      // same set Find/FindAll search
            {
                bool all = true;
                for (const ComponentInfo* info : required)
                    if (!info->find(registry, e)) { all = false; break; }

                if (all) result[i++] = EntityRef{ registry.get<IdComponent>(e).uuid };
            }
            return result;
        });
    api.BindTableFunction(entities, "Entities",
        EntityApiCall("FromId", "Resolve an entity UUID",
            { EntityApiParam("uuid", "string") }, { "EntityRef | nil" }),
        [&bus](const std::string& id) -> sol::optional<EntityRef>
        {
            EntityRef r = EntityRef::FromString(id);
            if (!bus.IsValid(r)) return sol::nullopt;
            return r;
        });

    api.BindTableFunction(entities, "Entities",
        EntityApiCall("Exists", "Check whether an EntityRef currently resolves",
            { EntityApiParam("ref", "EntityRef") }, { "boolean" }),
        [&bus](const EntityRef& r) { return bus.IsValid(r); });

    api.BindTableFunction(entities, "Entities",
        EntityApiCall("Destroy", "Destroy the referenced entity",
            { EntityApiParam("ref", "EntityRef") }, { "boolean" }),
        [&bus](const EntityRef& r) { return bus.Destroy(r); });

    api.BindTableFunction(entities, "Entities",
        EntityApiCall("Clone", "Clone an entity and its reflected component/script data",
            { EntityApiParam("ref", "EntityRef") }, { "EntityRef | nil" }), cloneEntity);

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

    api.DescribeTableFunction("Entities",
        EntityApiCall("Create", "Create an entity with its default name", {}, { "EntityRef" }));
    api.DescribeTableFunction("Entities",
        EntityApiCall("Create", "Create an entity with a name and no tags",
            { EntityApiParam("name", "string") }, { "EntityRef" }));
    api.BindTableFunction(entities, "Entities",
        EntityApiCall("Create", "Create an entity with a name and optional tags",
            { EntityApiParam("name", "string"), EntityApiParam("tags", "string | string[]") }, { "EntityRef" }),
        sol::overload(
            create,
            [create]() { return create("Entity"); },
            // Entities.Create("boid", "boid, flock")  /  Entities.Create("boid", { "boid" })
            [create, &bus, applyTags](const std::string& name, sol::object tags)
            {
                EntityRef ref = create(name);
                entt::entity e = bus.Resolve(ref);
                if (e != entt::null) applyTags(e, tags);
                return ref;
            }));
}