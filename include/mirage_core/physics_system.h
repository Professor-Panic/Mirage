#pragma once
#include "entt/entt.hpp"
#include "mirage_core/components.h"
#include <box2d/box2d.h>
#include <box2d/id.h>
#include <box2d/types.h>
#include <raylib.h>
#include <optional>
#include <vector>

inline Quaternion ToQuaternion(b2Rot rot)
{
    float angle = atan2f(rot.s, rot.c);
    return { 0.0f, 0.0f, sinf(angle * 0.5f), cosf(angle * 0.5f) };
}

// Owns the Box2D world and keeps one b2Body per RigidBody2DComponent.
//
// Lifecycle (mirrors EntityBus):
//   on_construct -> reset the copied bodyId, queue the entity. The body is NOT
//                   created here: the scene loader emplaces components with defaults
//                   and fills the fields afterwards, so we wait until the next Step()
//                   (or Initialise()) when the fields are final.
//   on_destroy   -> destroy the Box2D body, so removing the component or the entity
//                   never leaks a body.
//
// Every Step():  create queued bodies -> push moved Transforms into bodies
//                -> b2World_Step -> write body poses back into Transforms.
//
// All positions are in Transform units (the same units SpriteRenderer multiplies
// by the grid size), so Box2D metres == Transform units.
class PhysicsSystem {
public:
    Vector2 gravity = { 0.0f, 9.8f };     // units/s^2, +y is down. Change with SetGravity().
    int subStepCount = 4;
    b2WorldId worldId = b2_nullWorldId;

    explicit PhysicsSystem(entt::registry& registry) : reg(registry)
    {
        InitialiseWorld();
        reg.on_construct<RigidBody2DComponent>().connect<&PhysicsSystem::OnRigidBodyAdded>(*this);
        reg.on_destroy<RigidBody2DComponent>().connect<&PhysicsSystem::OnRigidBodyRemoved>(*this);
    }

    ~PhysicsSystem()
    {
        // Disconnect first: the registry outlives us and would otherwise call into
        // a dead PhysicsSystem while it destroys its components.
        reg.on_construct<RigidBody2DComponent>().disconnect<&PhysicsSystem::OnRigidBodyAdded>(*this);
        reg.on_destroy<RigidBody2DComponent>().disconnect<&PhysicsSystem::OnRigidBodyRemoved>(*this);
        if (b2World_IsValid(worldId))
            b2DestroyWorld(worldId);     // frees every body; stale ids are then simply "invalid"
    }

    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;

    void Step(float dt)
    {
        CreatePendingBodies();
        PushTransforms();
        b2World_Step(worldId, dt, subStepCount);
        Reconcile();
    }

    // Create bodies for every rigid body that doesn't have one yet. Step() does this
    // by itself, so calling it is optional (the player still does, once, after load).
    void Initialise()
    {
        for (auto entity : reg.view<RigidBody2DComponent>())
            if (!reg.get<RigidBody2DComponent>(entity).HasBody())
                pending.push_back(entity);
        CreatePendingBodies();
        AttachColliders<BoxCollider2DComponent>();
        AttachColliders<CircleCollider2DComponent>();
        AttachColliders<CapsuleCollider2DComponent>();
    }

    void SetGravity(Vector2 g)
    {
        gravity = g;
        if (b2World_IsValid(worldId)) b2World_SetGravity(worldId, { g.x, g.y });
    }

    // ---- Forces for scripts / C++ (no-ops if the entity has no live body) ----
    void AddForce(entt::entity e, Vector2 force)          { if (auto id = Body(e)) b2Body_ApplyForceToCenter(*id, { force.x, force.y }, true); }
    void AddImpulse(entt::entity e, Vector2 impulse)      { if (auto id = Body(e)) b2Body_ApplyLinearImpulseToCenter(*id, { impulse.x, impulse.y }, true); }
    void AddTorque(entt::entity e, float torque)          { if (auto id = Body(e)) b2Body_ApplyTorque(*id, torque, true); }
    void AddAngularImpulse(entt::entity e, float impulse) { if (auto id = Body(e)) b2Body_ApplyAngularImpulse(*id, impulse, true); }

private:
    entt::registry& reg;
    b2WorldDef worldDef;
    std::vector<entt::entity> pending;     // components added, body not created yet

    void InitialiseWorld()
    {
        worldDef = b2DefaultWorldDef();
        worldDef.gravity = { gravity.x, gravity.y };
        worldId = b2CreateWorld(&worldDef);
    }

    // ---- registry listeners ----
    void OnRigidBodyAdded(entt::registry& r, entt::entity e)
    {
        // A component copied from another entity carries that entity's bodyId.
        r.get<RigidBody2DComponent>(e).bodyId = b2_nullBodyId;
        pending.push_back(e);
    }

    void OnRigidBodyRemoved(entt::registry& r, entt::entity e)
    {
        auto& rb = r.get<RigidBody2DComponent>(e);
        if (rb.HasBody())
            b2DestroyBody(rb.bodyId);
        rb.bodyId = b2_nullBodyId;
    }

    // ---- helpers ----
    std::optional<b2BodyId> Body(entt::entity e)
    {
        if (!reg.valid(e)) return std::nullopt;
        auto* rb = reg.try_get<RigidBody2DComponent>(e);
        if (!rb || !rb->HasBody()) return std::nullopt;
        return rb->bodyId;
    }

    void CreatePendingBodies()
    {
        if (pending.empty()) return;
        std::vector<entt::entity> work;
        work.swap(pending);                     // CreateBody must not touch `pending` while we iterate

        for (entt::entity e : work)
        {
            if (!reg.valid(e)) continue;
            auto* rb = reg.try_get<RigidBody2DComponent>(e);
            if (!rb || rb->HasBody()) continue;
            auto* tf = reg.try_get<TransformComponent>(e);
            if (!tf) { pending.push_back(e); continue; }   // no Transform yet: try again next step
            CreateBody(*rb, *tf);
        }
    }

    void CreateBody(RigidBody2DComponent& rb, const TransformComponent& tf)
    {
        b2BodyDef def = b2DefaultBodyDef();
        def.type            = ToB2BodyType(rb.type);
        def.position        = { tf.position.x, tf.position.y };
        def.rotation        = b2MakeRot(tf.GetEulerAngles().z * DEG2RAD);
        def.linearVelocity  = { rb.linearVelocity.x, rb.linearVelocity.y };
        def.angularVelocity = rb.angularVelocity * DEG2RAD;
        def.linearDamping   = rb.linearDamping;
        def.angularDamping  = rb.angularDamping;
        def.gravityScale    = rb.gravityScale;
        def.motionLocks.angularZ = rb.fixedRotation;
        def.enableSleep     = rb.allowSleep;
        def.isBullet        = rb.bullet;

        rb.bodyId = b2CreateBody(worldId, &def);
        if (!rb.enabled) b2Body_Disable(rb.bodyId);
        rb.ApplyManualMass();

        rb.syncedPosition = { tf.position.x, tf.position.y };
        rb.syncedRotation = tf.rotation;
    }
    b2ShapeDef MakeShapeDef(const ColliderBase& c, entt::entity e) {
        b2ShapeDef def = b2DefaultShapeDef();
        def.density = c.density;
        def.material.friction    = c.friction;  
        def.material.restitution = c.restitution;
        def.isSensor = c.isSensor;
        def.enableSensorEvents  = true;  
        def.enableContactEvents = true;
        def.userData = (void*)(uintptr_t)entt::to_integral(e);  
        return def;
    }

    void CreateShape(b2BodyId body, entt::entity e, BoxCollider2DComponent& c, const TransformComponent& t) {
        const b2ShapeDef def = MakeShapeDef(c, e);
        const b2Polygon box = b2MakeOffsetBox(
            0.5f * c.size.x * fabsf(t.scale.x), 0.5f * c.size.y * fabsf(t.scale.y),
            { c.offset.x * t.scale.x, c.offset.y * t.scale.y }, b2MakeRot(0.0f));
        c.shapeId = b2CreatePolygonShape(body, &def, &box);
    }
    void CreateShape(b2BodyId body, entt::entity e, CircleCollider2DComponent& c, const TransformComponent& t) {
        const b2ShapeDef def = MakeShapeDef(c, e);
        const float s = std::max(fabsf(t.scale.x), fabsf(t.scale.y));
        const b2Circle circle = { { c.offset.x * t.scale.x, c.offset.y * t.scale.y }, c.radius * s };
        c.shapeId = b2CreateCircleShape(body, &def, &circle);
    }

    void CreateShape(b2BodyId body, entt::entity e, CapsuleCollider2DComponent& c, const TransformComponent& t) {
        const b2ShapeDef def = MakeShapeDef(c, e);
        const float s    = std::max(fabsf(t.scale.x), fabsf(t.scale.y));
        const float half = 0.5f * c.height * s;
        const b2Vec2 o   = { c.offset.x * t.scale.x, c.offset.y * t.scale.y };
        const b2Vec2 a   = c.horizontal ? b2Vec2{ half, 0.0f } : b2Vec2{ 0.0f, half };
        const b2Capsule capsule = { { o.x - a.x, o.y - a.y }, { o.x + a.x, o.y + a.y }, c.radius * s };
        c.shapeId = b2CreateCapsuleShape(body, &def, &capsule);
    }
    template <class Collider>
    void AttachColliders() {
        auto view = reg.view<Collider, TransformComponent>();
        for (auto e : view)
            CreateShape(BodyFor(e), e, view.template get<Collider>(e),
                        view.template get<TransformComponent>(e));
    }
    b2BodyId BodyFor(entt::entity e){
        if (auto* rb = reg.try_get<RigidBody2DComponent>(e))
            return rb->bodyId;

        const auto& t = reg.get<TransformComponent>(e);
        b2BodyDef def = b2DefaultBodyDef();
        def.type     = b2_staticBody;
        def.position = { t.position.x, t.position.y };
        def.rotation = b2MakeRot(t.GetEulerAngles().z * DEG2RAD);
        return b2CreateBody(worldId, &def);
    }
    // Transform -> body, only when something other than physics changed the Transform
    // (a script, the editor). Lets scripts teleport bodies and move kinematic ones.
    void PushTransforms()
    {
        auto view = reg.view<RigidBody2DComponent, TransformComponent>();
        for (auto entity : view)
        {
            auto& rb = view.get<RigidBody2DComponent>(entity);
            auto& tf = view.get<TransformComponent>(entity);
            if (!rb.HasBody() || !rb.enabled) continue;

            const bool moved = tf.position.x != rb.syncedPosition.x ||
                               tf.position.y != rb.syncedPosition.y ||
                               !rotation::SameRotation(tf.rotation, rb.syncedRotation);
            if (!moved) continue;

            b2Body_SetTransform(rb.bodyId, { tf.position.x, tf.position.y },
                                b2MakeRot(tf.GetEulerAngles().z * DEG2RAD));
            b2Body_SetAwake(rb.bodyId, true);
            rb.syncedPosition = { tf.position.x, tf.position.y };
            rb.syncedRotation = tf.rotation;
        }
    }

    // body -> Transform. Static bodies never move and sleeping ones didn't change, so skip them.
    void Reconcile()
    {
        auto view = reg.view<RigidBody2DComponent, TransformComponent>();
        for (auto entity : view)
        {
            auto& rb = view.get<RigidBody2DComponent>(entity);
            auto& tf = view.get<TransformComponent>(entity);
            if (!rb.HasBody() || !rb.enabled || rb.type == RigidBody::Static) continue;
            if (!b2Body_IsAwake(rb.bodyId)) continue;

            auto pos = b2Body_GetPosition(rb.bodyId);
            b2Rot rot = b2Body_GetRotation(rb.bodyId);

            tf.position.x = pos.x;
            tf.position.y = pos.y;

            // Only the z (2D) angle comes from physics; keep any x/y rotation and the Euler hint.
            Vector3 euler = tf.GetEulerAngles();
            euler.z = atan2f(rot.s, rot.c) * RAD2DEG;
            tf.SetEulerAngles(euler);

            rb.syncedPosition = { tf.position.x, tf.position.y };
            rb.syncedRotation = tf.rotation;
        }
    }
};