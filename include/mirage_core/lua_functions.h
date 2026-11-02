#pragma once
// ---------------------------------------------------------------------------
// lua_functions.h - Lua bindings for the types and helpers behind `Value`.
//
// Two generic helpers. Both bind ONLY IF the name is not already defined in the
// Lua state, so calling them twice, or after a script/host defined the name
// itself, never overwrites anything. Each returns true if it bound something.
//
//   BindLuaUserType<T>(lua, "Name", ...)        a C++ type  -> Lua usertype
//   BindLuaFunction(lua, "math.clamp", fn)      a function  -> Lua global / table field
//
// Two binders built on them. These are the only two the runtime calls:
//
//   BindLuaUserTypes(lua)   Vector2, Vector3, Vector4, Color
//   BindLuaFunctions(lua)   math.clamp, math.lerp, math.sign, math.round
//
// BindLuaUserTypes walks the alternatives of `Value` (reflection.h) and binds
// every one that plain Lua has no type for. How each alternative is handled:
//
//   bool, int, float, std::string   native Lua values, nothing to bind
//   entt::entity                    travels as a Lua integer, nothing to bind
//   EntityRef                       bound by BindEntityLua
//   Vector2/3/4, Color              bound here
//
// A new alternative added to `Value` that is not listed above fails to compile
// (static_assert) until it is handled here, so the two cannot drift apart.
//
// Call both once, after lua.open_libraries(...) (BindLuaFunctions adds to the
// `math` table) and before any script runs.
//
// Lua side
// --------
//   Vector2(x, y)  Vector3(x, y, z)  Vector4(x, y, z, w)  Color(r, g, b [, a])
//   Vector2() / Vector3() / Vector4() are all zeros; Color() is opaque white.
//   `Name.new(...)` works the same as `Name(...)`.
//
//   v.x v.y v.z v.w          Color: c.r c.g c.b c.a (clamped to 0..255)
//   a + b   a - b   a * b   a / b      component-wise; either side may be a number
//   -a      a == b          tostring(a)
//   v:Length()  v:LengthSqr()  v:Normalize()  v:Dot(o)  v:Distance(o)
//   v:Lerp(o, t)  v:Copy()      Vector3 also has v:Cross(o)
//   c:Lerp(o, t)  c:WithAlpha(a)  c:Copy()
//
// Anywhere a vector is expected, a usertype value, a plain {x=,y=,z=} table, or
// a live component proxy (transform.position) is accepted, so
//   transform.position = transform.position + Vector3(0, 1, 0) * dt
// works. (proxy + proxy does not: Lua needs one side to carry the operator.)
//
// Value <-> Lua: ValueToLua returns these usertypes once they are bound (it
// falls back to the old plain tables for any type that is not), and LuaToValue
// / expose() accept them as well as tables.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <variant>

#include <sol/sol.hpp>
#include "raylib.h"
#include "entt/entt.hpp"
#include "mirage_core/entity_id.h"
#include "mirage_core/reflection.h"      // Value

namespace lua_functions_detail {

inline std::vector<std::string> SplitPath(const std::string& path)
{
    std::vector<std::string> parts;
    std::string current;

    for (char c : path)
    {
        if (c == '.') { parts.push_back(current); current.clear(); }
        else          { current += c; }
    }
    parts.push_back(current);
    return parts;
}

inline bool ValidPath(const std::vector<std::string>& parts)
{
    for (const std::string& p : parts)
        if (p.empty()) return false;
    return !parts.empty();
}

inline bool IsNil(const sol::object& o)
{
    return !o.valid() ||
           o.get_type() == sol::type::lua_nil ||
           o.get_type() == sol::type::none;
}

inline std::string TypeFlagKey(const std::string& name)
{
    return "mirage.usertype." + name;
}

}  // namespace lua_functions_detail

// ---------------------------------------------------------------------------
// Existence checks
// ---------------------------------------------------------------------------

// True if `path` ("Vector3", "math.clamp", "a.b.c") already names something
// (anything but nil) in the Lua state.
inline bool LuaPathExists(sol::state& lua, const std::string& path)
{
    using namespace lua_functions_detail;

    const std::vector<std::string> parts = SplitPath(path);
    if (!ValidPath(parts)) return false;

    sol::object current = lua.globals();

    for (const std::string& part : parts)
    {
        if (current.get_type() != sol::type::table) return false;

        sol::table scope = current.as<sol::table>();
        current = scope.get<sol::object>(part);

        if (IsNil(current)) return false;
    }
    return true;
}

// True if BindLuaUserType bound a usertype under this name (as opposed to the
// name having been taken by something else). ValueToLua uses it to decide
// between a usertype and the plain-table fallback.
inline bool LuaTypeBound(sol::state_view lua, const std::string& name)
{
    sol::object flag = lua.registry()[lua_functions_detail::TypeFlagKey(name)];
    return flag.valid() &&
           flag.get_type() == sol::type::boolean &&
           flag.as<bool>();
}

// ---------------------------------------------------------------------------
// 1. Bind a custom user type
//
// Two ways to describe the type:
//
//   // sol's own key/value form
//   BindLuaUserType<Foo>(lua, "Foo",
//       sol::call_constructor, sol::factories([] { return Foo{}; }),
//       "bar", &Foo::bar);
//
//   // or one callable that receives the usertype (lets you use if constexpr,
//   // loops, helper functions...)
//   BindLuaUserType<Foo>(lua, "Foo", [](sol::usertype<Foo>& t) {
//       t["bar"] = &Foo::bar;
//   });
//
// Returns false, and changes nothing, if `name` is already defined.
// ---------------------------------------------------------------------------
template <class T, class... Args>
bool BindLuaUserType(sol::state& lua, const std::string& name, Args&&... args)
{
    if (lua_functions_detail::SplitPath(name).size() != 1 || name.empty())
        return false;                                   // types are plain globals
    if (LuaPathExists(lua, name))
        return false;

    if constexpr (sizeof...(Args) == 1 &&
                  (std::is_invocable_v<Args, sol::usertype<T>&> && ...))
    {
        sol::usertype<T> type = lua.new_usertype<T>(name);
        (std::forward<Args>(args)(type), ...);
    }
    else
    {
        lua.new_usertype<T>(name, std::forward<Args>(args)...);
    }

    lua.registry()[lua_functions_detail::TypeFlagKey(name)] = true;
    return true;
}

// ---------------------------------------------------------------------------
// 2. Bind a custom function
//
//   BindLuaFunction(lua, "Spawn", [](const std::string& name) { ... });
//   BindLuaFunction(lua, "math.clamp", [](double x, double lo, double hi) {...});
//   BindLuaFunction(lua, "game.audio.play", &PlaySound);   // creates game, audio
//
// A dotted path puts the function inside tables, creating missing ones. Returns
// false, and changes nothing, if the final name is already defined or a part of
// the path is already taken by something that is not a table (e.g. "print.x").
// ---------------------------------------------------------------------------
template <class Fn>
bool BindLuaFunction(sol::state& lua, const std::string& path, Fn&& fn)
{
    using namespace lua_functions_detail;

    const std::vector<std::string> parts = SplitPath(path);
    if (!ValidPath(parts)) return false;
    if (LuaPathExists(lua, path)) return false;

    // Walk to the table that will hold the function, without creating anything
    // yet, so a refused bind leaves no stray empty tables behind.
    sol::object current = lua.globals();
    std::size_t firstMissing = parts.size() - 1;

    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
    {
        sol::table scope = current.as<sol::table>();
        sol::object next = scope.get<sol::object>(parts[i]);

        if (IsNil(next)) { firstMissing = i; break; }
        if (next.get_type() != sol::type::table) return false;   // name taken by a non-table
        current = next;
    }

    sol::table scope = current.as<sol::table>();
    for (std::size_t i = firstMissing; i + 1 < parts.size(); ++i)
    {
        sol::table created = lua.create_table();
        scope[parts[i]] = created;
        scope = created;
    }

    scope.set_function(parts.back(), std::forward<Fn>(fn));
    return true;
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

// A vector from the usertype, or from any table with exactly the right keys
// (this includes live component proxies, whose x/y/z come from __index).
// A Vector3 table is NOT accepted as a Vector2, so a component is never dropped
// silently.
template <class V>
bool ToVec(const sol::object& o, V& out)
{
    if (o.is<V>()) { out = o.as<V>(); return true; }
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
// Returns how many types were newly bound (a name that already existed is
// skipped, see BindLuaUserType).
// ---------------------------------------------------------------------------
namespace lua_functions_detail {

template <class T> inline constexpr bool kUnhandledValueType = false;

// How one alternative of Value reaches Lua. Returns 1 if it bound a usertype.
template <class T>
int BindValueAlternative(sol::state& lua)
{
    if constexpr (std::is_same_v<T, bool>        ||
                  std::is_same_v<T, int>         ||
                  std::is_same_v<T, float>       ||
                  std::is_same_v<T, std::string> ||
                  std::is_same_v<T, entt::entity>)
    {
        return 0;                               // native Lua value / integer
    }
    else if constexpr (std::is_same_v<T, EntityRef>)
    {
        return 0;                               // BindEntityLua owns it
    }
    else if constexpr (std::is_same_v<T, Vector2>)
    {
        return BindLuaUserType<Vector2>(lua, "Vector2", &ConfigureVector<Vector2>);
    }
    else if constexpr (std::is_same_v<T, Vector3>)
    {
        return BindLuaUserType<Vector3>(lua, "Vector3", &ConfigureVector<Vector3>);
    }
    else if constexpr (std::is_same_v<T, Vector4>)
    {
        // Vector4 is also raylib's Quaternion (same type), so
        // transform.rotation is a Vector4 too.
        return BindLuaUserType<Vector4>(lua, "Vector4", &ConfigureVector<Vector4>);
    }
    else if constexpr (std::is_same_v<T, Color>)
    {
        return BindLuaUserType<Color>(lua, "Color", &ConfigureColor);
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
int BindValueAlternatives(sol::state& lua, const std::variant<Ts...>*)
{
    return (0 + ... + BindValueAlternative<Ts>(lua));
}

}  // namespace lua_functions_detail

inline int BindLuaUserTypes(sol::state& lua)
{
    return lua_functions_detail::BindValueAlternatives(lua, static_cast<const Value*>(nullptr));
}

// ---------------------------------------------------------------------------
// BindLuaFunctions - small numeric helpers Lua's math library lacks.
// Returns how many were newly bound.
// ---------------------------------------------------------------------------
inline int BindLuaFunctions(sol::state& lua)
{
    using namespace lua_functions_detail;
    int bound = 0;

    bound += BindLuaFunction(lua, "math.clamp", &MathClamp);
    bound += BindLuaFunction(lua, "math.lerp",  &MathLerp);
    bound += BindLuaFunction(lua, "math.sign",  &MathSign);
    bound += BindLuaFunction(lua, "math.round", &MathRound);

    return bound;
}