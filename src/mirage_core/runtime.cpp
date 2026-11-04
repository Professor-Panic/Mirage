#include "mirage_core/lua_native.h"  // Declares BindLuaNativeHandles.
#include "mirage_core/runtime.h"
#include "mirage_core/components.h"
#include "mirage_core/entity_lua.h"
#include "mirage_core/script_system.h"
#include "mirage_core/lua_input.h"
#include "mirage_core/lua_functions.h"
MirageCoreRuntime::MirageCoreRuntime():
    entityBus(registry),
    luaBindings(scriptApi),
    scriptSystem(lua, reflection, registry, entityBus, events, luaBindings)
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
        .ScriptDescription("World-space position of the entity.")
        .Field("scale", &TransformComponent::scale)
        .ScriptDescription("Local scale of the entity.")
        // The quaternion stays the stored (and saved) truth, but the inspector
        // shows Euler angles in degrees. `eulerAngles` is computed from it, so it
        // is never written to a scene file, and scripts can use either:
        //   transform.eulerAngles.z = 45     transform.rotation (quaternion, as before)
        .Field("rotation", &TransformComponent::rotation).Hidden()
        .ScriptDescription("Stored quaternion rotation.")
        .Property<Vector3>("eulerAngles",
            [](const TransformComponent& t) { return t.GetEulerAngles(); },
            [](TransformComponent& t, const Vector3& degrees) { t.SetEulerAngles(degrees); })
        .Label("rotation").Transient()
        .ScriptDescription("Rotation in Euler degrees.")
        .ScriptExposed("Position, scale and rotation data for an entity transform.");

    reflection.Component<GameComponent>("Game")
        .Field("name", &GameComponent::name)
        .ScriptDescription("Display name used to identify the entity in the game.")
        .DefaultArgs(std::string("GameComponent")).ScriptExposed("Entity display name and game-facing identity data.");
    reflection.Component<ScriptComponent>("Script")
        .ScriptExposed("Attached script paths and their persisted exposed fields.");
    reflection.Component<TagsComponent>("Tags")
        .Field("tags", &TagsComponent::tags)
        .ScriptDescription("Comma-separated list of tags assigned to the entity.")
        .ScriptExposed("Comma-separated entity tags, used by Entities.FindWithTag.");
    reflection.Component<SpriteComponent>("Sprite")
        .Field("path",    &SpriteComponent::path).ScriptDescription("Texture path relative to the project root.")
        .Field("tint",    &SpriteComponent::tint).ScriptDescription("Color tint multiplied with the sprite texture.")
        .Field("pivot",   &SpriteComponent::pivot).ScriptDescription("Normalized sprite origin, where 0.5, 0.5 is the center.")
        .Field("source",  &SpriteComponent::source).ScriptDescription("Texture source rectangle in pixels; zero dimensions use the full texture.")
        .Field("flipX",   &SpriteComponent::flipX).ScriptDescription("Whether the sprite is flipped horizontally.")
        .Field("flipY",   &SpriteComponent::flipY).ScriptDescription("Whether the sprite is flipped vertically.")
        .Field("visible", &SpriteComponent::visible).ScriptDescription("Whether the sprite is rendered.")
        .Field("layer",   &SpriteComponent::layer).ScriptDescription("Render layer; lower values are drawn behind higher values.")
        .ScriptExposed("Sprite texture, color, source rectangle and rendering settings.");
    entityBus.SetNameProvider(
        [](const entt::registry& r, entt::entity e) -> std::string
        {
            if (const auto* g = r.try_get<GameComponent>(e))
                return g->name;
            return {};
        });

    // Import explicitly exposed reflected components before binding. The same
    // registry will later drive API definition generation as well.
    scriptApi.ImportReflectedComponents(reflection);

    ScriptApiType componentHandleType;
    componentHandleType.name = "ComponentHandle";
    componentHandleType.cppType = "LuaComponentHandle";
    componentHandleType.description = "Opaque live handle to a script-exposed component. Prefer the concrete component type returned by GetComponent with a literal component name.";
    componentHandleType.ownership = "engine-owned";
    componentHandleType.lifetime = "The handle resolves its entity and component on each access. It becomes invalid when the entity or component is removed.";
    scriptApi.DescribeType(std::move(componentHandleType));

    scriptSystem.DescribeReflectedApi();

    // Registration modules are deliberately ordered: value types and internal
    // userdata must exist before APIs that return them are bound.
    luaBindings.AddModule("value-types", [](LuaBindingBackend& api, sol::state& state)
    {
        BindLuaUserTypes(state, api);
    });
    luaBindings.AddModule("native-handles", [](LuaBindingBackend& api, sol::state& state)
    {
        BindLuaNativeHandles(state, api);
    });
    luaBindings.AddModule("entity-api", [this](LuaBindingBackend& api, sol::state& state)
    {
        BindEntityLua(state, api, entityBus, reflection, scriptSystem);
    });
    luaBindings.AddModule("math-functions", [](LuaBindingBackend& api, sol::state& state)
    {
        BindLuaFunctions(state, api);
    });
    luaBindings.AddModule("events-api", [this](LuaBindingBackend& api, sol::state& state)
    {
        sol::table eventsTable = MakeEventsTable(state, events, false, {}, api);
        api.SetGlobalTable(state, "Events", eventsTable,
            { "Events", "Named event listeners and event dispatch between scripts.", false, {} });
    });
    luaBindings.AddModule("script-print", [this](LuaBindingBackend& api, sol::state& state)
    {
        BindLuaPrint(state, scriptLog, api);
    });
    luaBindings.AddModule("input-api", [this](LuaBindingBackend& api, sol::state& state)
    {
        BindLuaInput(state, scriptLog, api);
    });

    luaBindings.BindAll(lua);
}
MirageCoreRuntime::~MirageCoreRuntime() { scriptSystem.UnloadAll(); events.Clear(); }