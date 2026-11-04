#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <sol/sol.hpp>
#include <raylib.h>
#include <entt/entt.hpp>

#include "mirage_core/reflection.h"
#include "mirage_core/lua_binding_backend.h"
#include "mirage_core/lua_functions.h"

// ---------------------------------------------------------------------------
// Reflected Value <-> Lua conversion.
// ---------------------------------------------------------------------------

inline sol::object ValueToLua(sol::state_view lua, const Value& value)
{
    return std::visit([&](const auto& v) -> sol::object
    {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, Vector2> ||
                      std::is_same_v<T, Vector3> ||
                      std::is_same_v<T, Vector4> ||
                      std::is_same_v<T, Color>)
        {
            const char* name = nullptr;
            if constexpr (std::is_same_v<T, Vector2>) name = "Vector2";
            else if constexpr (std::is_same_v<T, Vector3>) name = "Vector3";
            else if constexpr (std::is_same_v<T, Vector4>) name = "Vector4";
            else name = "Color";

            if (LuaTypeBound(lua, name))
                return sol::make_object(lua, v);

            // Defensive fallback for a host that converts values before the
            // native value types have been registered.
            sol::table table = lua.create_table();
            if constexpr (std::is_same_v<T, Vector2>)
            {
                table["x"] = v.x;
                table["y"] = v.y;
            }
            else if constexpr (std::is_same_v<T, Vector3>)
            {
                table["x"] = v.x;
                table["y"] = v.y;
                table["z"] = v.z;
            }
            else if constexpr (std::is_same_v<T, Vector4>)
            {
                table["x"] = v.x;
                table["y"] = v.y;
                table["z"] = v.z;
                table["w"] = v.w;
            }
            else
            {
                table["r"] = v.r;
                table["g"] = v.g;
                table["b"] = v.b;
                table["a"] = v.a;
            }
            return sol::make_object(lua, table);
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

// Convert a Lua value using the existing reflected field value to determine
// the expected type. Live value userdata is unwrapped without making a proxy
// table or retaining a pointer into engine storage.
inline Value LuaToValue(sol::object object, const Value& current)
{
    const bool isNil = !object.valid() || object == sol::lua_nil;

    if (isNil && !std::holds_alternative<EntityRef>(current))
        throw std::runtime_error("Cannot assign nil to a component field");

    if (!isNil && object.is<LuaLiveValueHandle>())
    {
        Value value = object.as<LuaLiveValueHandle>().Read();
        if (value.index() != current.index())
            throw std::runtime_error("Live value has the wrong field type");
        return value;
    }

    return std::visit([&](const auto& old) -> Value
    {
        using T = std::decay_t<decltype(old)>;

        if constexpr (std::is_same_v<T, bool>)
        {
            if (!object.is<bool>()) throw std::runtime_error("Expected boolean");
            return object.as<bool>();
        }
        else if constexpr (std::is_same_v<T, int>)
        {
            if (!object.is<int>()) throw std::runtime_error("Expected integer");
            return object.as<int>();
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            if (!object.is<double>() && !object.is<int>())
                throw std::runtime_error("Expected number");
            return object.as<float>();
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            if (!object.is<std::string>()) throw std::runtime_error("Expected string");
            return object.as<std::string>();
        }
        else if constexpr (std::is_same_v<T, Vector2>)
        {
            if (object.is<Vector2>()) return object.as<Vector2>();
            if (!object.is<sol::table>()) throw std::runtime_error("Expected Vector2 (or table)");
            sol::table table = object.as<sol::table>();
            return Vector2{ table.get<float>("x"), table.get<float>("y") };
        }
        else if constexpr (std::is_same_v<T, Vector3>)
        {
            if (object.is<Vector3>()) return object.as<Vector3>();
            if (!object.is<sol::table>()) throw std::runtime_error("Expected Vector3 (or table)");
            sol::table table = object.as<sol::table>();
            return Vector3{ table.get<float>("x"), table.get<float>("y"), table.get<float>("z") };
        }
        else if constexpr (std::is_same_v<T, Vector4>)
        {
            if (object.is<Vector4>()) return object.as<Vector4>();
            if (!object.is<sol::table>()) throw std::runtime_error("Expected Vector4 (or table)");
            sol::table table = object.as<sol::table>();
            return Vector4{
                table.get<float>("x"), table.get<float>("y"),
                table.get<float>("z"), table.get<float>("w")
            };
        }
        else if constexpr (std::is_same_v<T, Color>)
        {
            if (object.is<Color>()) return object.as<Color>();
            if (!object.is<sol::table>()) throw std::runtime_error("Expected Color (or table)");
            sol::table table = object.as<sol::table>();
            auto channel = [&](const char* key) -> unsigned char
            {
                const int value = table.get<int>(key);
                if (value < 0 || value > 255)
                    throw std::runtime_error("Color channels must be 0-255");
                return static_cast<unsigned char>(value);
            };
            return Color{ channel("r"), channel("g"), channel("b"), channel("a") };
        }
        else if constexpr (std::is_same_v<T, EntityRef>)
        {
            if (isNil) return EntityRef{};
            if (!object.is<EntityRef>()) throw std::runtime_error("Expected EntityRef (or nil)");
            return object.as<EntityRef>();
        }
        else if constexpr (std::is_same_v<T, entt::entity>)
        {
            if (!object.is<std::uint32_t>())
                throw std::runtime_error("Expected entity ID integer");
            return static_cast<entt::entity>(object.as<std::uint32_t>());
        }
        else
        {
            static_assert(!sizeof(T), "Unhandled Value alternative");
        }
    }, current);
}

// ---------------------------------------------------------------------------
// Native reflected component handle.
//
// The handle stores EnTT's versioned entity ID and immutable reflection
// metadata. It does NOT store a component pointer. EnTT validates the entity
// generation on every access, so destruction and index reuse cannot make this
// handle refer to a different entity. Component storage is resolved on demand.
struct LuaComponentHandle
{
    entt::registry* registry = nullptr;
    entt::entity entity = entt::null;
    const ComponentInfo* componentInfo = nullptr;
    // Cache one native live-value userdata per composite field, avoiding a new
    // userdata allocation for every transform.position access.
    sol::table fieldCache;
};

inline void* ResolveLuaComponent(const LuaComponentHandle& handle)
{
    if (!handle.registry || !handle.componentInfo ||
        handle.entity == entt::null || !handle.registry->valid(handle.entity) ||
        !handle.componentInfo->find)
        return nullptr;

    // Resolve on every access instead of retaining an EnTT storage pointer.
    // The explicit valid() check rejects destroyed entities and recycled IDs;
    // the component lookup also rejects a removed component.
    return handle.componentInfo->find(*handle.registry, handle.entity);
}

inline Value ReadLuaComponentCompositeField(
    const LuaComponentHandle& handle,
    std::size_t fieldIndex)
{
    void* component = ResolveLuaComponent(handle);
    if (!component)
        throw std::runtime_error("Component no longer exists");
    if (fieldIndex >= handle.componentInfo->fields.size())
        throw std::runtime_error("Reflected component field no longer exists");

    const FieldInfo& field = handle.componentInfo->fields[fieldIndex];
    if (field.addr)
    {
        void* address = field.addr(component);
        switch (field.type)
        {
        case FieldType::Vec2:  return *static_cast<Vector2*>(address);
        case FieldType::Vec3:  return *static_cast<Vector3*>(address);
        case FieldType::Vec4:  return *static_cast<Vector4*>(address);
        case FieldType::Color: return *static_cast<Color*>(address);
        default: break;
        }
    }
    return field.get(component);
}

inline void WriteLuaComponentCompositeField(
    const LuaComponentHandle& handle,
    std::size_t fieldIndex,
    Value value)
{
    void* component = ResolveLuaComponent(handle);
    if (!component)
        throw std::runtime_error("Component no longer exists");
    if (fieldIndex >= handle.componentInfo->fields.size())
        throw std::runtime_error("Reflected component field no longer exists");

    const FieldInfo& field = handle.componentInfo->fields[fieldIndex];
    if (field.addr)
    {
        void* address = field.addr(component);
        switch (field.type)
        {
        case FieldType::Vec2:
            if (const auto* typed = std::get_if<Vector2>(&value))
            { *static_cast<Vector2*>(address) = *typed; return; }
            break;
        case FieldType::Vec3:
            if (const auto* typed = std::get_if<Vector3>(&value))
            { *static_cast<Vector3*>(address) = *typed; return; }
            break;
        case FieldType::Vec4:
            if (const auto* typed = std::get_if<Vector4>(&value))
            { *static_cast<Vector4*>(address) = *typed; return; }
            break;
        case FieldType::Color:
            if (const auto* typed = std::get_if<Color>(&value))
            { *static_cast<Color*>(address) = *typed; return; }
            break;
        default: break;
        }
        throw std::runtime_error("Live component field type changed: " + field.name);
    }

    const Value current = field.get(component);
    if (current.index() != value.index())
        throw std::runtime_error("Live component field type changed: " + field.name);
    field.set(component, std::move(value));
}

inline LuaLiveValueHandle MakeLuaComponentFieldHandle(
    const LuaComponentHandle& componentHandle,
    std::size_t fieldIndex)
{
    // Do not capture the Lua-side cache table in the value callbacks: the
    // cache owns the userdata, so capturing it would form a Lua/C++ cycle.
    LuaComponentHandle handle = componentHandle;
    handle.fieldCache = sol::table{};

    auto get = [handle, fieldIndex]() -> Value
    {
        return ReadLuaComponentCompositeField(handle, fieldIndex);
    };

    auto set = [handle, fieldIndex](Value value)
    {
        WriteLuaComponentCompositeField(handle, fieldIndex, std::move(value));
    };

    return LuaLiveValueHandle{ std::move(get), std::move(set) };
}

inline sol::object LuaComponentIndex(
    sol::this_state state,
    LuaComponentHandle& handle,
    sol::object key)
{
    sol::state_view lua(state);
    if (!key.is<std::string>())
        return sol::make_object(lua, sol::lua_nil);

    void* component = ResolveLuaComponent(handle);
    if (!component)
        return sol::make_object(lua, sol::lua_nil);

    const std::string fieldName = key.as<std::string>();
    const int index = handle.componentInfo->FieldIndex(fieldName);
    if (index < 0)
        return sol::make_object(lua, sol::lua_nil);

    const std::size_t fieldIndex = static_cast<std::size_t>(index);
    const FieldInfo& field = handle.componentInfo->fields[fieldIndex];
    if (!field.scriptExposed)
        return sol::make_object(lua, sol::lua_nil);
    if (field.type == FieldType::Vec2 || field.type == FieldType::Vec3 ||
        field.type == FieldType::Vec4 || field.type == FieldType::Color)
    {
        const int cacheKey = index + 1;
        sol::object cached = handle.fieldCache.raw_get<sol::object>(cacheKey);
        if (cached.valid() && cached.get_type() != sol::type::lua_nil)
            return cached;

        sol::object liveValue = sol::make_object(
            lua, MakeLuaComponentFieldHandle(handle, fieldIndex));
        handle.fieldCache.raw_set(cacheKey, liveValue);
        return liveValue;
    }

    return ValueToLua(lua, field.get(component));
}

inline void LuaComponentNewIndex(
    const LuaComponentHandle& handle,
    sol::object key,
    sol::object value)
{
    if (!key.is<std::string>())
        throw sol::error("Component fields must be addressed by name");

    void* component = ResolveLuaComponent(handle);
    if (!component)
        throw std::runtime_error("Component no longer exists");

    const std::string fieldName = key.as<std::string>();
    const int index = handle.componentInfo->FieldIndex(fieldName);
    if (index < 0)
        throw std::runtime_error("Unknown component field: " + fieldName);

    const FieldInfo& field = handle.componentInfo->fields[static_cast<std::size_t>(index)];
    if (!field.scriptExposed)
        throw std::runtime_error("Component field is not exposed to Lua: " + fieldName);
    const Value current = field.get(component);
    field.set(component, LuaToValue(value, current));
}

inline std::string LuaComponentToString(const LuaComponentHandle& handle)
{
    if (!handle.componentInfo)
        return "ComponentHandle(invalid)";
    return std::string("ComponentHandle(") + handle.componentInfo->name + ")";
}

inline int BindLuaComponentHandle(sol::state& lua, LuaBindingBackend& api)
{
    ScriptApiType description;
    description.name = "_MirageComponentHandle";
    description.cppType = "LuaComponentHandle";
    description.description = "Internal userdata used to access reflected ECS component fields.";
    description.internal = true;
    api.BindUserType<LuaComponentHandle>(lua, std::move(description),
        [](sol::usertype<LuaComponentHandle>& type)
        {
            type[sol::meta_function::index] = &LuaComponentIndex;
            type[sol::meta_function::new_index] = &LuaComponentNewIndex;
            type[sol::meta_function::to_string] = &LuaComponentToString;
        });
    return 1;
}

inline int BindLuaNativeHandles(sol::state& lua, LuaBindingBackend& api)
{
    return BindLuaLiveValueHandle(lua, api) + BindLuaComponentHandle(lua, api);
}

inline sol::object MakeLuaComponentHandle(
    sol::state_view lua,
    entt::registry& registry,
    entt::entity entity,
    const ComponentInfo& info)
{
    LuaComponentHandle handle{ &registry, entity, &info, lua.create_table() };
    return sol::make_object(lua, std::move(handle));
}
