#include "mirage_core/runtime.h"
#include "mirage_core/components.h"
#include "mirage_core/entity_lua.h"
#include "mirage_core/script_system.h"
#include "mirage_core/lua_input.h"
#include "mirage_core/lua_functions.h"
MirageCoreRuntime::MirageCoreRuntime():
    entityBus(registry),
    scriptSystem(lua, reflection, registry, entityBus)
{
    lua.open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table
    );
    reflection.Component<TransformComponent>("Transform")
        .Field("position", &TransformComponent::position)
        .Field("scale", &TransformComponent::scale)
        .Field("rotation", &TransformComponent::rotation);

    reflection.Component<GameComponent>("Game")
        .Field("name", &GameComponent::name)
        .DefaultArgs(std::string("GameComponent"));
    reflection.Component<ScriptComponent>("Script");
    reflection.Component<SpriteComponent>("Sprite")
        .Field("path",    &SpriteComponent::path)
        .Field("tint",    &SpriteComponent::tint)
        .Field("pivot",   &SpriteComponent::pivot)
        .Field("source",  &SpriteComponent::source)
        .Field("flipX",   &SpriteComponent::flipX)
        .Field("flipY",   &SpriteComponent::flipY)
        .Field("visible", &SpriteComponent::visible)
        .Field("layer",   &SpriteComponent::layer);
    entityBus.SetNameProvider(
        [](const entt::registry& r, entt::entity e) -> std::string
        {
            if (const auto* g = r.try_get<GameComponent>(e))
                return g->name;
            return {};
        });

    // Registers EntityRef and the Entities table with Lua. Must run before scripts.
    BindEntityLua(lua, entityBus, reflection);
    BindLuaUserTypes(lua);      // Vector2/3/4, Color: the Value alternatives Lua has no type for
    BindLuaFunctions(lua);      // math.clamp / lerp / sign / round (after open_libraries)
    BindLuaPrint(lua, scriptLog);
    BindLuaInput(lua,scriptLog);
}
MirageCoreRuntime::~MirageCoreRuntime() { scriptSystem.UnloadAll(); }