#include "mirage_core/sprite_renderer.h"
#include "mirage_core/components.h"
#include <algorithm>
#include <cmath>

void SpriteRenderer::Draw(entt::registry& registry)
{
    auto view = registry.view<TransformComponent, SpriteComponent>();

    // 1. Collect: only what the sort needs.
    items_.clear();
    for (auto [e, tf, sprite] : view.each())
    {
        if (!sprite.visible) continue;
        items_.push_back({ e, sprite.layer, tf.position.z });
    }

    // 2. Sort by layer, then by z. Stable, so equal sprites don't flicker.
    std::stable_sort(items_.begin(), items_.end(),
        [](const DrawItem& a, const DrawItem& b)
        {
            if (a.layer != b.layer) return a.layer < b.layer;
            return a.z < b.z;
        });

    // 3. Draw in that order.
    for (const DrawItem& item : items_)
    {
        const TransformComponent& tf     = view.get<TransformComponent>(item.entity);
        const SpriteComponent&    sprite = view.get<SpriteComponent>(item.entity);

        // Fetch every frame; never keep this reference (Reload invalidates it).
        const Texture2D& tex = assets_.Get(sprite.path);

        // Source rectangle. All zeros means "the whole texture".
        Rectangle src = { sprite.source.x, sprite.source.y, sprite.source.z, sprite.source.w };
        if (src.width <= 0.0f || src.height <= 0.0f)
            src = { 0.0f, 0.0f, (float)tex.width, (float)tex.height };

        const float width  = src.width;     // size before the flip is applied
        const float height = src.height;

        // A negative source width/height mirrors the image in raylib.
        if (sprite.flipX) src.width  = -src.width;
        if (sprite.flipY) src.height = -src.height;

        // Destination: position in pixels, size = source size * scale.
        const float destW = width  * tf.scale.x;
        const float destH = height * tf.scale.y;
        const Rectangle dest = { tf.position.x, tf.position.y, destW, destH };

        // Pivot (0..1 of the size) is where the sprite rotates and sits on position.
        const Vector2 origin = { destW * sprite.pivot.x, destH * sprite.pivot.y };

        // 2D rotation = the quaternion's rotation around Z, in degrees.
        const Quaternion q = tf.rotation;
        const float angle = std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                                       1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * RAD2DEG;

        DrawTexturePro(tex, src, dest, origin, angle, sprite.tint);
    }
}