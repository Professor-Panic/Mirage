#pragma once
#include "components.h"
#include "raylib.h"
#include "entt/entt.hpp"
#include "mirage_core/components.h"
struct SpriteBounds {
    Vector2 position;  // world pixels: where the pivot sits (position * gridSize)
    Vector2 size;      // world pixels
    Vector2 origin;    // pivot offset inside the sprite (size * sprite.pivot)
    float   angle;     // degrees
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
    angle
    };
}
inline bool HitSprite(const SpriteBounds& b, Vector2 worldPx)
{
    Vector2 p = { worldPx.x - b.position.x, worldPx.y - b.position.y };
    p = Vector2Rotate(p, -b.angle * DEG2RAD);   // undo the sprite's rotation
    p = { p.x + b.origin.x, p.y + b.origin.y }; // pivot-relative -> top-left-relative
    return p.x >= 0 && p.y >= 0 && p.x <= b.size.x && p.y <= b.size.y;
}

inline void SpriteCorners(const SpriteBounds& b, Vector2 out[4])
{
    const Vector2 local[4] = { {0,0}, {b.size.x,0}, {b.size.x,b.size.y}, {0,b.size.y} };
    for (int i = 0; i < 4; ++i) {
        Vector2 p = { local[i].x - b.origin.x, local[i].y - b.origin.y };
        p = Vector2Rotate(p, b.angle * DEG2RAD);
        out[i] = { p.x + b.position.x, p.y + b.position.y };
    }
}