#pragma once
#include <cmath>
#include "raylib.h"
#include "raymath.h"
#include "string"
#include <algorithm>
#include <cctype>
#include <vector>
#include "box2d/box2d.h"
// ---------------------------------------------------------------------------
// Rotation helpers: Euler angles (degrees) <-> quaternion.
//
// Convention: x = roll about X, y = pitch about Y, z = yaw about Z, applied in
// the order X, then Y, then Z (q = Rz * Ry * Rx). With x = y = 0 the yaw IS the
// 2D sprite angle (SpriteRenderer and GetSpriteBounds read the quaternion's yaw
// with the same formula), so every existing 2D rotation keeps its meaning.
// ---------------------------------------------------------------------------
namespace rotation {

inline Quaternion FromEulerDegrees(Vector3 degrees)
{
    const float hx = degrees.x * DEG2RAD * 0.5f;
    const float hy = degrees.y * DEG2RAD * 0.5f;
    const float hz = degrees.z * DEG2RAD * 0.5f;
    const float cx = std::cos(hx), sx = std::sin(hx);
    const float cy = std::cos(hy), sy = std::sin(hy);
    const float cz = std::cos(hz), sz = std::sin(hz);

    Quaternion q;
    q.w = cz * cy * cx + sz * sy * sx;
    q.x = cz * cy * sx - sz * sy * cx;
    q.y = cz * sy * cx + sz * cy * sx;
    q.z = sz * cy * cx - cz * sy * sx;
    return q;
}

// Euler angles for a quaternion, in (-180, 180] for x and z and [-90, 90] for y.
// At y = +-90 (gimbal lock) x and z are not independent; x is returned as 0.
inline Vector3 ToEulerDegrees(Quaternion q)
{
    const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length < 1e-12f) return { 0.0f, 0.0f, 0.0f };
    q = { q.x / length, q.y / length, q.z / length, q.w / length };

    // Entries of the rotation matrix of q.
    const float r11 = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    const float r21 = 2.0f * (q.x * q.y + q.w * q.z);
    const float r31 = 2.0f * (q.x * q.z - q.w * q.y);
    const float r32 = 2.0f * (q.y * q.z + q.w * q.x);
    const float r33 = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);

    float x, y, z;
    if (std::fabs(r31) < 0.999999f)
    {
        y = -std::asin(r31);
        x = std::atan2(r32, r33);
        z = std::atan2(r21, r11);
    }
    else
    {
        const float r12 = 2.0f * (q.x * q.y - q.w * q.z);
        const float r22 = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
        y = r31 < 0.0f ? PI * 0.5f : -PI * 0.5f;
        x = 0.0f;
        z = std::atan2(-r12, r22);
    }
    return { x * RAD2DEG, y * RAD2DEG, z * RAD2DEG };
}

// True if a and b are the same rotation (q and -q are the same rotation).
inline bool SameRotation(Quaternion a, Quaternion b)
{
    const float sign = (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) < 0.0f ? -1.0f : 1.0f;
    const float tolerance = 5e-7f;
    return std::fabs(a.x - sign * b.x) <= tolerance &&
           std::fabs(a.y - sign * b.y) <= tolerance &&
           std::fabs(a.z - sign * b.z) <= tolerance &&
           std::fabs(a.w - sign * b.w) <= tolerance;
}

}  // namespace rotation

struct TransformComponent {
    Vector3 position;
    Vector3 scale;
    Quaternion rotation;        // the stored truth; the renderer, gizmos and Lua use it

    // The Euler angles last typed or dragged, in degrees. A quaternion has many
    // equivalent Euler triples (100 degrees of pitch comes back as (180, 80, 180)),
    // so the inspector would flip what the user just typed. The hint is trusted only
    // while it still produces `rotation`; any other change to the quaternion (a Lua
    // script, a scene load, undo) makes GetEulerAngles() derive fresh angles instead.
    Vector3 eulerHint;

    TransformComponent()
        : position(Vector3Zero()),
          scale(Vector3One()),
          rotation(QuaternionIdentity()),
          eulerHint(Vector3Zero()) {}

    // Euler angles in degrees, for the inspector and for scripts.
    Vector3 GetEulerAngles() const
    {
        if (rotation::SameRotation(rotation::FromEulerDegrees(eulerHint), rotation))
            return eulerHint;
        return rotation::ToEulerDegrees(rotation);
    }

    void SetEulerAngles(Vector3 degrees)
    {
        eulerHint = degrees;
        rotation  = rotation::FromEulerDegrees(degrees);
    }
};

// ---------------------------------------------------------------------------
// Tags: free-form labels on an entity ("boid", "enemy"), used to find groups of
// entities from Lua (Entities.FindWithTag). Stored as ONE comma-separated string
// so it saves, loads, clones and shows in the inspector like any string field.
// Tags are case-sensitive, surrounding spaces are ignored, and a tag cannot
// contain a comma.
// ---------------------------------------------------------------------------
struct TagsComponent {
    std::string tags;       // "boid, flock"

    static std::string Trim(const std::string& s)
    {
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
        return s.substr(a, b - a);
    }

    std::vector<std::string> List() const
    {
        std::vector<std::string> out;
        std::string current;
        for (char c : tags + ",")
        {
            if (c == ',')
            {
                const std::string t = Trim(current);
                if (!t.empty()) out.push_back(t);
                current.clear();
            }
            else current += c;
        }
        return out;
    }

    bool Has(const std::string& tag) const
    {
        const std::string wanted = Trim(tag);
        if (wanted.empty()) return false;
        const std::vector<std::string> all = List();
        return std::find(all.begin(), all.end(), wanted) != all.end();
    }

    // false if the tag is empty, contains a comma, or is already present.
    bool Add(const std::string& tag)
    {
        const std::string t = Trim(tag);
        if (t.empty() || t.find(',') != std::string::npos || Has(t)) return false;
        tags = tags.empty() ? t : tags + ", " + t;
        return true;
    }

    // false if the tag was not there.
    bool Remove(const std::string& tag)
    {
        const std::string wanted = Trim(tag);
        std::vector<std::string> all = List();
        const auto it = std::find(all.begin(), all.end(), wanted);
        if (it == all.end()) return false;
        all.erase(it);
        tags.clear();
        for (const std::string& t : all) tags += tags.empty() ? t : ", " + t;
        return true;
    }
};

struct GameComponent {
    std::string name;

    GameComponent(std::string name) : name(std::move(name)) {}
};
struct SpriteComponent {
    std::string path;                           // texture file, relative to the project root
    Color   tint    = WHITE;
    Vector2 pivot   = { 0.5f, 0.5f };           // 0..1 of the sprite's size; (0.5, 0.5) = centre
    Vector4 source  = { 0.0f, 0.0f, 0.0f, 0.0f }; // x, y, w, h in pixels; all zero = whole texture
    bool    flipX   = false;
    bool    flipY   = false;
    bool    visible = true;
    int     layer   = 1;                        // lower draws first (behind)
};

enum class RigidBody { Dynamic, Kinematic, Static };   // order = the int the inspector/Lua sees: 0, 1, 2

inline b2BodyType ToB2BodyType(RigidBody t)
{
    switch (t) {
        case RigidBody::Static:    return b2_staticBody;
        case RigidBody::Kinematic: return b2_kinematicBody;
        default:                   return b2_dynamicBody;
    }
}

struct RigidBody2DComponent {
    // ---- Settings: reflected, saved in scenes, copied when an entity is cloned ----
    RigidBody type          = RigidBody::Dynamic;
    float     mass          = 1.0f;      // only used while the entity has NO colliders (see ApplyManualMass)
    float     gravityScale  = 1.0f;
    float     linearDamping  = 0.0f;
    float     angularDamping = 0.0f;
    bool      fixedRotation = false;
    bool      allowSleep    = true;
    bool      bullet        = false;     // continuous collision for fast bodies
    bool      enabled       = true;      // false = removed from the simulation
    Vector2   linearVelocity  = { 0.0f, 0.0f };   // units/s. Before the body exists: the START velocity
    float     angularVelocity = 0.0f;             // degrees/s (same convention as eulerAngles)

    // ---- Runtime state: NOT reflected, never saved ----
    b2BodyId   bodyId = b2_nullBodyId;
    Vector2    syncedPosition = { 0.0f, 0.0f };          // what physics last wrote to the Transform;
    Quaternion syncedRotation = { 0.0f, 0.0f, 0.0f, 1.0f }; // a difference means a script/editor moved it

    bool HasBody() const { return b2Body_IsValid(bodyId); }
    void SetFixedRotation(bool f)
    {
        fixedRotation = f;
        if (HasBody())
        {
            b2MotionLocks locks = b2Body_GetMotionLocks(bodyId);
            locks.angularZ = f;
            b2Body_SetMotionLocks(bodyId, locks);
        }
    }
    // --- Setters that also push the change into a live body. Before the body
    //     exists (scene load, inspector, clone) they only store the value. ---
    void SetType(RigidBody t)
    {
        type = t;
        if (HasBody()) { b2Body_SetType(bodyId, ToB2BodyType(t)); ApplyManualMass(); }
    }
    void SetMass(float m)
    {
        mass = m < 0.0001f ? 0.0001f : m;
        ApplyManualMass();
    }
    void SetGravityScale(float g)    { gravityScale = g;    if (HasBody()) b2Body_SetGravityScale(bodyId, g); }
    void SetLinearDamping(float d)   { linearDamping = d;   if (HasBody()) b2Body_SetLinearDamping(bodyId, d); }
    void SetAngularDamping(float d)  { angularDamping = d;  if (HasBody()) b2Body_SetAngularDamping(bodyId, d); }
    void SetAllowSleep(bool a)       { allowSleep = a;      if (HasBody()) b2Body_EnableSleeping(bodyId, a); }
    void SetBullet(bool b)           { bullet = b;          if (HasBody()) b2Body_SetBullet(bodyId, b); }
    void SetEnabled(bool e)
    {
        enabled = e;
        if (HasBody()) { if (e) b2Body_Enable(bodyId); else b2Body_Disable(bodyId); }
    }

    // --- Velocity: reads the LIVE body when there is one, so scripts see real values ---
    Vector2 GetLinearVelocity() const
    {
        if (HasBody()) { b2Vec2 v = b2Body_GetLinearVelocity(bodyId); return { v.x, v.y }; }
        return linearVelocity;
    }
    void SetLinearVelocity(Vector2 v)
    {
        linearVelocity = v;
        if (HasBody()) b2Body_SetLinearVelocity(bodyId, { v.x, v.y });
    }
    float GetAngularVelocity() const
    {
        if (HasBody()) return b2Body_GetAngularVelocity(bodyId) * RAD2DEG;
        return angularVelocity;
    }
    void SetAngularVelocity(float degreesPerSecond)
    {
        angularVelocity = degreesPerSecond;
        if (HasBody()) b2Body_SetAngularVelocity(bodyId, degreesPerSecond * DEG2RAD);
    }

    // Box2D takes mass from shapes. With no shapes a body has zero mass and would
    // ignore gravity and forces, so (like Unity) we apply `mass` ourselves. Once
    // colliders exist, the collider code should create shapes and the shape-derived
    // mass wins (the GetShapeCount check below stops us overriding it).
    // The inertia is that of a 1x1 box: mass * (1 + 1) / 12.
    void ApplyManualMass()
    {
        if (!HasBody() || type != RigidBody::Dynamic) return;
        if (b2Body_GetShapeCount(bodyId) > 0) return;
        b2MassData md{};
        md.mass = mass;
        md.center = { 0.0f, 0.0f };
        md.rotationalInertia = mass / 6.0f;
        b2Body_SetMassData(bodyId, md);
    }
};
struct ColliderBase {
    Vector2 offset      = { 0, 0 };   // relative to the entity's position
    bool    isSensor    = false;      // detects overlaps, no physical response
    float   density     = 1.0f;       // 0 on a dynamic body = no mass
    float   friction    = 0.6f;
    float   restitution = 0.0f;       // bounciness
    b2ShapeId shapeId   = b2_nullShapeId;   // runtime only: don't reflect, don't copy
};
struct BoxCollider2DComponent     : ColliderBase { Vector2 size = {1, 1}; };      // full width/height
struct CircleCollider2DComponent  : ColliderBase { float radius = 0.5f; };
struct CapsuleCollider2DComponent : ColliderBase { float radius = 0.25f; float height = 1.0f; bool horizontal = false; };