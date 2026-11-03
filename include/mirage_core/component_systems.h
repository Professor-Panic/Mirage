#pragma once
#include <cmath>
#include "raylib.h"
#include "raymath.h"            // Vector2Rotate
#include "entt/entt.hpp"
#include "mirage_core/components.h"
struct SpriteBounds {
    Vector2 position;  // world pixels: where the pivot sits (position * gridSize)
    Vector2 size;      // world pixels
    Vector2 origin;    // pivot offset inside the sprite (size * sprite.pivot)
    float   angle;     // degrees (yaw only; what the 2D gizmos use)
    Quaternion rotation = { 0.0f, 0.0f, 0.0f, 1.0f };   // full rotation (normalised), for the projected quad
};
inline SpriteBounds GetSpriteBounds(const TransformComponent& tf,
                                    const SpriteComponent& sprite,
                                    int gridSize)
{
    const float w = tf.scale.x * gridSize;
    const float h = tf.scale.y * gridSize;

    const Quaternion q = tf.rotation;
    const float angle = std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                                   1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * RAD2DEG;

    return SpriteBounds{
    { tf.position.x * gridSize, tf.position.y * gridSize },
    { w, h },
    { w * sprite.pivot.x, h * sprite.pivot.y },
    angle,
    QuaternionNormalize(q)
    };
}
// The four corners in world pixels (TL, TR, BR, BL), projected orthographically:
// the sprite is a flat quad rotated by the full quaternion, then z is dropped.
// Rotating about X or Y therefore squashes it like a card turning in front of the
// camera (Unity's 2D view of a sprite with rotation x/y), and z is the plain 2D angle.
inline void SpriteCorners(const SpriteBounds& b, Vector2 out[4])
{
    const Vector2 local[4] = { {0,0}, {b.size.x,0}, {b.size.x,b.size.y}, {0,b.size.y} };
    for (int i = 0; i < 4; ++i) {
        const Vector3 p = { local[i].x - b.origin.x, local[i].y - b.origin.y, 0.0f };
        const Vector3 r = Vector3RotateByQuaternion(p, b.rotation);
        out[i] = { r.x + b.position.x, r.y + b.position.y };
    }
}

// Point-in-quad on the projected corners (any rotation, mirrored or not).
// A sprite seen exactly edge-on has no area and can't be clicked.
inline bool HitSprite(const SpriteBounds& b, Vector2 worldPx)
{
    Vector2 c[4];
    SpriteCorners(b, c);

    float area = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const Vector2 a = c[i], d = c[(i + 1) % 4];
        area += a.x * d.y - d.x * a.y;
    }
    if (std::fabs(area) < 1.0f) return false;

    bool anyPos = false, anyNeg = false;
    for (int i = 0; i < 4; ++i) {
        const Vector2 a = c[i], d = c[(i + 1) % 4];
        const float cross = (d.x - a.x) * (worldPx.y - a.y) - (d.y - a.y) * (worldPx.x - a.x);
        if (cross > 0.0f) anyPos = true;
        if (cross < 0.0f) anyNeg = true;
    }
    return !(anyPos && anyNeg);
}
