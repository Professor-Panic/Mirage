#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sol/sol.hpp>
#include <entt/entt.hpp>
#include <raylib.h>

#include "mirage_core/reflection.h"
#include "mirage_core/lua_proxy.h"
#include "mirage_core/entity_bus.h"

struct ScriptComponent
{
    std::string path;
    std::map<std::string, Value> fields;

    explicit ScriptComponent(std::string name)
        : path(std::move(name))
    {
    }
};

class ScriptSystem
{
private:
    struct RuntimeScript
    {
        std::string loadedPath;

        // Environment isolated to this script instance.
        sol::environment environment;

        // Entity API passed to the Lua factory.
        sol::table self;

        // Table returned by the factory; retains the instance and closures.
        sol::table instance;

        sol::protected_function start;
        sol::protected_function update;

        bool started = false;
        bool updateDisabled = false;
    };

public:
    ScriptSystem(
        sol::state& lua,
        Reflection& reflection,
        entt::registry& registry,
        EntityBus& bus)
        : lua_(lua),
          reflection_(reflection),
          registry_(registry),
          bus_(bus)
    {
    }

    // Convert supported Lua values into the engine's reflected Value type.
    Value LuaToValue(sol::object value)
    {
        switch (value.get_type())
        {
        case sol::type::boolean:
            return value.as<bool>();

        case sol::type::number:
            if (value.is<int>())
                return value.as<int>();

            return value.as<float>();

        case sol::type::string:
            return value.as<std::string>();

        case sol::type::table:
        {
            sol::table table = value.as<sol::table>();

            // Color
            if (table["r"].valid() &&
                table["g"].valid() &&
                table["b"].valid() &&
                table["a"].valid())
            {
                return Color{
                    table["r"].get<unsigned char>(),
                    table["g"].get<unsigned char>(),
                    table["b"].get<unsigned char>(),
                    table["a"].get<unsigned char>()
                };
            }

            // Vector4 / Quaternion
            if (table["x"].valid() &&
                table["y"].valid() &&
                table["z"].valid() &&
                table["w"].valid())
            {
                return Vector4{
                    table["x"].get<float>(),
                    table["y"].get<float>(),
                    table["z"].get<float>(),
                    table["w"].get<float>()
                };
            }

            // Vector3
            if (table["x"].valid() &&
                table["y"].valid() &&
                table["z"].valid())
            {
                return Vector3{
                    table["x"].get<float>(),
                    table["y"].get<float>(),
                    table["z"].get<float>()
                };
            }

            // Vector2
            if (table["x"].valid() &&
                table["y"].valid())
            {
                return Vector2{
                    table["x"].get<float>(),
                    table["y"].get<float>()
                };
            }

            throw std::runtime_error(
                "Unsupported Lua table: expected a vector or color");
        }

        case sol::type::userdata:
            if (value.is<EntityRef>())
                return value.as<EntityRef>();

            throw std::runtime_error(
                "Unsupported Lua userdata: expected an EntityRef");

        default:
            throw std::runtime_error(
                "Unsupported Lua value type "
                "(use EntityRef() for an empty entity reference)");
        }
    }

    // Create the entity API exposed to Lua.
    //
    // Supported calls:
    //   self:GetComponent("Transform")
    //   GetComponent("Transform")
    //   self:expose("speed", 5.0)
    //   expose("speed", 5.0)
    sol::table CreateSelf(entt::entity entity)
    {
        sol::table self = lua_.create_table();

        self.set_function(
            "GetComponent",
            sol::overload(
                [this, entity](
                    sol::table,
                    const std::string& name) -> sol::object
                {
                    return GetComponentFor(entity, name);
                },
                [this, entity](
                    const std::string& name) -> sol::object
                {
                    return GetComponentFor(entity, name);
                }));

        self.set_function(
            "expose",
            sol::overload(
                [this, entity](
                    sol::table,
                    const std::string& name,
                    sol::object value) -> sol::object
                {
                    return ExposeFor(entity, name, value);
                },
                [this, entity](
                    const std::string& name,
                    sol::object value) -> sol::object
                {
                    return ExposeFor(entity, name, value);
                }));

        // self:GetEntity() -> EntityRef to this script's own entity.
        self.set_function(
            "GetEntity",
            sol::overload(
                [this, entity](sol::table) { return bus_.Register(entity); },
                [this, entity]() { return bus_.Register(entity); }));

        // self.speed reads/writes exposed fields live (same lookup as bare names).
        sol::table meta = lua_.create_table();

        meta.set_function(
            "__index",
            [this, entity](sol::table, sol::object key) -> sol::object
            {
                if (key.is<std::string>())
                    if (auto exposed = ReadExposed(entity, key.as<std::string>()))
                        return *exposed;

                return sol::make_object(lua_, sol::lua_nil);
            });

        meta.set_function(
            "__newindex",
            [this, entity](sol::table tbl, sol::object key, sol::object value)
            {
                if (key.is<std::string>() &&
                    WriteExposed(entity, key.as<std::string>(), value))
                    return;

                tbl.raw_set(key, value);
            });

        self[sol::metatable_key] = meta;

        return self;
    }

    // Load a Lua file that returns a factory function.
    //
    // Expected Lua structure:
    //   return function(self)
    //       local transform = self:GetComponent("Transform")
    //
    //       local function Start()
    //       end
    //
    //       local function Update(dt)
    //       end
    //
    //       return {
    //           Start = Start,
    //           Update = Update
    //       }
    //   end
    bool LoadScript(entt::entity entity)
    {
        if (!registry_.valid(entity) ||
            !registry_.all_of<ScriptComponent>(entity))
        {
            std::cerr
                << "LoadScript(): entity has no ScriptComponent\n";
            return false;
        }

        ScriptComponent& component =
            registry_.get<ScriptComponent>(entity);

        if (component.path.empty())
        {
            std::cerr
                << "LoadScript(): script path is empty\n";
            return false;
        }

        RuntimeScript runtime;
        runtime.loadedPath = component.path;

        // Each entity gets its own Lua environment.
        runtime.environment = sol::environment(lua_, sol::create);

        runtime.self = CreateSelf(entity);

        // Expose the entity API inside the script environment.
        runtime.environment["self"] = runtime.self;
        runtime.environment["expose"] = runtime.self["expose"];
        runtime.environment["GetComponent"] =
            runtime.self["GetComponent"];

        // Exposed fields become live members of the script: the bare name
        // (speed) reads and writes ScriptComponent::fields directly.
        InstallEnvironmentMeta(runtime.environment, entity);

        // Execute the chunk. The chunk must return a factory function.
        // script_pass_on_error: a missing file or a syntax error comes back as
        // an invalid result (handled below) instead of throwing.
        sol::protected_function_result chunkResult =
            lua_.safe_script_file(
                component.path,
                runtime.environment,
                sol::script_pass_on_error);

        if (!chunkResult.valid())
        {
            sol::error err = chunkResult;

            std::cerr
                << "Loading script '"
                << component.path
                << "' failed: "
                << err.what()
                << '\n';

            return false;
        }

        sol::object factoryObject =
            chunkResult.get<sol::object>();

        if (factoryObject.get_type() != sol::type::function)
        {
            std::cerr
                << "Script '"
                << component.path
                << "' must return a factory function\n";
            return false;
        }

        sol::protected_function factory =
            factoryObject.as<sol::protected_function>();

        // Invoke the factory once. It can cache component proxies and
        // local state inside closures instead of fetching them every frame.
        sol::protected_function_result factoryResult =
            factory(runtime.self);

        if (!factoryResult.valid())
        {
            sol::error err = factoryResult;

            std::cerr
                << "Script factory failed for entity "
                << static_cast<std::uint32_t>(entity)
                << ": "
                << err.what()
                << '\n';

            return false;
        }

        sol::object instanceObject =
            factoryResult.get<sol::object>();

        if (instanceObject.get_type() != sol::type::table)
        {
            std::cerr
                << "Factory in '"
                << component.path
                << "' must return a table\n";
            return false;
        }

        runtime.instance = instanceObject.as<sol::table>();

        // Callbacks are read from the returned instance table.
        sol::object startObject = runtime.instance["Start"];

        if (startObject.get_type() == sol::type::function)
        {
            runtime.start =
                startObject.as<sol::protected_function>();
        }

        sol::object updateObject = runtime.instance["Update"];

        if (updateObject.get_type() == sol::type::function)
        {
            runtime.update =
                updateObject.as<sol::protected_function>();
        }

        // Replace the current runtime only after the new script loads.
        scripts_[entity] = std::move(runtime);

        return true;
    }

    // Invoke Start() once for this runtime.
    bool StartScript(entt::entity entity)
    {
        auto it = scripts_.find(entity);

        if (it == scripts_.end())
        {
            if (!LoadScript(entity))
                return false;

            it = scripts_.find(entity);

            if (it == scripts_.end())
                return false;
        }

        RuntimeScript& runtime = it->second;

        if (runtime.started)
            return true;

        // Mark as started before invoking Lua.
        runtime.started = true;

        // Start is optional.
        if (!runtime.start.valid())
            return true;

        sol::protected_function_result result =
            runtime.start();

        if (!result.valid())
        {
            sol::error err = result;

            std::cerr
                << "Start() failed for entity "
                << static_cast<std::uint32_t>(entity)
                << ": "
                << err.what()
                << '\n';

            return false;
        }

        return true;
    }

    // Load and start scripts as necessary, then invoke Update(dt).
    void UpdateScripts(float dt)
    {
        // Copy entity IDs first so script callbacks can modify the registry
        // without invalidating the view iteration.
        std::vector<entt::entity> entities;

        auto view = registry_.view<ScriptComponent>();

        for (entt::entity entity : view)
            entities.push_back(entity);

        for (entt::entity entity : entities)
        {
            if (!registry_.valid(entity) ||
                !registry_.all_of<ScriptComponent>(entity))
            {
                continue;
            }

            const std::string path =
                registry_.get<ScriptComponent>(entity).path;

            auto it = scripts_.find(entity);

            // Load if missing, or reload if the path changed.
            if (it == scripts_.end() ||
                it->second.loadedPath != path)
            {
                if (!TryLoad(entity, path))
                    continue;

                it = scripts_.find(entity);

                if (it == scripts_.end())
                    continue;
            }

            // StartScript is safe to call repeatedly.
            if (!it->second.started)
                StartScript(entity);

            // Re-find the runtime after invoking Lua.
            it = scripts_.find(entity);

            if (it == scripts_.end())
                continue;

            RuntimeScript& runtime = it->second;

            if (runtime.updateDisabled ||
                !runtime.update.valid())
            {
                continue;
            }

            sol::protected_function_result result =
                runtime.update(dt);

            if (!result.valid())
            {
                sol::error err = result;

                std::cerr
                    << "Update() failed for entity "
                    << static_cast<std::uint32_t>(entity)
                    << ": "
                    << err.what()
                    << '\n';

                // Avoid repeatedly invoking a callback that has failed.
                runtime.updateDisabled = true;
            }
        }

        RemoveStaleRuntimes();
    }

    // Edit mode: run every script's factory (so expose() registers its
    // fields for the Inspector) without calling Start() or Update().
    // Cheap to call every frame: it only loads scripts that have no runtime
    // yet or whose path changed, and a script that failed to load is not
    // retried until its path changes or ReloadAll() is called.
    void EnsureLoaded()
    {
        std::vector<entt::entity> entities;

        for (entt::entity entity : registry_.view<ScriptComponent>())
            entities.push_back(entity);

        for (entt::entity entity : entities)
        {
            if (!registry_.valid(entity) ||
                !registry_.all_of<ScriptComponent>(entity))
            {
                continue;
            }

            const std::string path =
                registry_.get<ScriptComponent>(entity).path;

            auto it = scripts_.find(entity);

            if (it == scripts_.end() || it->second.loadedPath != path)
                TryLoad(entity, path);
        }

        RemoveStaleRuntimes();
    }

    // Throw away every runtime and load them all again, fresh and not yet
    // started. Exposed field values stored in ScriptComponent::fields are
    // kept (expose() only supplies defaults). Re-reads the files from disk.
    // Call it when switching between Edit and Play.
    void ReloadAll()
    {
        scripts_.clear();
        failed_.clear();
        EnsureLoaded();
    }

    void UnloadScript(entt::entity entity)
    {
        scripts_.erase(entity);
        failed_.erase(entity);
    }

    void UnloadAll()
    {
        scripts_.clear();
        failed_.clear();
    }

    // Called when a script exposes a new field (or changes a field's type),
    // so UI lists built from ScriptComponent::fields can refresh.
    void OnFieldsChanged(std::function<void()> fn)
    {
        onFieldsChanged_ = std::move(fn);
    }

private:
    // LoadScript with a memory of failures, so a broken script prints its
    // error once instead of every frame.
    bool TryLoad(entt::entity entity, const std::string& path)
    {
        auto bad = failed_.find(entity);

        if (bad != failed_.end() && bad->second == path)
            return false;

        bool ok = false;

        try
        {
            ok = LoadScript(entity);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Loading script '" << path << "' failed: "
                      << e.what() << '\n';
        }

        if (ok)
        {
            failed_.erase(entity);
            return true;
        }

        failed_[entity] = path;
        return false;
    }

    // Remove runtimes for deleted entities or removed script components.
    void RemoveStaleRuntimes()
    {
        for (auto it = scripts_.begin(); it != scripts_.end();)
        {
            if (!registry_.valid(it->first) ||
                !registry_.all_of<ScriptComponent>(it->first))
                it = scripts_.erase(it);
            else
                ++it;
        }

        for (auto it = failed_.begin(); it != failed_.end();)
        {
            if (!registry_.valid(it->first) ||
                !registry_.all_of<ScriptComponent>(it->first))
                it = failed_.erase(it);
            else
                ++it;
        }
    }

    // Implementation shared by self:GetComponent(name) and
    // GetComponent(name).
    sol::object GetComponentFor(
        entt::entity entity,
        const std::string& name)
    {
        if (!registry_.valid(entity))
            return sol::make_object(lua_, sol::lua_nil);

        const ComponentInfo* info = reflection_.Find(name);

        if (!info)
        {
            std::cerr
                << "GetComponent: unknown component '"
                << name
                << "'\n";

            return sol::make_object(lua_, sol::lua_nil);
        }

        if (!info->find(registry_, entity))
        {
            // The component is registered, but this entity doesn't have it.
            return sol::make_object(lua_, sol::lua_nil);
        }

        sol::table proxy =
            CreateComponentProxy(lua_, registry_, entity, *info);

        return sol::make_object(lua_, proxy);
    }

    // ---- Exposed fields --------------------------------------------------
    //
    // Exposed fields live only in ScriptComponent::fields. Scripts reach them
    // through the environment (bare name) or self.<name>; neither keeps its own
    // copy, so Inspector edits are seen immediately and script writes show up
    // in the Inspector. Vectors/colors are returned as live proxies, so even
    // `local offset = offset_field` stays live. A local copy of a plain
    // number/bool/string is still just a copy, exactly like in Unity.

    template <class T>
    sol::object ExposedProxy(entt::entity entity, const std::string& name)
    {
        auto get = [this, entity, name]() -> T
        {
            if (registry_.valid(entity))
                if (auto* sc = registry_.try_get<ScriptComponent>(entity))
                {
                    auto it = sc->fields.find(name);

                    if (it != sc->fields.end())
                        if (auto* v = std::get_if<T>(&it->second))
                            return *v;
                }

            throw std::runtime_error(
                "Exposed field no longer exists: " + name);
        };

        auto set = [this, entity, name](const T& value)
        {
            if (registry_.valid(entity))
                if (auto* sc = registry_.try_get<ScriptComponent>(entity))
                {
                    auto it = sc->fields.find(name);

                    if (it != sc->fields.end())
                    {
                        it->second = value;
                        return;
                    }
                }

            throw std::runtime_error(
                "Exposed field no longer exists: " + name);
        };

        return sol::make_object(
            lua_,
            CreateLiveValueProxy<T>(lua_, std::move(get), std::move(set)));
    }

    sol::optional<sol::object> ReadExposed(
        entt::entity entity,
        const std::string& name)
    {
        ScriptComponent* sc = registry_.valid(entity)
            ? registry_.try_get<ScriptComponent>(entity)
            : nullptr;

        if (!sc)
            return sol::nullopt;

        auto it = sc->fields.find(name);

        if (it == sc->fields.end())
            return sol::nullopt;

        const Value& v = it->second;

        if (std::holds_alternative<Vector2>(v)) return ExposedProxy<Vector2>(entity, name);
        if (std::holds_alternative<Vector3>(v)) return ExposedProxy<Vector3>(entity, name);
        if (std::holds_alternative<Vector4>(v)) return ExposedProxy<Vector4>(entity, name);
        if (std::holds_alternative<Color>(v))   return ExposedProxy<Color>(entity, name);

        return ValueToLua(lua_, v);
    }

    // Returns false if `name` is not an exposed field (caller stores it as an
    // ordinary script variable). Throws a Lua error on a type mismatch.
    bool WriteExposed(
        entt::entity entity,
        const std::string& name,
        const sol::object& value)
    {
        ScriptComponent* sc = registry_.valid(entity)
            ? registry_.try_get<ScriptComponent>(entity)
            : nullptr;

        if (!sc)
            return false;

        auto it = sc->fields.find(name);

        if (it == sc->fields.end())
            return false;

        try
        {
            // Qualified: the member LuaToValue(sol::object) would hide this.
            it->second = ::LuaToValue(value, it->second);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error(
                "Cannot assign exposed field '" + name + "': " + e.what());
        }

        return true;
    }

    // Script globals resolve in this order:
    //   1. exposed fields (live)  2. the script's own variables  3. real globals
    // Assignments to an exposed name write the field; anything else is stored
    // in a per-script table, so scripts can still make their own "globals".
    void InstallEnvironmentMeta(sol::environment& env, entt::entity entity)
    {
        sol::table vars = lua_.create_table();
        sol::table meta = lua_.create_table();

        meta.set_function(
            "__index",
            [this, entity, vars](sol::table, sol::object key) -> sol::object
            {
                if (key.is<std::string>())
                    if (auto exposed = ReadExposed(entity, key.as<std::string>()))
                        return *exposed;

                sol::table own = vars;
                sol::object found = own.raw_get<sol::object>(key);

                if (found != sol::lua_nil)
                    return found;

                return lua_.globals().get<sol::object>(key);
            });

        meta.set_function(
            "__newindex",
            [this, entity, vars](sol::table, sol::object key, sol::object value)
            {
                if (key.is<std::string>() &&
                    WriteExposed(entity, key.as<std::string>(), value))
                    return;

                sol::table own = vars;
                own.raw_set(key, value);
            });

        env[sol::metatable_key] = meta;
    }

    // Implementation shared by self:expose(name, value) and expose(name, value).
    // The value is only a default: one already stored (Inspector edit or
    // loaded scene) wins, unless its type no longer matches.
    sol::object ExposeFor(
        entt::entity entity,
        const std::string& name,
        sol::object value)
    {
        try
        {
            if (!registry_.valid(entity) ||
                !registry_.all_of<ScriptComponent>(entity))
            {
                throw std::runtime_error(
                    "Entity no longer has a ScriptComponent");
            }

            ScriptComponent& script =
                registry_.get<ScriptComponent>(entity);

            Value incoming = LuaToValue(value);

            auto [it, inserted] = script.fields.try_emplace(name, incoming);
            bool changed = inserted;

            if (!inserted && it->second.index() != incoming.index())
            {
                it->second = incoming;
                changed = true;
            }

            if (changed && onFieldsChanged_)
                onFieldsChanged_();

            if (auto live = ReadExposed(entity, name))
                return *live;
        }
        catch (const std::exception& e)
        {
            std::cerr
                << "expose(): "
                << e.what()
                << " for field '"
                << name
                << "'\n";
        }

        return value;
    }

    sol::state& lua_;
    Reflection& reflection_;
    entt::registry& registry_;
    EntityBus& bus_;
    std::function<void()> onFieldsChanged_;

    std::map<entt::entity, RuntimeScript> scripts_;

    // entity -> script path that failed to load (see TryLoad)
    std::map<entt::entity, std::string> failed_;
};