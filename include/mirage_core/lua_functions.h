#pragma once
// ---------------------------------------------------------------------------
// lua_functions.h - Lua bindings for the types and helpers behind `Value`.
//
// All public registrations are routed through LuaBindingBackend. This file
// supplies the native value implementations and the value-type/math modules;
// it no longer exposes a parallel, registry-bypassing registration path.
//
// BindLuaUserTypes(api) registers Vector2/3/4 and Color. The API registry also
// records their Lua-visible constructors, properties, methods and operators
// for future API definition generation.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <variant>

#include <sol/sol.hpp>
#include "raylib.h"
#include "entt/entt.hpp"
#include "mirage_core/entity_id.h"
#include "mirage_core/reflection.h"      // Value
#include "mirage_core/lua_binding_backend.h"

// A native Lua userdata that refers to a composite Value stored elsewhere
// (a reflected component field or a script-exposed field). It deliberately
// stores accessors rather than a pointer into EnTT storage: every operation
// resolves the live value again, so entity destruction/component removal and
// storage relocation cannot leave a dangling pointer.
struct LuaLiveValueHandle
{
    std::function<Value()> get;
    std::function<void(Value)> set;

    Value Read() const
    {
        if (!get) throw std::runtime_error("Invalid live value handle");
        return get();
    }

    void Write(Value value) const
    {
        if (!set) throw std::runtime_error("Live value handle is read-only");
        set(std::move(value));
    }
};

inline bool TryReadLuaLiveMember(const Value& value, std::string_view key, double& out)
{
    if (key.size() != 1) return false;
    const char k = key[0];

    return std::visit([&](const auto& v) -> bool
    {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Vector2>)
        {
            if (k == 'x') { out = v.x; return true; }
            if (k == 'y') { out = v.y; return true; }
        }
        else if constexpr (std::is_same_v<T, Vector3>)
        {
            if (k == 'x') { out = v.x; return true; }
            if (k == 'y') { out = v.y; return true; }
            if (k == 'z') { out = v.z; return true; }
        }
        else if constexpr (std::is_same_v<T, Vector4>)
        {
            if (k == 'x') { out = v.x; return true; }
            if (k == 'y') { out = v.y; return true; }
            if (k == 'z') { out = v.z; return true; }
            if (k == 'w') { out = v.w; return true; }
        }
        else if constexpr (std::is_same_v<T, Color>)
        {
            if (k == 'r') { out = v.r; return true; }
            if (k == 'g') { out = v.g; return true; }
            if (k == 'b') { out = v.b; return true; }
            if (k == 'a') { out = v.a; return true; }
        }
        return false;
    }, value);
}

inline bool TryWriteLuaLiveMember(Value& value, std::string_view key, double number)
{
    if (key.size() != 1) return false;
    const char k = key[0];
    const float f = static_cast<float>(number);

    return std::visit([&](auto& v) -> bool
    {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Vector2>)
        {
            if (k == 'x') { v.x = f; return true; }
            if (k == 'y') { v.y = f; return true; }
        }
        else if constexpr (std::is_same_v<T, Vector3>)
        {
            if (k == 'x') { v.x = f; return true; }
            if (k == 'y') { v.y = f; return true; }
            if (k == 'z') { v.z = f; return true; }
        }
        else if constexpr (std::is_same_v<T, Vector4>)
        {
            if (k == 'x') { v.x = f; return true; }
            if (k == 'y') { v.y = f; return true; }
            if (k == 'z') { v.z = f; return true; }
            if (k == 'w') { v.w = f; return true; }
        }
        else if constexpr (std::is_same_v<T, Color>)
        {
            const auto channel = static_cast<unsigned char>(
                number < 0.0 ? 0.0 : number > 255.0 ? 255.0 : number);
            if (k == 'r') { v.r = channel; return true; }
            if (k == 'g') { v.g = channel; return true; }
            if (k == 'b') { v.b = channel; return true; }
            if (k == 'a') { v.a = channel; return true; }
        }
        return false;
    }, value);
}

namespace lua_functions_detail {

inline std::string TypeFlagKey(const std::string& name)
{
    return "mirage.usertype." + name;
}

}  // namespace lua_functions_detail

// True if LuaBindingBackend bound a usertype under this name (as opposed to the
// name having been taken by something else). ValueToLua uses it to decide
// between a usertype and the plain-table fallback.
inline bool LuaTypeBound(sol::state_view lua, const std::string& name)
{
    sol::object flag = lua.registry()[lua_functions_detail::TypeFlagKey(name)];
    return flag.valid() &&
           flag.get_type() == sol::type::boolean &&
           flag.as<bool>();
}

// Native metamethods for the internal live-value userdata. These remain plain
// function pointers, consistent with the other Sol2 registrations in this file.
inline sol::object LuaLiveValueIndex(
    sol::this_state state,
    const LuaLiveValueHandle& handle,
    sol::object key)
{
    sol::state_view view(state);
    if (!key.is<std::string>())
        return sol::make_object(view, sol::lua_nil);

    const Value current = handle.Read();
    double number = 0.0;
    if (TryReadLuaLiveMember(current, key.as<std::string>(), number))
        return sol::make_object(view, number);

    return sol::make_object(view, sol::lua_nil);
}

inline void LuaLiveValueNewIndex(
    const LuaLiveValueHandle& handle,
    sol::object key,
    sol::object value)
{
    if (!key.is<std::string>())
        throw sol::error("Live vector/color fields require a string key");
    if (value.get_type() != sol::type::number)
        throw sol::error("Vector/color members must be numbers");

    const std::string member = key.as<std::string>();
    Value current = handle.Read();
    if (!TryWriteLuaLiveMember(current, member, value.as<double>()))
        throw sol::error("Unknown vector/color member: " + member);
    handle.Write(std::move(current));
}

// Register the internal live-value wrapper as native userdata. Its metamethods
// expose vector/color channels while keeping the underlying value live.
inline int BindLuaLiveValueHandle(sol::state& lua, LuaBindingBackend& api)
{
    ScriptApiType description;
    description.name = "_MirageLiveValueHandle";
    description.cppType = "LuaLiveValueHandle";
    description.description = "Internal live view of a reflected vector or color field.";
    description.internal = true;
    api.BindUserType<LuaLiveValueHandle>(lua, std::move(description),
        [](sol::usertype<LuaLiveValueHandle>& type)
        {
            type[sol::meta_function::index] = &LuaLiveValueIndex;
            type[sol::meta_function::new_index] = &LuaLiveValueNewIndex;
        });
    return 1;
}

// ---------------------------------------------------------------------------
// Implementation details for the Value types
// ---------------------------------------------------------------------------
namespace lua_functions_detail {

template <class V> inline constexpr bool kIsVec2 = std::is_same_v<V, Vector2>;
template <class V> inline constexpr bool kIsVec3 = std::is_same_v<V, Vector3>;
template <class V> inline constexpr bool kIsVec4 = std::is_same_v<V, Vector4>;

inline std::string Num(float v)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", v);
    return buffer;
}

template <class V>
V Splat(float f)
{
    V r{};
    r.x = f;
    r.y = f;
    if constexpr (!kIsVec2<V>) r.z = f;
    if constexpr (kIsVec4<V>)  r.w = f;
    return r;
}

template <class V>
bool Equal(const V& a, const V& b)
{
    bool same = a.x == b.x && a.y == b.y;
    if constexpr (!kIsVec2<V>) same = same && a.z == b.z;
    if constexpr (kIsVec4<V>)  same = same && a.w == b.w;
    return same;
}

template <class V, class Op>
V Combine(const V& a, const V& b, Op op)
{
    V r{};
    r.x = op(a.x, b.x);
    r.y = op(a.y, b.y);
    if constexpr (!kIsVec2<V>) r.z = op(a.z, b.z);
    if constexpr (kIsVec4<V>)  r.w = op(a.w, b.w);
    return r;
}

template <class V, class Op>
V Map(const V& a, Op op)
{
    V r{};
    r.x = op(a.x);
    r.y = op(a.y);
    if constexpr (!kIsVec2<V>) r.z = op(a.z);
    if constexpr (kIsVec4<V>)  r.w = op(a.w);
    return r;
}

template <class V>
float Dot(const V& a, const V& b)
{
    float sum = a.x * b.x + a.y * b.y;
    if constexpr (!kIsVec2<V>) sum += a.z * b.z;
    if constexpr (kIsVec4<V>)  sum += a.w * b.w;
    return sum;
}

inline bool ToNumber(const sol::object& o, float& out)
{
    if (o.get_type() != sol::type::number) return false;
    out = o.as<float>();
    return true;
}

// A vector from the usertype, live-value userdata, or a table with exactly
// the right keys. Live-value userdata resolves x/y/z through __index.
// A Vector3 table is NOT accepted as a Vector2, so a component is never dropped
// silently.
template <class V>
bool ToVec(const sol::object& o, V& out)
{
    if (o.is<V>()) { out = o.as<V>(); return true; }
    if (o.is<LuaLiveValueHandle>())
    {
        const Value value = o.as<LuaLiveValueHandle>().Read();
        if (const auto* typed = std::get_if<V>(&value))
        {
            out = *typed;
            return true;
        }
        return false;
    }
    if (o.get_type() != sol::type::table) return false;

    sol::table t = o.as<sol::table>();
    const auto read = [&](const char* key, float& dst)
    {
        sol::optional<float> v = t.get<sol::optional<float>>(key);
        if (!v) return false;
        dst = *v;
        return true;
    };

    V r{};
    float extra = 0.0f;

    if (!read("x", r.x) || !read("y", r.y)) return false;

    if constexpr (kIsVec2<V>) { if (read("z", extra)) return false; }
    else                      { if (!read("z", r.z)) return false; }

    if constexpr (kIsVec4<V>)       { if (!read("w", r.w)) return false; }
    else if constexpr (kIsVec3<V>)  { if (read("w", extra)) return false; }

    out = r;
    return true;
}

inline unsigned char Channel(double v)
{
    return static_cast<unsigned char>(std::clamp(std::lround(v), 0L, 255L));
}

inline bool ToColor(const sol::object& o, Color& out)
{
    if (o.is<Color>()) { out = o.as<Color>(); return true; }
    if (o.is<LuaLiveValueHandle>())
    {
        const Value value = o.as<LuaLiveValueHandle>().Read();
        if (const auto* typed = std::get_if<Color>(&value))
        {
            out = *typed;
            return true;
        }
        return false;
    }
    if (o.get_type() != sol::type::table) return false;

    sol::table t = o.as<sol::table>();
    sol::optional<double> r = t.get<sol::optional<double>>("r");
    sol::optional<double> g = t.get<sol::optional<double>>("g");
    sol::optional<double> b = t.get<sol::optional<double>>("b");
    sol::optional<double> a = t.get<sol::optional<double>>("a");
    if (!r || !g || !b) return false;

    out = Color{ Channel(*r), Channel(*g), Channel(*b), Channel(a ? *a : 255.0) };
    return true;
}

inline std::string Describe(const sol::object& o)
{
    return sol::type_name(o.lua_state(), o.get_type());
}

// a (op) b for vector/vector, vector/number and number/vector.
template <class V, class Op>
V BinaryOp(const char* typeName, const char* verb,
           const sol::object& lhs, const sol::object& rhs, Op op)
{
    V a{}, b{};
    float s = 0.0f;

    const bool lv = ToVec<V>(lhs, a);
    const bool rv = ToVec<V>(rhs, b);

    if (lv && rv)                    return Combine(a, b, op);
    if (lv && ToNumber(rhs, s))      return Combine(a, Splat<V>(s), op);
    if (rv && ToNumber(lhs, s))      return Combine(Splat<V>(s), b, op);

    throw sol::error(std::string("attempt to ") + verb + " " + Describe(lhs) + " and " +
                     Describe(rhs) + " (expected " + typeName + " or number)");
}

template <class V>
V Need(const char* typeName, const char* method, const sol::object& o)
{
    V v{};
    if (!ToVec<V>(o, v))
        throw sol::error(std::string(typeName) + ":" + method + " expects a " + typeName +
                         ", got " + Describe(o));
    return v;
}

template <class V>
std::string ToText(const char* typeName, const V& v)
{
    std::string s = std::string(typeName) + "(" + Num(v.x) + ", " + Num(v.y);
    if constexpr (!kIsVec2<V>) s += ", " + Num(v.z);
    if constexpr (kIsVec4<V>)  s += ", " + Num(v.w);
    return s + ")";
}

// Field accessors for sol::property (see ConfigureVector).
template <class V, float V::* Member>
float GetField(const V& v) { return v.*Member; }

template <class V, float V::* Member>
void SetField(V& v, float f) { v.*Member = f; }

template <unsigned char Color::* Member>
int GetChannel(const Color& c) { return static_cast<int>(c.*Member); }

template <unsigned char Color::* Member>
void SetChannel(Color& c, double v) { c.*Member = Channel(v); }

// ---------------------------------------------------------------------------
// Everything bound to Lua below is a plain function (or function template)
// bound by pointer, never a lambda.
//
// Why: sol keeps per-callable bookkeeping in function-local statics
// (usertype_traits<F>::metatable, demangle<F>), keyed by the callable's type.
// For a lambda defined inside a function template, MinGW GCC 15 emits code that
// references those statics but not the statics themselves, so linking fails
// with "undefined reference to guard variable for sol::usertype_traits<...>".
// Function pointers have no lambda types in their names, so the problem cannot
// occur. (It is also why the binders below take no name argument: TypeName<V>()
// supplies it.)
// ---------------------------------------------------------------------------

template <class V> const char* TypeName();
template <> inline const char* TypeName<Vector2>() { return "Vector2"; }
template <> inline const char* TypeName<Vector3>() { return "Vector3"; }
template <> inline const char* TypeName<Vector4>() { return "Vector4"; }

// ----- constructors -----
template <class V> V MakeZero() { return V{}; }
inline Vector2 MakeVector2(float x, float y)                   { return Vector2{ x, y }; }
inline Vector3 MakeVector3(float x, float y, float z)          { return Vector3{ x, y, z }; }
inline Vector4 MakeVector4(float x, float y, float z, float w) { return Vector4{ x, y, z, w }; }

// ----- operators -----
template <class V> V OpAdd(const sol::object& a, const sol::object& b)
{ return BinaryOp<V>(TypeName<V>(), "add", a, b, std::plus<float>()); }

template <class V> V OpSub(const sol::object& a, const sol::object& b)
{ return BinaryOp<V>(TypeName<V>(), "subtract", a, b, std::minus<float>()); }

template <class V> V OpMul(const sol::object& a, const sol::object& b)
{ return BinaryOp<V>(TypeName<V>(), "multiply", a, b, std::multiplies<float>()); }

template <class V> V OpDiv(const sol::object& a, const sol::object& b)
{ return BinaryOp<V>(TypeName<V>(), "divide", a, b, std::divides<float>()); }

template <class V> V OpNeg(const V& v) { return Map(v, std::negate<float>()); }

// Lua only calls __eq when both sides are userdata, but they need not both be
// this type (Vector3(1,2,3) == Color(1,2,3)), so compare by object.
template <class V> bool OpEq(const sol::object& a, const sol::object& b)
{ return a.is<V>() && b.is<V>() && Equal(a.as<V>(), b.as<V>()); }

template <class V> std::string OpToString(const V& v) { return ToText<V>(TypeName<V>(), v); }

// ----- methods -----
template <class V> float VecLength(const V& v)    { return std::sqrt(Dot(v, v)); }
template <class V> float VecLengthSqr(const V& v) { return Dot(v, v); }
template <class V> V     VecCopy(const V& v)      { return v; }

template <class V> V VecNormalize(const V& v)
{
    const float length = std::sqrt(Dot(v, v));
    if (length > 0.0f)
        return Map(v, [length](float x) { return x / length; });
    return V{};
}

template <class V> float VecDot(const V& a, const sol::object& o)
{ return Dot(a, Need<V>(TypeName<V>(), "Dot", o)); }

template <class V> float VecDistance(const V& a, const sol::object& o)
{
    const V d = Combine(a, Need<V>(TypeName<V>(), "Distance", o), std::minus<float>());
    return std::sqrt(Dot(d, d));
}

template <class V> V VecLerp(const V& a, const sol::object& o, float amount)
{
    const V b = Need<V>(TypeName<V>(), "Lerp", o);
    return Combine(a, b, [amount](float x, float y) { return x + (y - x) * amount; });
}

inline Vector3 VecCross(const Vector3& a, const sol::object& o)
{
    const Vector3 b = Need<Vector3>("Vector3", "Cross", o);
    return Vector3{ a.y * b.z - a.z * b.y,
                    a.z * b.x - a.x * b.z,
                    a.x * b.y - a.y * b.x };
}

template <class V>
void ConfigureVector(sol::usertype<V>& t)
{
    t["x"] = sol::property(&GetField<V, &V::x>, &SetField<V, &V::x>);
    t["y"] = sol::property(&GetField<V, &V::y>, &SetField<V, &V::y>);
    if constexpr (!kIsVec2<V>) t["z"] = sol::property(&GetField<V, &V::z>, &SetField<V, &V::z>);
    if constexpr (kIsVec4<V>)  t["w"] = sol::property(&GetField<V, &V::w>, &SetField<V, &V::w>);

    // Vector2(), Vector2(x, y)  /  Vector3(), Vector3(x, y, z)  /  ...
    if constexpr (kIsVec2<V>)
    {
        auto make = sol::factories(&MakeZero<V>, &MakeVector2);
        t[sol::call_constructor] = make;
        t["new"] = make;
    }
    else if constexpr (kIsVec3<V>)
    {
        auto make = sol::factories(&MakeZero<V>, &MakeVector3);
        t[sol::call_constructor] = make;
        t["new"] = make;
    }
    else
    {
        auto make = sol::factories(&MakeZero<V>, &MakeVector4);
        t[sol::call_constructor] = make;
        t["new"] = make;
    }

    t[sol::meta_function::addition]       = &OpAdd<V>;
    t[sol::meta_function::subtraction]    = &OpSub<V>;
    t[sol::meta_function::multiplication] = &OpMul<V>;
    t[sol::meta_function::division]       = &OpDiv<V>;
    t[sol::meta_function::unary_minus]    = &OpNeg<V>;
    t[sol::meta_function::equal_to]       = &OpEq<V>;
    t[sol::meta_function::to_string]      = &OpToString<V>;

    t["Length"]    = &VecLength<V>;
    t["LengthSqr"] = &VecLengthSqr<V>;
    t["Copy"]      = &VecCopy<V>;
    t["Normalize"] = &VecNormalize<V>;
    t["Dot"]       = &VecDot<V>;
    t["Distance"]  = &VecDistance<V>;
    t["Lerp"]      = &VecLerp<V>;

    if constexpr (kIsVec3<V>)
        t["Cross"] = &VecCross;
}

// ----- Color -----
inline Color ColorNew0() { return Color{ 255, 255, 255, 255 }; }
inline Color ColorNew3(double r, double g, double b)
{ return Color{ Channel(r), Channel(g), Channel(b), 255 }; }
inline Color ColorNew4(double r, double g, double b, double a)
{ return Color{ Channel(r), Channel(g), Channel(b), Channel(a) }; }

inline bool ColorEq(const sol::object& x, const sol::object& y)
{
    if (!x.is<Color>() || !y.is<Color>()) return false;
    const Color a = x.as<Color>();
    const Color b = y.as<Color>();
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

inline std::string ColorToString(const Color& c)
{
    return "Color(" + std::to_string(c.r) + ", " + std::to_string(c.g) + ", " +
           std::to_string(c.b) + ", " + std::to_string(c.a) + ")";
}

inline Color ColorCopy(const Color& c) { return c; }

inline Color ColorWithAlpha(const Color& c, double a) { return Color{ c.r, c.g, c.b, Channel(a) }; }

inline Color ColorLerp(const Color& a, const sol::object& o, double amount)
{
    Color b{};
    if (!ToColor(o, b))
        throw sol::error("Color:Lerp expects a Color, got " + Describe(o));

    const auto mix = [amount](unsigned char x, unsigned char y) { return Channel(x + (y - x) * amount); };
    return Color{ mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a) };
}

inline void ConfigureColor(sol::usertype<Color>& t)
{
    t["r"] = sol::property(&GetChannel<&Color::r>, &SetChannel<&Color::r>);
    t["g"] = sol::property(&GetChannel<&Color::g>, &SetChannel<&Color::g>);
    t["b"] = sol::property(&GetChannel<&Color::b>, &SetChannel<&Color::b>);
    t["a"] = sol::property(&GetChannel<&Color::a>, &SetChannel<&Color::a>);

    auto make = sol::factories(&ColorNew0, &ColorNew3, &ColorNew4);
    t[sol::call_constructor] = make;
    t["new"] = make;

    t[sol::meta_function::equal_to]  = &ColorEq;
    t[sol::meta_function::to_string] = &ColorToString;

    t["Copy"]      = &ColorCopy;
    t["WithAlpha"] = &ColorWithAlpha;
    t["Lerp"]      = &ColorLerp;
}

// ----- math helpers (bound by BindLuaFunctions) -----
inline double MathClamp(double x, double lo, double hi)
{
    if (lo > hi) std::swap(lo, hi);
    return std::min(std::max(x, lo), hi);
}
inline double MathLerp(double a, double b, double t) { return a + (b - a) * t; }
inline double MathSign(double x) { return x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0); }
inline double MathRound(double x) { return std::round(x); }      // halves round away from zero

}  // namespace lua_functions_detail

// ---------------------------------------------------------------------------
// BindLuaUserTypes - one step per alternative of `Value`.
// Every registration goes through LuaBindingBackend so runtime bindings and
// the metadata consumed by future tooling cannot drift apart.
// ---------------------------------------------------------------------------
namespace lua_functions_detail {

template <class T> inline constexpr bool kUnhandledValueType = false;

inline ScriptApiParameter ApiParam(std::string name, std::string type,
                                   bool optional = false, bool variadic = false,
                                   std::string defaultValue = {})
{
    return { std::move(name), std::move(type), optional, variadic, std::move(defaultValue) };
}

inline ScriptApiCallable ApiCall(std::string name, std::string description,
                                 std::vector<ScriptApiParameter> parameters,
                                 std::vector<std::string> returns)
{
    return { std::move(name), std::move(description), std::move(parameters), std::move(returns), false };
}

template <class V>
ScriptApiType VectorApiType(const char* name)
{
    ScriptApiType type;
    type.name = name;
    type.cppType = name;
    type.description = std::string(name) + " value type. Supports component-wise arithmetic with other vectors or numbers.";
    type.ownership = "value";
    type.operators = { "+", "-", "*", "/", "unary -", "==", "tostring" };
    type.properties.push_back({ "x", "number", "X component", false });
    type.properties.push_back({ "y", "number", "Y component", false });
    if constexpr (!kIsVec2<V>) type.properties.push_back({ "z", "number", "Z component", false });
    if constexpr (kIsVec4<V>) type.properties.push_back({ "w", "number", "W component", false });

    type.constructors.push_back(ApiCall("new", "Create a zero-initialized value", {}, { name }));
    if constexpr (kIsVec2<V>)
        type.constructors.push_back(ApiCall("new", "Create a vector from its components",
            { ApiParam("x", "number"), ApiParam("y", "number") }, { name }));
    else if constexpr (kIsVec3<V>)
        type.constructors.push_back(ApiCall("new", "Create a vector from its components",
            { ApiParam("x", "number"), ApiParam("y", "number"), ApiParam("z", "number") }, { name }));
    else
        type.constructors.push_back(ApiCall("new", "Create a vector from its components",
            { ApiParam("x", "number"), ApiParam("y", "number"), ApiParam("z", "number"), ApiParam("w", "number") }, { name }));

    type.methods.push_back(ApiCall("Length", "Return the vector magnitude", {}, { "number" }));
    type.methods.push_back(ApiCall("LengthSqr", "Return the squared vector magnitude", {}, { "number" }));
    type.methods.push_back(ApiCall("Copy", "Return a copy of this vector", {}, { name }));
    type.methods.push_back(ApiCall("Normalize", "Return a normalized copy; zero remains zero", {}, { name }));
    type.methods.push_back(ApiCall("Dot", "Return the dot product with another vector",
        { ApiParam("other", name) }, { "number" }));
    type.methods.push_back(ApiCall("Distance", "Return the distance to another vector",
        { ApiParam("other", name) }, { "number" }));
    type.methods.push_back(ApiCall("Lerp", "Linearly interpolate to another vector",
        { ApiParam("other", name), ApiParam("amount", "number") }, { name }));
    if constexpr (kIsVec3<V>)
        type.methods.push_back(ApiCall("Cross", "Return the cross product with another Vector3",
            { ApiParam("other", "Vector3") }, { "Vector3" }));
    return type;
}

inline ScriptApiType ColorApiType()
{
    ScriptApiType type;
    type.name = "Color";
    type.cppType = "Color";
    type.description = "RGBA color. Channels are integers clamped to the range 0..255.";
    type.ownership = "value";
    type.operators = { "==", "tostring" };
    type.properties = {
        { "r", "integer", "Red channel", false },
        { "g", "integer", "Green channel", false },
        { "b", "integer", "Blue channel", false },
        { "a", "integer", "Alpha channel", false }
    };
    type.constructors.push_back(ApiCall("new", "Create opaque white", {}, { "Color" }));
    type.constructors.push_back(ApiCall("new", "Create a color from RGB channels",
        { ApiParam("r", "number"), ApiParam("g", "number"), ApiParam("b", "number") }, { "Color" }));
    type.constructors.push_back(ApiCall("new", "Create a color from RGBA channels",
        { ApiParam("r", "number"), ApiParam("g", "number"), ApiParam("b", "number"), ApiParam("a", "number") }, { "Color" }));
    type.methods.push_back(ApiCall("Copy", "Return a copy of this color", {}, { "Color" }));
    type.methods.push_back(ApiCall("WithAlpha", "Return a copy with a different alpha channel",
        { ApiParam("alpha", "number") }, { "Color" }));
    type.methods.push_back(ApiCall("Lerp", "Interpolate to another color",
        { ApiParam("other", "Color"), ApiParam("amount", "number") }, { "Color" }));
    return type;
}

// How one alternative of Value reaches Lua. C++-native Lua primitives need no
// usertype; engine value types are bound and described through the registry.
template <class T>
int BindValueAlternative(sol::state& lua, LuaBindingBackend& api)
{
    if constexpr (std::is_same_v<T, bool>        ||
                  std::is_same_v<T, int>         ||
                  std::is_same_v<T, float>       ||
                  std::is_same_v<T, std::string> ||
                  std::is_same_v<T, entt::entity>)
    {
        return 0;
    }
    else if constexpr (std::is_same_v<T, EntityRef>)
    {
        return 0; // EntityRef is registered by the entity API module.
    }
    else if constexpr (std::is_same_v<T, Vector2>)
    {
        api.BindUserType<Vector2>(lua, VectorApiType<Vector2>("Vector2"), &ConfigureVector<Vector2>);
        return 1;
    }
    else if constexpr (std::is_same_v<T, Vector3>)
    {
        api.BindUserType<Vector3>(lua, VectorApiType<Vector3>("Vector3"), &ConfigureVector<Vector3>);
        return 1;
    }
    else if constexpr (std::is_same_v<T, Vector4>)
    {
        api.BindUserType<Vector4>(lua, VectorApiType<Vector4>("Vector4"), &ConfigureVector<Vector4>);
        return 1;
    }
    else if constexpr (std::is_same_v<T, Color>)
    {
        api.BindUserType<Color>(lua, ColorApiType(), &ConfigureColor);
        return 1;
    }
    else
    {
        static_assert(kUnhandledValueType<T>,
            "Value has an alternative that lua_functions.h does not handle: "
            "add it to BindValueAlternative (bind a usertype, or mark it native)");
        return 0;
    }
}

template <class... Ts>
int BindValueAlternatives(sol::state& lua, LuaBindingBackend& api,
                          const std::variant<Ts...>*)
{
    return (0 + ... + BindValueAlternative<Ts>(lua, api));
}

}  // namespace lua_functions_detail

inline int BindLuaUserTypes(sol::state& lua, LuaBindingBackend& api)
{
    return lua_functions_detail::BindValueAlternatives(
        lua, api, static_cast<const Value*>(nullptr));
}

// ---------------------------------------------------------------------------
// BindLuaFunctions - numeric helpers added to Lua's math library.
// ---------------------------------------------------------------------------
inline int BindLuaFunctions(sol::state& lua, LuaBindingBackend& api)
{
    using namespace lua_functions_detail;
    int bound = 0;

    api.BindGlobalFunction(lua, "math.clamp",
        ApiCall("clamp", "Clamp a number between a minimum and maximum",
            { ApiParam("value", "number"), ApiParam("minimum", "number"), ApiParam("maximum", "number") }, { "number" }),
        &MathClamp);
    ++bound;
    api.BindGlobalFunction(lua, "math.lerp",
        ApiCall("lerp", "Linearly interpolate between two numbers",
            { ApiParam("a", "number"), ApiParam("b", "number"), ApiParam("amount", "number") }, { "number" }),
        &MathLerp);
    ++bound;
    api.BindGlobalFunction(lua, "math.sign",
        ApiCall("sign", "Return -1, 0, or 1 based on the sign of a number",
            { ApiParam("value", "number") }, { "number" }),
        &MathSign);
    ++bound;
    api.BindGlobalFunction(lua, "math.round",
        ApiCall("round", "Round to the nearest integer; halves round away from zero",
            { ApiParam("value", "number") }, { "number" }),
        &MathRound);
    ++bound;

    return bound;
}
