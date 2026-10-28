#pragma once
#include "raylib.h"
#include "entt/entt.hpp"
#include "mirage_core/entity_id.h"
#include <functional>
#include <type_traits>
#include <map>
#include <string>
#include <variant>
#include <vector>
using Value=std::variant<bool,float,int,Vector2,Vector3,Vector4,Color,entt::entity,std::string,EntityRef>;
enum class FieldType { Bool, Int, Float, String, Vec2, Vec3, Vec4, Color, Entity, EntityRef };
template<class T> constexpr FieldType TypeOf();
template<> constexpr FieldType TypeOf<bool>()         { return FieldType::Bool;      }
template<> constexpr FieldType TypeOf<int>()          { return FieldType::Int;       }
template<> constexpr FieldType TypeOf<float>()        { return FieldType::Float;     }
template<> constexpr FieldType TypeOf<Vector2>()      { return FieldType::Vec2;      }
template<> constexpr FieldType TypeOf<Vector3>()      { return FieldType::Vec3;      }
template<> constexpr FieldType TypeOf<Vector4>()      { return FieldType::Vec4;      }
template<> constexpr FieldType TypeOf<std::string>()  { return FieldType::String;    }
template<> constexpr FieldType TypeOf<Color>()        { return FieldType::Color;     }
template<> constexpr FieldType TypeOf<entt::entity>() { return FieldType::Entity;    }
template<> constexpr FieldType TypeOf<EntityRef>()   { return FieldType::EntityRef; }
struct FieldInfo{
    std::string name;
    FieldType type;
    std::function<Value(const void*)>   get;
    std::function<void(void*, Value)>   set;
    float min = 0, max = 0;
    bool  hidden = false;
};
struct ComponentInfo{
    std::string name;
    std::vector<FieldInfo> fields;
    bool isTag = false;
    std::function<void(entt::registry&, entt::entity)>  add;
    std::function<void(entt::registry&, entt::entity)>  remove;
    std::function<void*(entt::registry&, entt::entity)> find;
};
class Reflection {
public:
    template<class C> struct Builder {
        ComponentInfo& info;

        template<class F>
        Builder& Field(std::string name, F C::* member) {
            info.fields.push_back({
                std::move(name), TypeOf<F>(),
                [member](const void* c) { return Value(static_cast<const C*>(c)->*member); },
                [member](void* c, Value v) { static_cast<C*>(c)->*member = std::get<F>(v); }
            });
            return *this;
        }

        // Replaces info.add so it constructs C with these args instead of C().
        template<class... Args>
        Builder& DefaultArgs(Args... args) {
            info.add = [=](entt::registry& r, entt::entity e) {
                if (!r.all_of<C>(e)) r.emplace<C>(e, args...);
            };
            return *this;
        }
    };

    template<class C> Builder<C> Component(std::string name) {
        ComponentInfo& info = components_[name];
        info.name = name;
        info.isTag  = std::is_empty_v<C>;
        if constexpr (std::is_default_constructible_v<C>)
            info.add = [](entt::registry& r, entt::entity e) { if(!r.all_of<C>(e)) r.emplace<C>(e); };
        info.remove = [](entt::registry& r, entt::entity e) { r.remove<C>(e); };
        info.find   = [](entt::registry& r, entt::entity e) -> void* {
            if constexpr(std::is_empty_v<C>) {
                static char present;                       // any non-null address
                return r.all_of<C>(e) ? &present : nullptr;
            } else {
                return r.try_get<C>(e);
            }
        };
        return {info};
    }

    template<class C> void Tag(std::string name) { static_assert(std::is_empty_v<C>,"Tag<C> needs an empty struct"); Component<C>(std::move(name)); }
    const ComponentInfo* Find(const std::string& name) const {
        auto it = components_.find(name);
        return it == components_.end() ? nullptr : &it->second;
    }
    const std::map<std::string, ComponentInfo>& All() const { return components_; }
private:
    std::map<std::string, ComponentInfo> components_;
};
static inline std::vector<const ComponentInfo*> ComponentsOf(const Reflection& refl,
                                               entt::registry& reg, entt::entity e) {
    std::vector<const ComponentInfo*> out;
    for (auto& [name, info] : refl.All())
        if (info.find(reg, e)) out.push_back(&info);
    return out;
}