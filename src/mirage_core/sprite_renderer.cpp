#include "mirage_core/sprite_renderer.h"
#include "mirage_core/components.h"
#include "mirage_core/component_systems.h"   // GetSpriteBounds, HitSprite
#include <algorithm>
#include <cmath>
void SpriteRenderer::CollectSorted(entt::registry& registry)
{
    auto view = registry.view<TransformComponent, SpriteComponent>();
    items_.clear();
    for (auto [e, tf, sprite] : view.each())
    {
        if (!sprite.visible) continue;
        items_.push_back({ e, sprite.layer, tf.position.z });
    }
    std::stable_sort(items_.begin(), items_.end(),
        [](const DrawItem& a, const DrawItem& b)
        {
            if (a.layer != b.layer) return a.layer < b.layer;
            return a.z < b.z;
        });
}

void SpriteRenderer::Draw(entt::registry& registry,int gridX,int gridY)
{
    CollectSorted(registry);
    auto view = registry.view<TransformComponent, SpriteComponent>();
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

        // A negative source width/height mirrors the image in raylib.
        if (sprite.flipX) src.width  = -src.width;
        if (sprite.flipY) src.height = -src.height;
        // pixels). scale = the sprite's size in world units, so scale (1,1) is
        // exactly one grid cell no matter how big the texture is.
        const float destW = tf.scale.x * gridX;
        const float destH = tf.scale.y * gridY;
        const Rectangle dest = { tf.position.x*gridX, tf.position.y*gridY, destW, destH };

        // Pivot (0..1 of the size) is where the sprite rotates and sits on position.
        const Vector2 origin = { destW * sprite.pivot.x, destH * sprite.pivot.y };

        // 2D rotation = the quaternion's rotation around Z, in degrees.
        const Quaternion q = tf.rotation;
        const float angle = std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                                       1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * RAD2DEG;

        DrawTexturePro(tex, src, dest, origin, angle, sprite.tint);
    }
}
entt::entity SpriteRenderer::Pick(entt::registry& registry, Vector2 worldPx, int grid)
{
    CollectSorted(registry);
    for (auto it = items_.rbegin(); it != items_.rend(); ++it)   // front to back
    {
        const auto& tf     = registry.get<TransformComponent>(it->entity);
        const auto& sprite = registry.get<SpriteComponent>(it->entity);
        if (HitSprite(GetSpriteBounds(tf, sprite, grid), worldPx))
            return it->entity;
    }
    return entt::null;
}