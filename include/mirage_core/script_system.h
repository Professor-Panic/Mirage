#pragma once

#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sol/sol.hpp>
#include <entt/entt.hpp>
#include <raylib.h>

#include "mirage_core/reflection.h"
#include "mirage_core/lua_binding_backend.h"
#include "mirage_core/lua_native.h"
#include "mirage_core/entity_bus.h"
#include "mirage_core/event_bus.h"

// A list of scripts on one entity. The map key is the script's path; the value
// holds that script's exposed fields, so two scripts never share (or fight over)
// a field name. Each path can appear once per entity.
struct ScriptComponent
{
    std::map<std::string, std::map<std::string, Value>> scripts;

    ScriptComponent() = default;

    // Convenience for the common case of one script: ScriptComponent("a.lua").
    explicit ScriptComponent(std::string path)
    {
        scripts.try_emplace(std::move(path));
    }

    bool Has(const std::string& path) const { return scripts.count(path) != 0; }

    // false if the script is already attached or the path is empty.
    bool Add(const std::string& path)
    {
        return !path.empty() && scripts.try_emplace(path).second;
    }

    bool Remove(const std::string& path) { return scripts.erase(path) != 0; }
};

inline ScriptApiCallable ScriptContextApiCall(std::string name, std::string description,
                                               std::vector<ScriptApiParameter> parameters,
                                               std::vector<std::string> returns)
{
    ScriptApiCallable call{ std::move(name), std::move(description),
                            std::move(parameters), std::move(returns), false };
    call.customBinding = true;
    call.bindingNotes = "Implemented by Mirage's per-script context bridge; fields and component handles are resolved against the live entity/script instance.";
    return call;
}

class ScriptSystem
{
private:
    // One running script = one entity + one script path.
    using ScriptKey = std::pair<entt::entity, std::string>;

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

        // Lives exactly as long as this runtime. Events listeners registered by this
        // script hold a weak_ptr to it, so they stop being called the moment the
        // runtime is destroyed (unload, reload, entity destroyed).
        std::shared_ptr<bool> alive;

        bool started = false;
        bool updateDisabled = false;
    };

public:
    ScriptSystem(
        sol::state& lua,
        Reflection& reflection,
        entt::registry& registry,
        EntityBus& bus,
        EventBus& events,
        LuaBindingBackend& api)
        : lua_(lua),
          reflection_(reflection),
          registry_(registry),
          bus_(bus),
          events_(events),
          api_(api)
    {
        api_.DescribeTable({ "ScriptContext", "The per-script context table passed to each script factory.", false, {} });
        api_.DescribeTableFunction("ScriptContext", ScriptContextApiCall(
            "GetComponent", "Get a live native handle to a script-exposed component, or nil",
            { { "name", "string", false, false, {} } }, { "ComponentHandle | nil" }));
        api_.DescribeTableFunction("ScriptContext", ScriptContextApiCall(
            "expose", "Declare a persistent, inspector-visible script field",
            { { "name", "string", false, false, {} }, { "value", "any", false, false, {} } }, { "any" }));
        api_.DescribeTableFunction("ScriptContext", ScriptContextApiCall(
            "GetEntity", "Return the EntityRef belonging to this script instance", {}, { "EntityRef" }));
    }

    // Called after the runtime has populated Reflection and before BindAll().
    // The constructor runs before runtime.cpp registers its built-in components,
    // so reflected literal-name overloads must be added in this later phase.
    void DescribeReflectedApi()
    {
        // The same context functions are also installed as bare names in each
        // script's isolated environment (see Load/InstallEnvironmentMeta).
        // Describe those environment globals too, otherwise `_engine.lua`
        // would document self:GetComponent() but leave GetComponent() and
        // expose() looking undefined to LuaLS.
        api_.DescribeGlobalFunction("GetComponent", ScriptContextApiCall(
            "GetComponent", "Get a live native handle to a script-exposed component, or nil",
            { { "name", "string", false, false, {} } }, { "ComponentHandle | nil" }));
        api_.DescribeGlobalFunction("expose", ScriptContextApiCall(
            "expose", "Declare a persistent, inspector-visible script field",
            { { "name", "string", false, false, {} }, { "value", "any", false, false, {} } }, { "any" }));

        for (const auto& [registeredName, component] : reflection_.All())
        {
            if (!component.scriptExposed)
                continue;

            const std::string scriptName = component.scriptName.empty()
                ? registeredName : component.scriptName;
            const std::string literalName = "\"" + scriptName + "\"";
            const std::string description = "Get a live " + scriptName + " component handle, or nil";
            api_.DescribeTableFunction("ScriptContext", ScriptContextApiCall(
                "GetComponent", description,
                { { "name", literalName, false, false, {} } },
                { scriptName + " | nil" }));
            api_.DescribeGlobalFunction("GetComponent", ScriptContextApiCall(
                "GetComponent", description,
                { { "name", literalName, false, false, {} } },
                { scriptName + " | nil" }));
        }
    }

    // Convert supported Lua values into the engine's reflected Value type.
    Value LuaToValue(sol::object value)
    {
        switch (value.get_type())
        {
        case sol::type::boolean:
            return value.as<bool>();

        case sol::type::number:
        {
            const double number = value.as<double>();

#if LUA_VERSION_NUM >= 503
            // Lua 5.3+ has a separate integer subtype: 2.5 and 5.0 are floats.
            if (value.is<int>())
                return value.as<int>();
            return static_cast<float>(number);
#else
            // LuaJIT / Lua 5.1 / 5.2 have ONE number type, so sol2's is<int>()
            // is true for every number (2.5 would silently become 2). Here a
            // number is an int only if it is a whole number that fits in one.
            if (std::isfinite(number) && number == std::floor(number) &&
                number >= static_cast<double>(std::numeric_limits<int>::min()) &&
                number <= static_cast<double>(std::numeric_limits<int>::max()))
                return static_cast<int>(number);
            return static_cast<float>(number);
#endif
        }

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
            if (value.is<LuaLiveValueHandle>())
                return value.as<LuaLiveValueHandle>().Read();

            if (value.is<Vector2>()) return value.as<Vector2>();
            if (value.is<Vector3>()) return value.as<Vector3>();
            if (value.is<Vector4>()) return value.as<Vector4>();
            if (value.is<Color>())   return value.as<Color>();

            if (value.is<EntityRef>())
                return value.as<EntityRef>();

            throw std::runtime_error(
                "Unsupported Lua userdata: expected a Vector, Color or EntityRef");

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
    // `path` is the script this table belongs to: its exposed fields are kept
    // under that path in the entity's ScriptComponent.
    sol::table CreateSelf(entt::entity entity, const std::string& path)
    {
        sol::table self = lua_.create_table();

        api_.BindTableFunction(
            self, "ScriptContext",
            ScriptContextApiCall("GetComponent", "Get a live native handle to a script-exposed component, or nil",
                { { "name", "string", false, false, {} } }, { "ComponentHandle | nil" }),
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

        api_.BindTableFunction(
            self, "ScriptContext",
            ScriptContextApiCall("expose", "Declare a persistent, inspector-visible script field",
                { { "name", "string", false, false, {} }, { "value", "any", false, false, {} } }, { "any" }),
            sol::overload(
                [this, entity, path](
                    sol::table,
                    const std::string& name,
                    sol::object value) -> sol::object
                {
                    return ExposeFor(entity, path, name, value);
                },
                [this, entity, path](
                    const std::string& name,
                    sol::object value) -> sol::object
                {
                    return ExposeFor(entity, path, name, value);
                }));

        // self:GetEntity() -> EntityRef to this script's own entity.
        api_.BindTableFunction(
            self, "ScriptContext",
            ScriptContextApiCall("GetEntity", "Return the EntityRef belonging to this script instance", {}, { "EntityRef" }),
            sol::overload(
                [this, entity](sol::table) { return bus_.Register(entity); },
                [this, entity]() { return bus_.Register(entity); }));

        // self.speed reads/writes exposed fields live (same lookup as bare names).
        sol::table meta = lua_.create_table();

        api_.BindInternalTableFunction(
            meta, "__index",
            [this, entity, path](sol::table, sol::object key) -> sol::object
            {
                if (key.is<std::string>())
                    if (auto exposed = ReadExposed(entity, path, key.as<std::string>()))
                        return *exposed;

                return sol::make_object(lua_, sol::lua_nil);
            });

        api_.BindInternalTableFunction(
            meta, "__newindex",
            [this, entity, path](sol::table tbl, sol::object key, sol::object value)
            {
                if (key.is<std::string>() &&
                    WriteExposed(entity, path, key.as<std::string>(), value))
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
    //
    // An entity can carry several scripts (ScriptComponent::scripts); this
    // loads the one stored under `path`.
    bool LoadScript(entt::entity entity, const std::string& path)
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

        if (path.empty())
        {
            std::cerr
                << "LoadScript(): script path is empty\n";
            return false;
        }

        if (!component.Has(path))
        {
            std::cerr
                << "LoadScript(): entity has no script '"
                << path
                << "'\n";
            return false;
        }

        // Record which names this load's factory exposes (see PruneExposed).
        // Only the factory run counts: expose() from Start() happens later.
        exposedNames_[ScriptKey{entity, path}].clear();
        loadingKey_ = ScriptKey{entity, path};
        struct LoadingGuard
        {
            std::optional<ScriptKey>& slot;
            ~LoadingGuard() { slot.reset(); }
        } loadingGuard{ loadingKey_ };

        RuntimeScript runtime;
        runtime.loadedPath = path;

        // Each entity gets its own Lua environment.
        runtime.environment = sol::environment(lua_, sol::create);

        runtime.self = CreateSelf(entity, path);

        // Expose the entity API inside the script environment.
        runtime.environment["self"] = runtime.self;
        runtime.environment["expose"] = runtime.self["expose"];
        runtime.environment["GetComponent"] =
            runtime.self["GetComponent"];

        // This script's own Events: listeners it registers die with it.
        runtime.alive = std::make_shared<bool>(true);
        runtime.environment["Events"] =
            MakeEventsTable(lua_, events_, true, runtime.alive, api_);

        // Exposed fields become live members of the script: the bare name
        // (speed) reads and writes this script's entry in
        // ScriptComponent::scripts directly.
        InstallEnvironmentMeta(runtime.environment, entity, path);

#if LUA_VERSION_NUM < 503
        floatLiterals_[path] = FindFloatExposeNames(path);
#endif

        // Execute the chunk. The chunk must return a factory function.
        // script_pass_on_error: a missing file or a syntax error comes back as
        // an invalid result (handled below) instead of throwing.
        sol::protected_function_result chunkResult =
            lua_.safe_script_file(
                path,
                runtime.environment,
                sol::script_pass_on_error);

        if (!chunkResult.valid())
        {
            sol::error err = chunkResult;

            std::cerr
                << "Loading script '"
                << path
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
                << path
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
                << path
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
        scripts_[ScriptKey{entity, path}] = std::move(runtime);

        return true;
    }

    // Invoke Start() once for this runtime.
    bool StartScript(entt::entity entity, const std::string& path)
    {
        const ScriptKey key{entity, path};
        auto it = scripts_.find(key);

        if (it == scripts_.end())
        {
            if (!LoadScript(entity, path))
                return false;

            it = scripts_.find(key);

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
                << "Start() failed in '"
                << path
                << "' (entity "
                << static_cast<std::uint32_t>(entity)
                << "): "
                << err.what()
                << '\n';

            return false;
        }

        return true;
    }

    // Load and start scripts as necessary, then invoke Update(dt) on every
    // script of every entity.
    void UpdateScripts(float dt)
    {
        // Copy the keys first so script callbacks can modify the registry
        // (or add and remove scripts) without invalidating the iteration.
        const std::vector<ScriptKey> keys = CollectScriptKeys();

        for (const ScriptKey& key : keys)
        {
            const entt::entity entity = key.first;
            const std::string& path = key.second;

            // An earlier script this frame may have removed it.
            if (!ScriptExists(key))
                continue;

            auto it = scripts_.find(key);

            // Load if it has no runtime yet.
            if (it == scripts_.end())
            {
                if (!TryLoad(entity, path))
                    continue;

                it = scripts_.find(key);

                if (it == scripts_.end())
                    continue;
            }

            // StartScript is safe to call repeatedly.
            if (!it->second.started)
                StartScript(entity, path);

            // Re-find the runtime after invoking Lua.
            it = scripts_.find(key);

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
                    << "Update() failed in '"
                    << path
                    << "' (entity "
                    << static_cast<std::uint32_t>(entity)
                    << "): "
                    << err.what()
                    << '\n';

                // Avoid repeatedly invoking a callback that has failed.
                runtime.updateDisabled = true;
            }
        }

        RemoveStaleRuntimes();

        // Events.Post()ed this tick are delivered once every script has run.
        events_.Flush();
    }

    // Edit mode: run every script's factory (so expose() registers its
    // fields for the Inspector) without calling Start() or Update().
    // Cheap to call every frame: it only loads scripts that have no runtime
    // yet, and a script that failed to load is not retried until ReloadAll()
    // is called (or it is removed and added again).
    //
    // Hot reload: once a second, every .lua file used by a Script component is
    // hashed (its contents, not its timestamp). If the hash differs from the one
    // the running copy was loaded from, that script is reloaded, so a new
    // expose() line shows up in the Inspector without detaching and re-attaching
    // the script. A script that failed to load is retried once its file changes.
    void EnsureLoaded()
    {
        const std::vector<ScriptKey> keys = CollectScriptKeys();

        const auto now = std::chrono::steady_clock::now();
        const bool checkFiles = now >= nextFileCheck_;
        if (checkFiles)
            nextFileCheck_ = now + std::chrono::seconds(1);

        // Each distinct file is read and hashed once per check, however many
        // entities use it.
        std::map<std::string, std::optional<std::uint64_t>> currentHashes;
        bool reloaded = false;

        for (const ScriptKey& key : keys)
        {
            if (!ScriptExists(key))
                continue;

            // Names the previous copy exposed, set only when this key is reloaded
            // because its file changed.
            std::optional<std::set<std::string>> previousNames;

            if (checkFiles)
            {
                const auto known = hashes_.find(key);
                if (known != hashes_.end())
                {
                    auto cached = currentHashes.find(key.second);
                    if (cached == currentHashes.end())
                        cached = currentHashes.emplace(key.second, FileHash(key.second)).first;

                    // An unreadable or missing file keeps the running copy;
                    // only a real content change reloads.
                    if (cached->second && *cached->second != known->second)
                    {
                        const auto names = exposedNames_.find(key);
                        if (names != exposedNames_.end())
                            previousNames = std::move(names->second);
                        scripts_.erase(key);
                        failed_.erase(key);
                        hashes_.erase(key);
                        exposedNames_.erase(key);
                        reloaded = true;
                    }
                }
            }

            if (scripts_.find(key) == scripts_.end())
            {
                const bool loaded = TryLoad(key.first, key.second);

                // The new version loaded: fields the old version exposed that the
                // new one no longer does are gone from the Inspector too. A
                // failed load (e.g. a syntax error mid-edit) keeps everything.
                if (loaded && previousNames)
                    PruneExposed(key, *previousNames);
            }
        }

        RemoveStaleRuntimes();

        // The set of exposed fields may have changed even if no single expose()
        // reported a change (a field removed, a type or order changed).
        if (reloaded && onFieldsChanged_)
            onFieldsChanged_();
    }

    // Throw away every runtime and load them all again, fresh and not yet
    // started. Exposed field values stored in ScriptComponent::scripts are
    // kept (expose() only supplies defaults). Re-reads the files from disk.
    // Call it when switching between Edit and Play.
    void ReloadAll()
    {
        scripts_.clear();
        events_.Clear();                // nothing from the previous run may fire or stay queued
        failed_.clear();
        hashes_.clear();
        exposedNames_.clear();
        EnsureLoaded();
    }

    // The script's PUBLIC interface: the table its factory returned, so whatever
    // the author put in `return { Start = ..., Update = ..., Foo = Foo }` is
    // reachable (locals that are not in that table never are). nil if the entity
    // or that script does not exist, or its runtime has not loaded yet (a script
    // added this tick loads on the next one). The table is replaced when the
    // script is reloaded, so do not keep it across frames.
    sol::object GetInstance(entt::entity entity, const std::string& path)
    {
        const auto it = scripts_.find(ScriptKey{entity, path});
        if (it == scripts_.end() || !it->second.instance.valid())
            return sol::make_object(lua_, sol::lua_nil);

        return sol::make_object(lua_, it->second.instance);
    }

    // Same, for an entity that has exactly one script. With none or several
    // attached it is ambiguous, so it returns nil: pass the path then.
    sol::object GetInstance(entt::entity entity)
    {
        const auto* component = registry_.try_get<ScriptComponent>(entity);
        if (!component || component->scripts.size() != 1)
            return sol::make_object(lua_, sol::lua_nil);

        return GetInstance(entity, component->scripts.begin()->first);
    }

    // Drop every runtime that belongs to `entity`.
    void UnloadScript(entt::entity entity)
    {
        EraseEntity(scripts_, entity);
        EraseEntity(failed_, entity);
        EraseEntity(hashes_, entity);
        EraseEntity(exposedNames_, entity);
    }

    // Drop one script's runtime.
    void UnloadScript(entt::entity entity, const std::string& path)
    {
        scripts_.erase(ScriptKey{entity, path});
        failed_.erase(ScriptKey{entity, path});
        hashes_.erase(ScriptKey{entity, path});
        exposedNames_.erase(ScriptKey{entity, path});
    }

    void UnloadAll()
    {
        scripts_.clear();
        events_.Clear();
        failed_.clear();
        hashes_.clear();
        exposedNames_.clear();
    }

    // Called when a script exposes a new field (or changes a field's type),
    // so UI lists built from ScriptComponent::fields can refresh.
    void OnFieldsChanged(std::function<void()> fn)
    {
        onFieldsChanged_ = std::move(fn);
    }

private:
    // Every (entity, script path) pair currently in the registry.
    std::vector<ScriptKey> CollectScriptKeys()
    {
        std::vector<ScriptKey> keys;

        for (entt::entity entity : registry_.view<ScriptComponent>())
            for (const auto& entry : registry_.get<ScriptComponent>(entity).scripts)
                keys.emplace_back(entity, entry.first);

        return keys;
    }

    // True while the entity is alive and still has this script attached.
    bool ScriptExists(const ScriptKey& key)
    {
        if (!registry_.valid(key.first))
            return false;

        const auto* sc = registry_.try_get<ScriptComponent>(key.first);

        return sc && sc->Has(key.second);
    }

    template <class Mapped>
    static void EraseEntity(std::map<ScriptKey, Mapped>& container, entt::entity entity)
    {
        for (auto it = container.begin(); it != container.end();)
        {
            if (it->first.first == entity)
                it = container.erase(it);
            else
                ++it;
        }
    }

    static void EraseEntity(std::set<ScriptKey>& container, entt::entity entity)
    {
        for (auto it = container.begin(); it != container.end();)
        {
            if (it->first == entity)
                it = container.erase(it);
            else
                ++it;
        }
    }

    // LoadScript with a memory of failures, so a broken script prints its
    // error once instead of every frame.
    bool TryLoad(entt::entity entity, const std::string& path)
    {
        const ScriptKey key{entity, path};

        if (failed_.count(key))
            return false;

        // Remember the file's content hash BEFORE loading it, so a save that lands
        // while the script loads still triggers another reload (see EnsureLoaded).
        RecordHash(key);

        bool ok = false;

        try
        {
            ok = LoadScript(entity, path);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Loading script '" << path << "' failed: "
                      << e.what() << '\n';
        }

        if (ok)
        {
            failed_.erase(key);
            return true;
        }

        failed_.insert(key);
        return false;
    }

#if LUA_VERSION_NUM < 503
    // Names passed to expose() with a float literal (5.0, 5., .5, 1e3). Only
    // used on LuaJIT / Lua 5.1, where 5.0 and 5 are the same number at run time.
    // It is a source scan, so it only sees literal arguments:
    //   expose("speed", 5.0)   -> float
    //   expose("speed", 2 * 2.0)  -> still an int (4); write 4.0 instead
    static std::set<std::string> FindFloatExposeNames(const std::string& path)
    {
        std::set<std::string> names;

        std::ifstream file(path, std::ios::binary);
        if (!file)
            return names;     // a missing file is reported by the load itself

        const std::string source((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());

        static const std::regex pattern(
            R"re(\bexpose\s*\(\s*(["'])([A-Za-z_][A-Za-z0-9_]*)\1\s*,\s*[+-]?(?:\d+\.\d*|\.\d+|\d+[eE][+-]?\d+))re");

        for (std::sregex_iterator it(source.begin(), source.end(), pattern), end;
             it != end; ++it)
            names.insert((*it)[2].str());

        return names;
    }
#endif

    // Remove stored fields that `previous` (the old copy's expose() names) had
    // and the freshly loaded copy did not expose. Fields that were never exposed
    // by a factory run in this session (loaded from a scene, or exposed from
    // Start()) are not touched.
    void PruneExposed(const ScriptKey& key, const std::set<std::string>& previous)
    {
        auto* component = registry_.try_get<ScriptComponent>(key.first);
        if (!component)
            return;

        const auto fields = component->scripts.find(key.second);
        if (fields == component->scripts.end())
            return;

        const auto current = exposedNames_.find(key);
        for (const std::string& name : previous)
        {
            const bool stillExposed =
                current != exposedNames_.end() && current->second.count(name) != 0;
            if (!stillExposed)
                fields->second.erase(name);
        }
    }

    // 64-bit FNV-1a of the file's bytes; nullopt if it cannot be read.
    static std::optional<std::uint64_t> FileHash(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return std::nullopt;

        std::uint64_t hash = 14695981039346656037ull;
        char buffer[4096];
        while (file.read(buffer, sizeof buffer) || file.gcount() > 0)
        {
            const std::streamsize count = file.gcount();
            for (std::streamsize i = 0; i < count; ++i)
            {
                hash ^= static_cast<unsigned char>(buffer[i]);
                hash *= 1099511628211ull;
            }
        }
        return hash;
    }

    void RecordHash(const ScriptKey& key)
    {
        if (const auto hash = FileHash(key.second))
            hashes_[key] = *hash;
    }

    // Remove runtimes for deleted entities, removed script components, or
    // scripts that were detached from their entity.
    void RemoveStaleRuntimes()
    {
        for (auto it = hashes_.begin(); it != hashes_.end();)
        {
            if (!ScriptExists(it->first))
                it = hashes_.erase(it);
            else
                ++it;
        }

        for (auto it = exposedNames_.begin(); it != exposedNames_.end();)
        {
            if (!ScriptExists(it->first))
                it = exposedNames_.erase(it);
            else
                ++it;
        }

        for (auto it = scripts_.begin(); it != scripts_.end();)
        {
            if (!ScriptExists(it->first))
                it = scripts_.erase(it);
            else
                ++it;
        }

        for (auto it = failed_.begin(); it != failed_.end();)
        {
            if (!ScriptExists(*it))
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

        const ComponentInfo* info = reflection_.FindScriptExposed(name);

        if (!info || !info->scriptExposed)
        {
            std::cerr
                << "GetComponent: unknown or non-script-exposed component '"
                << name
                << "'\n";

            return sol::make_object(lua_, sol::lua_nil);
        }

        if (!info->find(registry_, entity))
        {
            // The component is registered, but this entity doesn't have it.
            return sol::make_object(lua_, sol::lua_nil);
        }

        return MakeLuaComponentHandle(lua_, registry_, entity, *info);
    }

    // ---- Exposed fields --------------------------------------------------
    //
    // Exposed fields live only in ScriptComponent::scripts[path]. Scripts reach
    // them through the environment (bare name) or self.<n>; neither keeps its
    // own copy, so Inspector edits are seen immediately and script writes show
    // up in the Inspector. Vectors/colors are returned as native live-value
    // userdata, so even `local offset = offset_field` stays live. A local copy
    // of a plain number/bool/string is still just a copy, exactly like in Unity.
    //
    // Every script on an entity has its own field table, so two scripts can
    // both expose "speed" without touching each other.

    // The exposed-field table of one script, or nullptr if the entity, its
    // ScriptComponent or that script no longer exists.
    std::map<std::string, Value>* FieldsFor(
        entt::entity entity,
        const std::string& path)
    {
        if (!registry_.valid(entity))
            return nullptr;

        auto* sc = registry_.try_get<ScriptComponent>(entity);

        if (!sc)
            return nullptr;

        auto it = sc->scripts.find(path);

        return it == sc->scripts.end() ? nullptr : &it->second;
    }

    sol::object ExposedValueHandle(
        entt::entity entity,
        const std::string& path,
        const std::string& name)
    {
        auto get = [this, entity, path, name]() -> Value
        {
            auto* fields = FieldsFor(entity, path);
            if (!fields)
                throw std::runtime_error("Exposed field no longer exists: " + name);

            const auto it = fields->find(name);
            if (it == fields->end())
                throw std::runtime_error("Exposed field no longer exists: " + name);
            return it->second;
        };

        auto set = [this, entity, path, name](Value value)
        {
            auto* fields = FieldsFor(entity, path);
            if (!fields)
                throw std::runtime_error("Exposed field no longer exists: " + name);

            const auto it = fields->find(name);
            if (it == fields->end())
                throw std::runtime_error("Exposed field no longer exists: " + name);
            if (it->second.index() != value.index())
                throw std::runtime_error("Exposed field type changed: " + name);
            it->second = std::move(value);
        };

        return sol::make_object(
            lua_, LuaLiveValueHandle{ std::move(get), std::move(set) });
    }

    sol::optional<sol::object> ReadExposed(
        entt::entity entity,
        const std::string& path,
        const std::string& name)
    {
        auto* fields = FieldsFor(entity, path);

        if (!fields)
            return sol::nullopt;

        auto it = fields->find(name);

        if (it == fields->end())
            return sol::nullopt;

        const Value& v = it->second;

        if (std::holds_alternative<Vector2>(v) ||
            std::holds_alternative<Vector3>(v) ||
            std::holds_alternative<Vector4>(v) ||
            std::holds_alternative<Color>(v))
            return ExposedValueHandle(entity, path, name);

        return ValueToLua(lua_, v);
    }

    // Returns false if `name` is not an exposed field (caller stores it as an
    // ordinary script variable). Throws a Lua error on a type mismatch.
    bool WriteExposed(
        entt::entity entity,
        const std::string& path,
        const std::string& name,
        const sol::object& value)
    {
        auto* fields = FieldsFor(entity, path);

        if (!fields)
            return false;

        auto it = fields->find(name);

        if (it == fields->end())
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
    void InstallEnvironmentMeta(
        sol::environment& env,
        entt::entity entity,
        const std::string& path)
    {
        sol::table vars = lua_.create_table();
        sol::table meta = lua_.create_table();

        api_.BindInternalTableFunction(
            meta, "__index",
            [this, entity, path, vars](sol::table, sol::object key) -> sol::object
            {
                if (key.is<std::string>())
                    if (auto exposed = ReadExposed(entity, path, key.as<std::string>()))
                        return *exposed;

                sol::table own = vars;
                sol::object found = own.raw_get<sol::object>(key);

                if (found != sol::lua_nil)
                    return found;

                return lua_.globals().get<sol::object>(key);
            });

        api_.BindInternalTableFunction(
            meta, "__newindex",
            [this, entity, path, vars](sol::table, sol::object key, sol::object value)
            {
                if (key.is<std::string>() &&
                    WriteExposed(entity, path, key.as<std::string>(), value))
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
        const std::string& path,
        const std::string& name,
        sol::object value)
    {
        try
        {
            auto* fields = FieldsFor(entity, path);

            if (!fields)
            {
                throw std::runtime_error(
                    "Entity no longer has this script");
            }

            Value incoming = LuaToValue(value);

            if (loadingKey_ && loadingKey_->first == entity && loadingKey_->second == path)
                exposedNames_[*loadingKey_].insert(name);

#if LUA_VERSION_NUM < 503
            // expose("speed", 5.0): LuaJIT hands us 5, so use what the source said.
            if (std::holds_alternative<int>(incoming))
            {
                const auto hints = floatLiterals_.find(path);
                if (hints != floatLiterals_.end() && hints->second.count(name))
                    incoming = static_cast<float>(std::get<int>(incoming));
            }
#endif

            auto [it, inserted] = fields->try_emplace(name, incoming);
            bool changed = inserted;

#if LUA_VERSION_NUM < 503
            // Without an integer subtype a whole-number default (expose("speed", 5))
            // cannot be told from a float one, so a stored float (from the
            // Inspector or a scene file) must not be replaced by that int.
            const bool wholeNumberForFloat =
                !inserted &&
                std::holds_alternative<float>(it->second) &&
                std::holds_alternative<int>(incoming);
#else
            const bool wholeNumberForFloat = false;
#endif

            if (!inserted && !wholeNumberForFloat &&
                it->second.index() != incoming.index())
            {
                it->second = incoming;
                changed = true;
            }

            if (changed && onFieldsChanged_)
                onFieldsChanged_();

            if (auto live = ReadExposed(entity, path, name))
                return *live;
        }
        catch (const std::exception& e)
        {
            std::cerr
                << "expose(): "
                << e.what()
                << " for field '"
                << name
                << "' in '"
                << path
                << "'\n";
        }

        return value;
    }

    sol::state& lua_;
    Reflection& reflection_;
    entt::registry& registry_;
    EntityBus& bus_;
    EventBus& events_;
    LuaBindingBackend& api_;
    std::function<void()> onFieldsChanged_;

    // Content hash of the file each (entity, script) pair was loaded from;
    // EnsureLoaded() reloads the script when the file's hash no longer matches.
    std::map<ScriptKey, std::uint64_t> hashes_;

    // Names each script's factory exposed on its last load, and the script
    // currently inside its factory (so expose() knows whether to record).
    std::map<ScriptKey, std::set<std::string>> exposedNames_;
    std::optional<ScriptKey> loadingKey_;
    std::chrono::steady_clock::time_point nextFileCheck_{};

#if LUA_VERSION_NUM < 503
    // Names exposed with a float literal (expose("speed", 5.0)), per script file.
    // LuaJIT cannot tell 5.0 from 5 at run time, so the source is scanned.
    std::map<std::string, std::set<std::string>> floatLiterals_;
#endif

    // (entity, script path) -> running script
    std::map<ScriptKey, RuntimeScript> scripts_;

    // (entity, script path) pairs that failed to load (see TryLoad)
    std::set<ScriptKey> failed_;
};