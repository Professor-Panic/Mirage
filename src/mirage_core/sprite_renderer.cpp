#include "mirage_core/sprite_renderer.h"
#include "mirage_core/components.h"
#include "mirage_core/component_systems.h"   // GetSpriteBounds, HitSprite

#include <algorithm>
#include <cmath>
#include "rlgl.h"
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

    // A sprite turned about X or Y shows its back face; Unity's sprite shader is
    // double-sided, so don't cull. Flush first so earlier batched draws keep culling.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
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

        // Flat (z-only) rotation: the normal fast path, identical to before.
        if (std::fabs(q.x) < 1e-6f && std::fabs(q.y) < 1e-6f)
        {
            DrawTexturePro(tex, src, dest, origin, angle, sprite.tint);
            continue;
        }

        // Rotated about X and/or Y: draw the quad projected through the full rotation.
        const Quaternion qn = QuaternionNormalize(q);
        Vector2 c[4];                       // TL, TR, BR, BL
        {
            const Vector2 local[4] = { {0,0}, {destW,0}, {destW,destH}, {0,destH} };
            for (int i = 0; i < 4; ++i) {
                const Vector3 p = { local[i].x - origin.x, local[i].y - origin.y, 0.0f };
                const Vector3 r = Vector3RotateByQuaternion(p, qn);
                c[i] = { r.x + dest.x, r.y + dest.y };
            }
        }
        float u0 = src.x / tex.width,  u1 = (src.x + std::fabs(src.width))  / tex.width;
        float v0 = src.y / tex.height, v1 = (src.y + std::fabs(src.height)) / tex.height;
        if (sprite.flipX) std::swap(u0, u1);
        if (sprite.flipY) std::swap(v0, v1);

        rlSetTexture(tex.id);
        rlBegin(RL_QUADS);
            rlColor4ub(sprite.tint.r, sprite.tint.g, sprite.tint.b, sprite.tint.a);
            rlNormal3f(0.0f, 0.0f, 1.0f);
            rlTexCoord2f(u0, v0); rlVertex2f(c[0].x, c[0].y);   // TL
            rlTexCoord2f(u0, v1); rlVertex2f(c[3].x, c[3].y);   // BL
            rlTexCoord2f(u1, v1); rlVertex2f(c[2].x, c[2].y);   // BR
            rlTexCoord2f(u1, v0); rlVertex2f(c[1].x, c[1].y);   // TR
        rlEnd();
        rlSetTexture(0);
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
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