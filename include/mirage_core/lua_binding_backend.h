#pragma once

#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <sol/sol.hpp>

#include "mirage_core/script_api.h"

// Runtime-only consumer of ScriptApiRegistry. Keeping Sol2 registration here
// leaves the API metadata model independent of the Lua implementation.
class LuaBindingBackend
{
public:
    using Module = std::function<void(LuaBindingBackend&, sol::state&)>;

    struct ModuleEntry
    {
        std::string name;
        Module bind;
    };

    explicit LuaBindingBackend(ScriptApiRegistry& registry) : registry_(registry) {}

    ScriptApiRegistry& Registry() { return registry_; }
    const ScriptApiRegistry& Registry() const { return registry_; }

    void DescribeType(ScriptApiType description) { registry_.DescribeType(std::move(description)); }
    void DescribeTable(ScriptApiTable description) { registry_.DescribeTable(std::move(description)); }
    void DescribeTableFunction(const std::string& table, ScriptApiCallable function)
    { registry_.DescribeTableFunction(table, std::move(function)); }
    void DescribeGlobalFunction(const std::string& path, ScriptApiCallable function)
    { registry_.DescribeGlobalFunction(path, std::move(function)); }
    void DescribeEnum(ScriptApiEnum description) { registry_.DescribeEnum(std::move(description)); }
    void DescribeConstant(ScriptApiConstant description) { registry_.DescribeConstant(std::move(description)); }

    void AddModule(std::string name, Module bind)
    {
        if (name.empty() || !bind)
            throw std::invalid_argument("Lua binding module requires a name and binder");
        if (bound_ || binding_ || failed_)
            throw std::logic_error("Cannot add a Lua binding module after binding has started");
        if (!moduleNames_.insert(name).second)
            throw std::logic_error("Duplicate Lua binding module: " + name);
        modules_.push_back({ std::move(name), std::move(bind) });
    }

    void BindAll(sol::state& lua)
    {
        if (bound_ || binding_ || failed_)
            throw std::logic_error("Lua binding backend cannot be bound in its current state");

        binding_ = true;
        try
        {
            for (const ModuleEntry& module : modules_)
            {
                try
                {
                    module.bind(*this, lua);
                }
                catch (const std::exception& e)
                {
                    throw std::runtime_error(
                        "Failed to register Lua binding module '" + module.name + "': " + e.what());
                }
            }

            const std::vector<std::string> errors = registry_.ValidateMetadata();
            if (!errors.empty())
            {
                std::string message = "Script API metadata validation failed";
                for (const std::string& error : errors)
                    message += "\n - " + error;
                throw std::runtime_error(message);
            }

            binding_ = false;
            bound_ = true;
        }
        catch (...)
        {
            binding_ = false;
            failed_ = true;
            throw;
        }
    }

    bool IsBound() const { return bound_; }
    const std::vector<ModuleEntry>& Modules() const { return modules_; }

    // Register an enum as a Lua table of integer values. The registry retains a
    // typed enum descriptor in addition to its table representation.
    sol::table BindEnumTable(sol::state& lua, ScriptApiEnum description)
    {
        const std::string name = description.name;
        const auto values = description.values;
        registry_.DescribeEnum(std::move(description));
        const ScriptApiEnum& stored = registry_.Enums().at(name);
        sol::table table = CreateGlobalTable(lua,
            { name, stored.description, stored.internal, {} });
        for (const ScriptApiEnumValue& value : values)
        {
            sol::object existing = table.get<sol::object>(value.name);
            if (existing.valid() && existing.get_type() != sol::type::lua_nil)
                throw std::runtime_error("Lua enum value already exists: " + name + "." + value.name);
            table[value.name] = value.value;
        }
        return table;
    }

    template<class T>
    void BindConstant(sol::state& lua, ScriptApiConstant description, T&& value)
    {
        const std::string path = description.path;
        const std::vector<std::string> parts = SplitPath(path);
        if (parts.empty())
            throw std::invalid_argument("Invalid Lua constant path: " + path);
        registry_.DescribeConstant(std::move(description));

        sol::table scope = lua.globals();
        for (std::size_t i = 0; i + 1 < parts.size(); ++i)
        {
            sol::object next = scope.get<sol::object>(parts[i]);
            if (!next.valid() || next.get_type() == sol::type::lua_nil)
            {
                sol::table created = lua.create_table();
                scope[parts[i]] = created;
                scope = created;
            }
            else if (next.get_type() == sol::type::table)
                scope = next.as<sol::table>();
            else
                throw std::runtime_error("Lua constant path crosses a non-table value: " + parts[i]);
        }

        const std::string& leaf = parts.back();
        sol::object existing = scope.get<sol::object>(leaf);
        if (existing.valid() && existing.get_type() != sol::type::lua_nil)
            throw std::runtime_error("Lua constant already exists: " + path);
        scope[leaf] = std::forward<T>(value);
    }

    template<class T, class... Args>
    sol::usertype<T> BindUserType(sol::state& lua, ScriptApiType description, Args&&... args)
    {
        const std::string name = description.name;
        registry_.DescribeType(std::move(description));
        if (GlobalPathExists(lua, name))
            throw std::runtime_error("Lua type name is already in use: " + name);

        sol::usertype<T> type = [&]() -> sol::usertype<T>
        {
            if constexpr (sizeof...(Args) == 1 &&
                          (std::is_invocable_v<Args, sol::usertype<T>&> && ...))
            {
                sol::usertype<T> result = lua.new_usertype<T>(name);
                (std::forward<Args>(args)(result), ...);
                return result;
            }
            else
            {
                return lua.new_usertype<T>(name, std::forward<Args>(args)...);
            }
        }();

        lua.registry()["mirage.usertype." + name] = true;
        return type;
    }

    sol::table CreateGlobalTable(sol::state& lua, ScriptApiTable description)
    {
        const std::string name = description.name;
        registry_.DescribeTable(std::move(description));
        sol::object existing = lua.globals().get<sol::object>(name);
        if (existing.valid() && existing.get_type() != sol::type::lua_nil)
        {
            if (existing.get_type() == sol::type::table)
                return existing.as<sol::table>();
            throw std::runtime_error("Lua global name is already in use: " + name);
        }
        sol::table table = lua.create_table();
        lua.globals()[name] = table;
        return table;
    }

    sol::table CreateTable(sol::state_view lua, ScriptApiTable description)
    {
        registry_.DescribeTable(std::move(description));
        return lua.create_table();
    }

    void SetGlobalTable(sol::state& lua, const std::string& name, sol::table table,
                        ScriptApiTable description)
    {
        description.name = name;
        registry_.DescribeTable(std::move(description));
        sol::object existing = lua.globals().get<sol::object>(name);
        if (existing.valid() && existing.get_type() != sol::type::lua_nil &&
            existing.get_type() != sol::type::table)
            throw std::runtime_error("Lua global name is already in use: " + name);
        lua.globals()[name] = table;
    }

    template<class Fn>
    void BindTableFunction(sol::table table, const std::string& tableName,
                           ScriptApiCallable description, Fn&& function)
    {
        const std::string functionName = description.name;
        registry_.DescribeTableFunction(tableName, std::move(description));
        sol::object existing = table.get<sol::object>(functionName);
        if (existing.valid() && existing.get_type() != sol::type::lua_nil)
            throw std::runtime_error("Lua table function already exists: " + tableName + "." + functionName);
        table.set_function(functionName, std::forward<Fn>(function));
    }

    template<class Fn>
    void BindInternalTableFunction(sol::table table, const std::string& functionName, Fn&& function)
    {
        sol::object existing = table.get<sol::object>(functionName);
        if (existing.valid() && existing.get_type() != sol::type::lua_nil)
            throw std::runtime_error("Internal Lua table function already exists: " + functionName);
        table.set_function(functionName, std::forward<Fn>(function));
    }

    template<class Fn>
    void BindGlobalFunction(sol::state& lua, const std::string& path,
                            ScriptApiCallable description, Fn&& function,
                            bool replaceExisting = false)
    {
        const std::vector<std::string> parts = SplitPath(path);
        if (parts.empty())
            throw std::invalid_argument("Invalid Lua function path: " + path);

        sol::table scope = lua.globals();
        for (std::size_t i = 0; i + 1 < parts.size(); ++i)
        {
            sol::object next = scope.get<sol::object>(parts[i]);
            if (!next.valid() || next.get_type() == sol::type::lua_nil)
            {
                sol::table created = lua.create_table();
                scope[parts[i]] = created;
                scope = created;
            }
            else if (next.get_type() == sol::type::table)
                scope = next.as<sol::table>();
            else
                throw std::runtime_error("Lua path crosses a non-table value: " + parts[i]);
        }

        sol::object existing = scope.get<sol::object>(parts.back());
        if (!replaceExisting && existing.valid() && existing.get_type() != sol::type::lua_nil)
            throw std::runtime_error("Lua function already exists: " + path);
        registry_.DescribeGlobalFunction(path, std::move(description));
        scope.set_function(parts.back(), std::forward<Fn>(function));
    }

private:
    static std::vector<std::string> SplitPath(const std::string& path)
    {
        std::vector<std::string> parts;
        std::string part;
        for (char c : path)
        {
            if (c == '.')
            {
                if (part.empty()) return {};
                parts.push_back(std::move(part));
                part.clear();
            }
            else part += c;
        }
        if (part.empty()) return {};
        parts.push_back(std::move(part));
        return parts;
    }

    static bool GlobalPathExists(sol::state_view lua, const std::string& path)
    {
        const std::vector<std::string> parts = SplitPath(path);
        if (parts.empty()) return false;
        sol::object current = lua.globals();
        for (const std::string& part : parts)
        {
            if (current.get_type() != sol::type::table)
                return false;
            current = current.as<sol::table>().get<sol::object>(part);
            if (!current.valid() || current.get_type() == sol::type::lua_nil)
                return false;
        }
        return true;
    }

    ScriptApiRegistry& registry_;
    std::vector<ModuleEntry> modules_;
    std::set<std::string> moduleNames_;
    bool binding_ = false;
    bool bound_ = false;
    bool failed_ = false;
};
