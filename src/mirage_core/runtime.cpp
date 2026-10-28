#include "mirage_core/runtime.h"
#include "mirage_core/components.h"
#include "mirage_core/entity_lua.h"
#include "mirage_core/script_system.h"
#include "mirage_core/lua_input.h"
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
        .Field("Position", &TransformComponent::position)
        .Field("Scale", &TransformComponent::scale)
        .Field("Rotation", &TransformComponent::rotation);

    reflection.Component<GameComponent>("Game")
        .Field("Name", &GameComponent::name)
        .DefaultArgs(std::string("GameComponent"));
    reflection.Component<ScriptComponent>("Script");
    reflection.Component<SpriteComponent>("Sprite")
        .Field("Path", &SpriteComponent::path)
        .Field("Tint", &SpriteComponent::tint)
        .Field("Hidden",&SpriteComponent::visible);
    ;
    entityBus.SetNameProvider(
        [](const entt::registry& r, entt::entity e) -> std::string
        {
            if (const auto* g = r.try_get<GameComponent>(e))
                return g->name;
            return {};
        });

    // Registers EntityRef and the Entities table with Lua. Must run before scripts.
    BindEntityLua(lua, entityBus, reflection);
    BindLuaPrint(lua, scriptLog);
    BindLuaInput(lua,scriptLog);
}
MirageCoreRuntime::~MirageCoreRuntime() { scriptSystem.UnloadAll(); }