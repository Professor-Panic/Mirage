#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include <sol/sol.hpp>
#include "raylib.h"
#include "entt.hpp"
#include "reflection.h"

// C++ Value -> Lua
inline sol::object ValueToLua(sol::state& lua, const Value& value)
{
    return std::visit([&](const auto& v) -> sol::object
    {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, Vector2>)
        {
            sol::table t = lua.create_table();
            t["x"] = v.x;
            t["y"] = v.y;
            return sol::make_object(lua, t);
        }
        else if constexpr (std::is_same_v<T, Vector3>)
        {
            sol::table t = lua.create_table();
            t["x"] = v.x;
            t["y"] = v.y;
            t["z"] = v.z;
            return sol::make_object(lua, t);
        }
        else if constexpr (std::is_same_v<T, Vector4>)
        {
            sol::table t = lua.create_table();
            t["x"] = v.x;
            t["y"] = v.y;
            t["z"] = v.z;
            t["w"] = v.w;
            return sol::make_object(lua, t);
        }
        else if constexpr (std::is_same_v<T, Color>)
        {
            sol::table t = lua.create_table();
            t["r"] = v.r;
            t["g"] = v.g;
            t["b"] = v.b;
            t["a"] = v.a;
            return sol::make_object(lua, t);
        }
        else if constexpr (std::is_same_v<T, entt::entity>)
        {
            return sol::make_object(
                lua, static_cast<std::uint32_t>(entt::to_integral(v)));
        }
        else
        {
            return sol::make_object(lua, v);
        }
    }, value);
}

// Lua -> C++ Value, using the existing C++ field to determine the type.
inline Value LuaToValue(sol::object obj, const Value& current)
{
    const bool isNil = !obj.valid() || obj == sol::lua_nil;

    // nil clears an EntityRef field; every other type rejects it.
    if (isNil && !std::holds_alternative<EntityRef>(current))
        throw std::runtime_error("Cannot assign nil to a component field");

    return std::visit([&](const auto& old) -> Value
    {
        using T = std::decay_t<decltype(old)>;

        if constexpr (std::is_same_v<T, bool>)
        {
            if (!obj.is<bool>())
                throw std::runtime_error("Expected boolean");
            return obj.as<bool>();
        }
        else if constexpr (std::is_same_v<T, int>)
        {
            if (!obj.is<int>())
                throw std::runtime_error("Expected integer");
            return obj.as<int>();
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            if (!obj.is<double>() && !obj.is<int>())
                throw std::runtime_error("Expected number");
            return obj.as<float>();
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            if (!obj.is<std::string>())
                throw std::runtime_error("Expected string");
            return obj.as<std::string>();
        }
        else if constexpr (std::is_same_v<T, Vector2>)
        {
            if (!obj.is<sol::table>())
                throw std::runtime_error("Expected Vector2 table");

            sol::table t = obj.as<sol::table>();
            return Vector2{
                t.get<float>("x"),
                t.get<float>("y")
            };
        }
        else if constexpr (std::is_same_v<T, Vector3>)
        {
            if (!obj.is<sol::table>())
                throw std::runtime_error("Expected Vector3 table");

            sol::table t = obj.as<sol::table>();
            return Vector3{
                t.get<float>("x"),
                t.get<float>("y"),
                t.get<float>("z")
            };
        }
        else if constexpr (std::is_same_v<T, Vector4>)
        {
            if (!obj.is<sol::table>())
                throw std::runtime_error("Expected Vector4 table");

            sol::table t = obj.as<sol::table>();
            return Vector4{
                t.get<float>("x"),
                t.get<float>("y"),
                t.get<float>("z"),
                t.get<float>("w")
            };
        }
        else if constexpr (std::is_same_v<T, Color>)
        {
            if (!obj.is<sol::table>())
                throw std::runtime_error("Expected Color table");

            sol::table t = obj.as<sol::table>();

            auto channel = [&](const char* key) -> unsigned char {
                int c = t.get<int>(key);
                if (c < 0 || c > 255)
                    throw std::runtime_error("Color channels must be 0-255");
                return static_cast<unsigned char>(c);
            };

            return Color{
                channel("r"),
                channel("g"),
                channel("b"),
                channel("a")
            };
        }
        else if constexpr (std::is_same_v<T, EntityRef>)
        {
            if (isNil)
                return EntityRef{};

            if (!obj.is<EntityRef>())
                throw std::runtime_error("Expected EntityRef (or nil)");

            return obj.as<EntityRef>();
        }
        else if constexpr (std::is_same_v<T, entt::entity>)
        {
            if (!obj.is<std::uint32_t>())
                throw std::runtime_error("Expected entity ID integer");

            return static_cast<entt::entity>(obj.as<std::uint32_t>());
        }
        else
        {
            static_assert(!sizeof(T), "Unhandled Value alternative");
        }
    }, current);
}
// Forward declaration: component-field proxies are defined before the
// live-value proxy implementation below.
template<typename T>
sol::table CreateLiveValueProxy(
    sol::state& lua,
    std::function<T()> get,
    std::function<void(const T&)> set);

// Creates a live proxy that re-resolves the component and field on every
// read/write. This avoids retaining pointers into EnTT component storage.
template<typename T>
inline sol::table CreateComponentFieldProxy(
    sol::state& lua,
    entt::registry& registry,
    entt::entity entity,
    const ComponentInfo& info,
    const std::string& fieldName)
{
    auto get = [&registry, entity, &info, fieldName]() -> T
    {
        auto* component = info.find(registry, entity);
        if (!component)
            throw std::runtime_error("Component no longer exists");

        for (const auto& field : info.fields)
        {
            if (field.name != fieldName)
                continue;

            Value value = field.get(component);
            if (const auto* typed = std::get_if<T>(&value))
                return *typed;

            throw std::runtime_error(
                "Reflected field type changed: " + fieldName);
        }

        throw std::runtime_error("Unknown component field: " + fieldName);
    };

    auto set = [&registry, entity, &info, fieldName](const T& value)
    {
        auto* component = info.find(registry, entity);
        if (!component)
            throw std::runtime_error("Component no longer exists");

        for (const auto& field : info.fields)
        {
            if (field.name != fieldName)
                continue;

            field.set(component, Value{value});
            return;
        }

        throw std::runtime_error("Unknown component field: " + fieldName);
    };

    return CreateLiveValueProxy<T>(lua, std::move(get), std::move(set));
}

inline sol::table CreateComponentProxy(
    sol::state& lua,
    entt::registry& registry,
    entt::entity entity,
    const ComponentInfo& info)
{
    sol::table proxy = lua.create_table();
    sol::table mt = lua.create_table();

    mt.set_function("__index",
        [&lua, &registry, entity, &info](
            sol::table,
            const std::string& fieldName) -> sol::object
        {
            auto* component = info.find(registry, entity);

            if (!component)
                return sol::make_object(lua, sol::lua_nil);

            for (auto& field : info.fields)
            {
                if (field.name == fieldName)
                {
                    // Composite fields return live proxies so nested writes,
                    // e.g. transform.position.x = 5, update the C++ component.
                    switch (field.type)
                    {
                        case FieldType::Vec2:
                            return sol::make_object(
                                lua, CreateComponentFieldProxy<Vector2>(
                                    lua, registry, entity, info, fieldName));
                        case FieldType::Vec3:
                            return sol::make_object(
                                lua, CreateComponentFieldProxy<Vector3>(
                                    lua, registry, entity, info, fieldName));
                        case FieldType::Vec4:
                            return sol::make_object(
                                lua, CreateComponentFieldProxy<Vector4>(
                                    lua, registry, entity, info, fieldName));
                        case FieldType::Color:
                            return sol::make_object(
                                lua, CreateComponentFieldProxy<Color>(
                                    lua, registry, entity, info, fieldName));
                        default:
                            return ValueToLua(lua, field.get(component));
                    }
                }
            }

            return sol::make_object(lua, sol::lua_nil);
        });

    mt.set_function("__newindex",
        [&registry, entity, &info](
            sol::table,
            const std::string& fieldName,
            sol::object luaValue)
        {
            auto* component = info.find(registry, entity);

            if (!component)
                throw std::runtime_error("Component no longer exists");

            for (auto& field : info.fields)
            {
                if (field.name != fieldName)
                    continue;

                Value current = field.get(component);
                Value converted = LuaToValue(luaValue, current);
                field.set(component, converted);
                return;
            }

            throw std::runtime_error(
                "Unknown component field: " + fieldName);
        });

    proxy[sol::metatable_key] = mt;
    return proxy;
}
// Reads and writes individual members of Vector2, Vector3,
// Vector4, and Color.
template<typename T>
double ReadProxyMember(const T& value, const std::string& key)
{
    if constexpr (std::is_same_v<T, Vector2>)
    {
        if (key == "x") return value.x;
        if (key == "y") return value.y;
    }
    else if constexpr (std::is_same_v<T, Vector3>)
    {
        if (key == "x") return value.x;
        if (key == "y") return value.y;
        if (key == "z") return value.z;
    }
    else if constexpr (std::is_same_v<T, Vector4>)
    {
        if (key == "x") return value.x;
        if (key == "y") return value.y;
        if (key == "z") return value.z;
        if (key == "w") return value.w;
    }
    else if constexpr (std::is_same_v<T, Color>)
    {
        if (key == "r") return value.r;
        if (key == "g") return value.g;
        if (key == "b") return value.b;
        if (key == "a") return value.a;
    }

    throw std::runtime_error("Unknown proxy member: " + key);
}

template<typename T>
void WriteProxyMember(
    T& value,
    const std::string& key,
    double number)
{
    if constexpr (std::is_same_v<T, Vector2>)
    {
        if (key == "x") { value.x = static_cast<float>(number); return; }
        if (key == "y") { value.y = static_cast<float>(number); return; }
    }
    else if constexpr (std::is_same_v<T, Vector3>)
    {
        if (key == "x") { value.x = static_cast<float>(number); return; }
        if (key == "y") { value.y = static_cast<float>(number); return; }
        if (key == "z") { value.z = static_cast<float>(number); return; }
    }
    else if constexpr (std::is_same_v<T, Vector4>)
    {
        if (key == "x") { value.x = static_cast<float>(number); return; }
        if (key == "y") { value.y = static_cast<float>(number); return; }
        if (key == "z") { value.z = static_cast<float>(number); return; }
        if (key == "w") { value.w = static_cast<float>(number); return; }
    }
    else if constexpr (std::is_same_v<T, Color>)
    {
        // Clamp channels to the valid 0–255 range.
        auto channel = static_cast<unsigned char>(
            number < 0.0 ? 0.0 : number > 255.0 ? 255.0 : number);

        if (key == "r") { value.r = channel; return; }
        if (key == "g") { value.g = channel; return; }
        if (key == "b") { value.b = channel; return; }
        if (key == "a") { value.a = channel; return; }
    }

    throw std::runtime_error("Unknown proxy member: " + key);
}

// Creates a live proxy for a Vector2, Vector3, Vector4, or Color.
// Get and Set must resolve the actual C++ field each time.
template<typename T>
sol::table CreateLiveValueProxy(
    sol::state& lua,
    std::function<T()> get,
    std::function<void(const T&)> set)
{
    static_assert(
        std::is_same_v<T, Vector2> ||
        std::is_same_v<T, Vector3> ||
        std::is_same_v<T, Vector4> ||
        std::is_same_v<T, Color>,
        "Unsupported live proxy type");

    sol::table proxy = lua.create_table();
    sol::table meta = lua.create_table();

    meta.set_function(
    "__index",
    [get](sol::this_state state,
          sol::table,
          const std::string& key) -> sol::object
    {
        sol::state_view lua(state);
        T current = get();

        try
        {
            return sol::make_object(
                lua,
                ReadProxyMember(current, key));
        }
        catch (const std::runtime_error&)
        {
            return sol::make_object(lua, sol::lua_nil);
        }
    });

    meta.set_function(
        "__newindex",
        [get, set](sol::table,
                   const std::string& key,
                   sol::object value)
        {
            if (value.get_type() != sol::type::number)
                throw sol::error(
                    "Vector/color members must be numbers");

            T current = get();

            WriteProxyMember(current, key, value.as<double>());

            set(current);
        });

    proxy[sol::metatable_key] = meta;
    return proxy;
}