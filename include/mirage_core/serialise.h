// mirage_core/serialise.h
//
// Scene save / load for Mirage. Header-only.
//
//   json j = SaveScene(registry, bus, reflection);
//   LoadResult r = LoadScene(registry, bus, reflection, j);
//
//   SaveSceneFile(core, "scenes/test.json");
//   LoadSceneFile(core, "scenes/test.json");     // also unloads/reloads scripts
//
// What is saved: every entity with an IdComponent (by Uuid), every reflected
// component field, and the ScriptComponent (path + exposed fields, typed).
// What is not: entt handles, Lua runtime state, editor state.
//
// Include this from .cpp files only: nlohmann/json.hpp is a big header.
#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "mirage_core/entity_bus.h"
#include "mirage_core/reflection.h"
#include "mirage_core/script_system.h"   // ScriptComponent

namespace Mirage {

using json = nlohmann::json;

inline constexpr int         kSceneVersion = 1;
inline constexpr const char* kScriptComponentName = "Script";  // handled separately

// ---------------------------------------------------------------------------
// FieldType <-> name
// ---------------------------------------------------------------------------
inline const char* FieldTypeName(FieldType t) {
    switch (t) {
        case FieldType::Bool:      return "Bool";
        case FieldType::Int:       return "Int";
        case FieldType::Float:     return "Float";
        case FieldType::String:    return "String";
        case FieldType::Vec2:      return "Vec2";
        case FieldType::Vec3:      return "Vec3";
        case FieldType::Vec4:      return "Vec4";
        case FieldType::Color:     return "Color";
        case FieldType::Entity:    return "Entity";
        case FieldType::EntityRef: return "EntityRef";
    }
    return "?";
}

inline bool FieldTypeFromName(const std::string& n, FieldType& out) {
    static const FieldType all[] = {
        FieldType::Bool, FieldType::Int, FieldType::Float, FieldType::String,
        FieldType::Vec2, FieldType::Vec3, FieldType::Vec4, FieldType::Color,
        FieldType::Entity, FieldType::EntityRef };
    for (FieldType t : all)
        if (n == FieldTypeName(t)) { out = t; return true; }
    return false;
}

// Which FieldType a Value currently holds.
inline FieldType TypeOfValue(const Value& v) {
    if (std::holds_alternative<bool>(v))         return FieldType::Bool;
    if (std::holds_alternative<int>(v))          return FieldType::Int;
    if (std::holds_alternative<float>(v))        return FieldType::Float;
    if (std::holds_alternative<std::string>(v))  return FieldType::String;
    if (std::holds_alternative<Vector2>(v))      return FieldType::Vec2;
    if (std::holds_alternative<Vector3>(v))      return FieldType::Vec3;
    if (std::holds_alternative<Vector4>(v))      return FieldType::Vec4;
    if (std::holds_alternative<Color>(v))        return FieldType::Color;
    if (std::holds_alternative<entt::entity>(v)) return FieldType::Entity;
    return FieldType::EntityRef;
}

// ---------------------------------------------------------------------------
// Value -> JSON   (plain value, no type tag)
//   bool/int/float/string -> JSON scalar
//   Vec2/3/4              -> [x,y(,z(,w))]
//   Color                 -> [r,g,b,a] as ints
//   EntityRef / entity    -> uuid string ("" for null / missing)
// ---------------------------------------------------------------------------
inline json ValueToJson(const Value& v, const EntityBus& bus) {
    switch (TypeOfValue(v)) {
        case FieldType::Bool:   return std::get<bool>(v);
        case FieldType::Int:    return std::get<int>(v);
        case FieldType::Float:  return std::get<float>(v);
        case FieldType::String: return std::get<std::string>(v);
        case FieldType::Vec2: { auto& p = std::get<Vector2>(v); return json::array({p.x, p.y}); }
        case FieldType::Vec3: { auto& p = std::get<Vector3>(v); return json::array({p.x, p.y, p.z}); }
        case FieldType::Vec4: { auto& p = std::get<Vector4>(v); return json::array({p.x, p.y, p.z, p.w}); }
        case FieldType::Color: {
            auto& c = std::get<Color>(v);
            return json::array({(int)c.r, (int)c.g, (int)c.b, (int)c.a});
        }
        case FieldType::Entity:
            return bus.IdOf(std::get<entt::entity>(v)).ToString();
        case FieldType::EntityRef:
            return std::get<EntityRef>(v).ToString();
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// JSON -> Value   (the FieldType says what to expect)
// Returns false, leaving `out` untouched, if the JSON has the wrong shape.
// Entity fields are resolved through the bus, so call this after every
// entity of the scene exists (LoadScene does).
// ---------------------------------------------------------------------------
namespace detail {
inline bool Floats(const json& j, std::size_t n, float* dst) {
    if (!j.is_array() || j.size() != n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        if (!j[i].is_number()) return false;
        dst[i] = j[i].get<float>();
    }
    return true;
}
}  // namespace detail

inline bool JsonToValue(const json& j, FieldType type, const EntityBus& bus, Value& out) {
    switch (type) {
        case FieldType::Bool:
            if (!j.is_boolean()) return false;
            out = j.get<bool>(); return true;
        case FieldType::Int:
            if (!j.is_number()) return false;
            out = j.get<int>(); return true;
        case FieldType::Float:
            if (!j.is_number()) return false;
            out = j.get<float>(); return true;
        case FieldType::String:
            if (!j.is_string()) return false;
            out = j.get<std::string>(); return true;
        case FieldType::Vec2: {
            float f[2]; if (!detail::Floats(j, 2, f)) return false;
            out = Vector2{f[0], f[1]}; return true;
        }
        case FieldType::Vec3: {
            float f[3]; if (!detail::Floats(j, 3, f)) return false;
            out = Vector3{f[0], f[1], f[2]}; return true;
        }
        case FieldType::Vec4: {
            float f[4]; if (!detail::Floats(j, 4, f)) return false;
            out = Vector4{f[0], f[1], f[2], f[3]}; return true;
        }
        case FieldType::Color: {
            if (!j.is_array() || j.size() != 4) return false;
            int c[4];
            for (int i = 0; i < 4; ++i) {
                if (!j[i].is_number()) return false;
                c[i] = std::clamp(j[i].get<int>(), 0, 255);
            }
            out = Color{(unsigned char)c[0], (unsigned char)c[1],
                        (unsigned char)c[2], (unsigned char)c[3]};
            return true;
        }
        case FieldType::Entity: {
            if (!j.is_string()) return false;
            out = bus.Resolve(EntityRef::FromString(j.get<std::string>()));
            return true;
        }
        case FieldType::EntityRef: {
            if (!j.is_string()) return false;
            out = EntityRef::FromString(j.get<std::string>());
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Scene -> JSON
// Call it in Edit mode (scripts have run expose(), so their fields exist).
// Saving during Play stores the values as Play has changed them.
// ---------------------------------------------------------------------------
inline json SaveScene(entt::registry& reg, const EntityBus& bus, const Reflection& refl) {
    json root;
    root["version"]  = kSceneVersion;
    root["entities"] = json::array();

    // entt iterates newest-first; reverse so the file is in creation order.
    std::vector<entt::entity> order;
    for (entt::entity e : reg.view<IdComponent>()) order.push_back(e);
    std::reverse(order.begin(), order.end());

    for (entt::entity e : order) {
        json ent;
        ent["id"] = reg.get<IdComponent>(e).uuid.ToString();

        json comps = json::object();
        for (const auto& [name, info] : refl.All()) {
            if (name == kScriptComponentName) continue;
            void* c = info.find(reg, e);
            if (!c) continue;

            json fields = json::object();
            for (const FieldInfo& f : info.fields)
                fields[f.name] = ValueToJson(f.get(c), bus);
            comps[name] = std::move(fields);       // tags save as {}
        }
        ent["components"] = std::move(comps);

        if (const auto* sc = reg.try_get<ScriptComponent>(e)) {
            json fields = json::object();
            for (const auto& [name, value] : sc->fields)
                fields[name] = { {"type",  FieldTypeName(TypeOfValue(value))},
                                 {"value", ValueToJson(value, bus)} };
            ent["script"] = { {"path", sc->path}, {"fields", std::move(fields)} };
        }

        root["entities"].push_back(std::move(ent));
    }
    return root;
}

// ---------------------------------------------------------------------------
// JSON -> scene
// Replaces every entity that has an IdComponent. If you run scripts, call
// scriptSystem.UnloadAll() BEFORE and ReloadAll() AFTER (LoadSceneFile does).
// Problems with single fields/components are warnings; the rest still loads.
// ---------------------------------------------------------------------------
struct LoadResult {
    bool ok = false;
    int  entities = 0;
    std::string error;                  // set when ok == false
    std::vector<std::string> warnings;
};

inline LoadResult LoadScene(entt::registry& reg, EntityBus& bus,
                            const Reflection& refl, const json& root) {
    LoadResult res;
    try {
        if (!root.is_object() || !root.contains("entities") || !root["entities"].is_array()) {
            res.error = "not a scene file (no 'entities' array)";
            return res;
        }
        const int version = root.value("version", 0);
        if (version < 1 || version > kSceneVersion) {
            res.error = "unsupported scene version " + std::to_string(version);
            return res;
        }

        // Clear the old scene.
        std::vector<entt::entity> old;
        for (entt::entity e : reg.view<IdComponent>()) old.push_back(e);
        for (entt::entity e : old) reg.destroy(e);

        // Pass 1: create every entity with its saved Uuid, so references
        // between entities resolve no matter the order in the file.
        struct Pending { entt::entity e; const json* data; };
        std::vector<Pending> pending;

        for (const json& ent : root["entities"]) {
            if (!ent.is_object()) { res.warnings.push_back("skipped a non-object entity"); continue; }

            Uuid id = ent.contains("id") && ent["id"].is_string()
                          ? Uuid::Parse(ent["id"].get<std::string>()) : Uuid{};
            entt::entity e = bus.Create(id);
            if (!id.IsNull() && bus.IdOf(e) != id)
                res.warnings.push_back("duplicate entity id " + id.ToString() + ", gave it a new one");
            pending.push_back({e, &ent});
        }

        // Pass 2: components, fields, script.
        for (const Pending& p : pending) {
            const json& ent = *p.data;
            const std::string who = bus.IdOf(p.e).ToString();

            if (ent.contains("components") && ent["components"].is_object()) {
                for (const auto& [cname, cdata] : ent["components"].items()) {
                    const ComponentInfo* info = refl.Find(cname);
                    if (!info)       { res.warnings.push_back(who + ": unknown component '" + cname + "'"); continue; }
                    if (!info->add)  { res.warnings.push_back(who + ": cannot create '" + cname + "' (no default ctor / DefaultArgs)"); continue; }

                    info->add(reg, p.e);
                    void* c = info->find(reg, p.e);
                    if (!c || !cdata.is_object()) continue;

                    for (const auto& [fname, fval] : cdata.items()) {
                        const FieldInfo* field = nullptr;
                        for (const FieldInfo& f : info->fields)
                            if (f.name == fname) { field = &f; break; }
                        if (!field) { res.warnings.push_back(who + ": " + cname + " has no field '" + fname + "'"); continue; }

                        Value v;
                        if (!JsonToValue(fval, field->type, bus, v)) {
                            res.warnings.push_back(who + ": bad value for " + cname + "." + fname);
                            continue;
                        }
                        field->set(c, std::move(v));
                    }
                }
            }

            if (ent.contains("script") && ent["script"].is_object()) {
                const json& s = ent["script"];
                if (!s.contains("path") || !s["path"].is_string()) {
                    res.warnings.push_back(who + ": script without a path");
                } else {
                    auto& sc = reg.emplace_or_replace<ScriptComponent>(p.e, s["path"].get<std::string>());
                    if (s.contains("fields") && s["fields"].is_object()) {
                        for (const auto& [fname, fj] : s["fields"].items()) {
                            FieldType t;
                            Value v;
                            if (!fj.is_object() || !fj.contains("type") || !fj.contains("value") ||
                                !fj["type"].is_string() ||
                                !FieldTypeFromName(fj["type"].get<std::string>(), t) ||
                                !JsonToValue(fj["value"], t, bus, v)) {
                                res.warnings.push_back(who + ": bad script field '" + fname + "'");
                                continue;
                            }
                            sc.fields[fname] = std::move(v);
                        }
                    }
                }
            }
        }

        res.entities = (int)pending.size();
        res.ok = true;
    } catch (const std::exception& ex) {
        res.ok = false;
        res.error = ex.what();
    }
    return res;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------
inline bool SaveSceneFile(entt::registry& reg, const EntityBus& bus, const Reflection& refl,
                          const std::string& path, std::string* error = nullptr) {
    try {
        std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());

        std::ofstream out(p, std::ios::binary);
        if (!out) { if (error) *error = "cannot open '" + path + "' for writing"; return false; }
        out << SaveScene(reg, bus, refl).dump(2);
        out.flush();
        if (!out) { if (error) *error = "write failed: " + path; return false; }
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    }
}

inline bool ReadJsonFile(const std::string& path, json& out, std::string* error = nullptr) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { if (error) *error = "cannot open '" + path + "'"; return false; }
    out = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (out.is_discarded()) { if (error) *error = "'" + path + "' is not valid JSON"; return false; }
    return true;
}

inline LoadResult LoadSceneFile(entt::registry& reg, EntityBus& bus, const Reflection& refl,
                                const std::string& path) {
    LoadResult res;
    json j;
    if (!ReadJsonFile(path, j, &res.error)) return res;
    return LoadScene(reg, bus, refl, j);
}

// Convenience for MirageCoreRuntime (any type with registry, entityBus,
// reflection, scriptSystem). The file is read BEFORE anything is thrown away,
// so a bad file leaves the current scene untouched.
template <class Runtime>
bool SaveSceneFile(Runtime& rt, const std::string& path, std::string* error = nullptr) {
    return SaveSceneFile(rt.registry, rt.entityBus, rt.reflection, path, error);
}

template <class Runtime>
LoadResult LoadSceneFile(Runtime& rt, const std::string& path) {
    LoadResult res;
    json j;
    if (!ReadJsonFile(path, j, &res.error)) return res;

    rt.scriptSystem.UnloadAll();                       // drop Lua runtimes first
    res = LoadScene(rt.registry, rt.entityBus, rt.reflection, j);
    rt.scriptSystem.ReloadAll();                       // re-expose script fields
    return res;
}

}  // namespace Mirage