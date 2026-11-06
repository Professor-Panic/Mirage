#include "mirage_core/lua_native.h"  // Declares BindLuaNativeHandles.
#include "mirage_core/runtime.h"
#include "mirage_core/components.h"
#include "mirage_core/entity_lua.h"
#include "mirage_core/script_system.h"
#include "mirage_core/lua_input.h"
#include "mirage_core/lua_functions.h"
#include <box2d/types.h>
MirageCoreRuntime::MirageCoreRuntime():
    entityBus(registry),
    luaBindings(scriptApi),
    scriptSystem(lua, reflection, registry, entityBus, events, luaBindings),
    physicsSystem(registry)
{
    lua.open_libraries(
        sol::lib::base,
        sol::lib::package,
        sol::lib::math,
        sol::lib::string,
        sol::lib::table
    );
    //physics
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
    reflection.Component<RigidBody2DComponent>("RigidBody2D")
    // enum -> int, see note 1. Order matches the enum: 0 Dynamic, 1 Kinematic, 2 Static
    .Property<int>("type",
        [](const RigidBody2DComponent& r) { return static_cast<int>(r.type); },
        [](RigidBody2DComponent& r, int v) { r.SetType(static_cast<RigidBody>(std::clamp(v, 0, 2))); })
        .ScriptDescription("Body type: 0 = Dynamic, 1 = Kinematic, 2 = Static.")
    .Property<float>("mass",
        [](const RigidBody2DComponent& r) { return r.mass; },
        [](RigidBody2DComponent& r, float v) { r.SetMass(v); })
        .ScriptDescription("Manual mass. Only used while the entity has no colliders.")
    .Property<float>("gravityScale",
        [](const RigidBody2DComponent& r) { return r.gravityScale; },
        [](RigidBody2DComponent& r, float v) { r.SetGravityScale(v); })
        .ScriptDescription("Multiplier on world gravity. 0 = floats, negative = falls upward.")
    .Property<float>("linearDamping",
        [](const RigidBody2DComponent& r) { return r.linearDamping; },
        [](RigidBody2DComponent& r, float v) { r.SetLinearDamping(v); })
        .ScriptDescription("Drag on linear velocity.")
    .Property<float>("angularDamping",
        [](const RigidBody2DComponent& r) { return r.angularDamping; },
        [](RigidBody2DComponent& r, float v) { r.SetAngularDamping(v); })
        .ScriptDescription("Drag on rotation.")
    .Property<bool>("fixedRotation",
        [](const RigidBody2DComponent& r) { return r.fixedRotation; },
        [](RigidBody2DComponent& r, bool v) { r.SetFixedRotation(v); })
        .ScriptDescription("Locks rotation so the body never spins.")
    .Property<bool>("allowSleep",
        [](const RigidBody2DComponent& r) { return r.allowSleep; },
        [](RigidBody2DComponent& r, bool v) { r.SetAllowSleep(v); })
        .ScriptDescription("Lets the body sleep when it comes to rest.")
    .Property<bool>("bullet",
        [](const RigidBody2DComponent& r) { return r.bullet; },
        [](RigidBody2DComponent& r, bool v) { r.SetBullet(v); })
        .ScriptDescription("Continuous collision for fast bodies, so they don't tunnel.")
    .Property<bool>("enabled",
        [](const RigidBody2DComponent& r) { return r.enabled; },
        [](RigidBody2DComponent& r, bool v) { r.SetEnabled(v); })
        .ScriptDescription("False removes the body from the simulation.")
    .Property<Vector2>("linearVelocity",
        [](const RigidBody2DComponent& r) { return r.GetLinearVelocity(); },
        [](RigidBody2DComponent& r, const Vector2& v) { r.SetLinearVelocity(v); })
        .ScriptDescription("Units per second. Before the body exists this is the start velocity.").Group("Velocity")
    .Property<float>("angularVelocity",
        [](const RigidBody2DComponent& r) { return r.GetAngularVelocity(); },
        [](RigidBody2DComponent& r, float v) { r.SetAngularVelocity(v); })
        .ScriptDescription("Degrees per second.")
    .ScriptExposed("Box2D rigid body settings and live velocity.")
    .Group("Velocity");
    reflection.Component<BoxCollider2DComponent>("BoxCollider2D")
    .Field<Vector2>("size", &BoxCollider2DComponent::size)
        .ScriptDescription("Full width and height in world units, multiplied by the transform's scale.")
    .Field<Vector2>("offset", &BoxCollider2DComponent::offset)
        .ScriptDescription("Centre of the box relative to the entity's position.")
    .Field<bool>("isSensor", &BoxCollider2DComponent::isSensor)
        .ScriptDescription("Detects overlaps but causes no physical response.")
    .Field<float>("density", &BoxCollider2DComponent::density)
        .ScriptDescription("Mass per area. Zero on a dynamic body means it has no mass.").Group("Material")
    .Field<float>("friction", &BoxCollider2DComponent::friction)
        .ScriptDescription("Surface friction, usually 0 to 1.").Group("Material")
    .Field<float>("restitution", &BoxCollider2DComponent::restitution)
        .ScriptDescription("Bounciness. 0 = no bounce, 1 = perfectly elastic.").Group("Material")
    .ScriptExposed("Box collider attached to the entity's rigid body (or a static body if it has none).");

reflection.Component<CircleCollider2DComponent>("CircleCollider2D")
    .Field<float>("radius", &CircleCollider2DComponent::radius)
        .ScriptDescription("Radius in world units, multiplied by the larger axis of the transform's scale.")
    .Field<Vector2>("offset", &CircleCollider2DComponent::offset)
        .ScriptDescription("Centre of the circle relative to the entity's position.")
    .Field<bool>("isSensor", &CircleCollider2DComponent::isSensor)
        .ScriptDescription("Detects overlaps but causes no physical response.")
    .Field<float>("density", &CircleCollider2DComponent::density)
        .ScriptDescription("Mass per area. Zero on a dynamic body means it has no mass.").Group("Material")
    .Field<float>("friction", &CircleCollider2DComponent::friction)
        .ScriptDescription("Surface friction, usually 0 to 1.").Group("Material")
    .Field<float>("restitution", &CircleCollider2DComponent::restitution)
        .ScriptDescription("Bounciness. 0 = no bounce, 1 = perfectly elastic.").Group("Material")
    .ScriptExposed("Circle collider attached to the entity's rigid body (or a static body if it has none).");

reflection.Component<CapsuleCollider2DComponent>("CapsuleCollider2D")
    .Field<float>("radius", &CapsuleCollider2DComponent::radius)
        .ScriptDescription("Radius of the two end caps in world units.")
    .Field<float>("height", &CapsuleCollider2DComponent::height)
        .ScriptDescription("Distance between the two cap centres; total length is height + 2 * radius.")
    .Field<bool>("horizontal", &CapsuleCollider2DComponent::horizontal)
        .ScriptDescription("True lays the capsule on its side.")
    .Field<Vector2>("offset", &CapsuleCollider2DComponent::offset)
        .ScriptDescription("Centre of the capsule relative to the entity's position.")
    .Field<bool>("isSensor", &CapsuleCollider2DComponent::isSensor)
        .ScriptDescription("Detects overlaps but causes no physical response.")
    .Field<float>("density", &CapsuleCollider2DComponent::density)
        .ScriptDescription("Mass per area. Zero on a dynamic body means it has no mass.").Group("Material")
    .Field<float>("friction", &CapsuleCollider2DComponent::friction)
        .ScriptDescription("Surface friction, usually 0 to 1.").Group("Material")
    .Field<float>("restitution", &CapsuleCollider2DComponent::restitution)
        .ScriptDescription("Bounciness. 0 = no bounce, 1 = perfectly elastic.").Group("Material")
    .ScriptExposed("Capsule collider attached to the entity's rigid body (or a static body if it has none).");
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