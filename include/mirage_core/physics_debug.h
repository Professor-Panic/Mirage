#pragma once
#include "entt/entt.hpp"
#include "mirage_core/components.h"
#include <raylib.h>
#include <algorithm>
#include <cmath>

// Draws every collider as an outline, using the SAME numbers PhysicsSystem uses to
// build the Box2D shapes (scale, offset, size/radius/height, body rotation), so what
// you see is what the simulation collides with. Works before Play too, because it
// reads the Transform, not the Box2D world.
//
// Like Unity, show the colliders of the SELECTED entity only. Call it inside the
// world camera, after the sprites:
//     physics_debug::DrawEntityColliders(registry, state_.selected, kPixelsPerUnit, 1.0f / zoom);
// (DrawColliders draws every entity, if you ever want an "all colliders" toggle.)
// `lineScale` keeps lines ~1.5 screen pixels wide at any zoom (pass 1/zoom; 1 if unsure).
//
// Colours: green = solid, orange = sensor.  Toggle with F2 via physics_debug::HandleToggle().
namespace physics_debug
{
    inline bool enabled = true;

    inline void HandleToggle()
    {
        if (IsKeyPressed(KEY_F2)) enabled = !enabled;
    }

    namespace detail
    {
        struct Pose
        {
            Vector2 pos;      // body origin in world pixels
            float c, s;       // cos / sin of the body angle
            float ppu;
            // collider-local units -> world pixels (rotate, then translate)
            Vector2 ToWorld(float x, float y) const
            {
                return { pos.x + (c * x - s * y) * ppu,
                         pos.y + (s * x + c * y) * ppu };
            }
        };

        inline Pose MakePose(const TransformComponent& t, float ppu)
        {
            const float a = t.GetEulerAngles().z * DEG2RAD;
            return { { t.position.x * ppu, t.position.y * ppu }, std::cos(a), std::sin(a), ppu };
        }

        inline Color LineColor(bool sensor) { return sensor ? Color{ 255, 160, 40, 255 } : Color{ 60, 230, 110, 255 }; }

        inline void Circle(const Pose& p, Vector2 centre, float radius, float thick, Color col)
        {
            constexpr int kSeg = 32;
            Vector2 last = p.ToWorld(centre.x + radius, centre.y);
            for (int i = 1; i <= kSeg; ++i)
            {
                const float a = 2.0f * PI * static_cast<float>(i) / kSeg;
                const Vector2 next = p.ToWorld(centre.x + radius * std::cos(a), centre.y + radius * std::sin(a));
                DrawLineEx(last, next, thick, col);
                last = next;
            }
        }

        inline void Draw(const BoxCollider2DComponent& c, const TransformComponent& t, const Pose& p, float thick)
        {
            const float hx = 0.5f * c.size.x * std::fabs(t.scale.x);
            const float hy = 0.5f * c.size.y * std::fabs(t.scale.y);
            const float ox = c.offset.x * t.scale.x, oy = c.offset.y * t.scale.y;
            const Vector2 q[4] = { p.ToWorld(ox - hx, oy - hy), p.ToWorld(ox + hx, oy - hy),
                                   p.ToWorld(ox + hx, oy + hy), p.ToWorld(ox - hx, oy + hy) };
            const Color col = LineColor(c.isSensor);
            for (int i = 0; i < 4; ++i)
                DrawLineEx(q[i], q[(i + 1) % 4], thick, col);
        }

        inline void Draw(const CircleCollider2DComponent& c, const TransformComponent& t, const Pose& p, float thick)
        {
            const float s = std::max(std::fabs(t.scale.x), std::fabs(t.scale.y));
            const Vector2 centre = { c.offset.x * t.scale.x, c.offset.y * t.scale.y };
            const Color col = LineColor(c.isSensor);
            Circle(p, centre, c.radius * s, thick, col);
            // a spoke so you can see the body's rotation
            DrawLineEx(p.ToWorld(centre.x, centre.y), p.ToWorld(centre.x + c.radius * s, centre.y), thick, col);
        }

        inline void Draw(const CapsuleCollider2DComponent& c, const TransformComponent& t, const Pose& p, float thick)
        {
            const float s = std::max(std::fabs(t.scale.x), std::fabs(t.scale.y));
            const float half = 0.5f * c.height * s;
            const float r = c.radius * s;
            const Vector2 o = { c.offset.x * t.scale.x, c.offset.y * t.scale.y };
            const Vector2 a = c.horizontal ? Vector2{ half, 0.0f } : Vector2{ 0.0f, half };
            const Vector2 n = c.horizontal ? Vector2{ 0.0f, r } : Vector2{ r, 0.0f };   // side offset
            const Color col = LineColor(c.isSensor);

            Circle(p, { o.x - a.x, o.y - a.y }, r, thick, col);
            Circle(p, { o.x + a.x, o.y + a.y }, r, thick, col);
            DrawLineEx(p.ToWorld(o.x - a.x + n.x, o.y - a.y + n.y), p.ToWorld(o.x + a.x + n.x, o.y + a.y + n.y), thick, col);
            DrawLineEx(p.ToWorld(o.x - a.x - n.x, o.y - a.y - n.y), p.ToWorld(o.x + a.x - n.x, o.y + a.y - n.y), thick, col);
        }

        template <class Collider>
        void DrawAll(entt::registry& reg, float ppu, float thick)
        {
            auto view = reg.view<Collider, TransformComponent>();
            for (auto e : view)
            {
                const auto& t = view.template get<TransformComponent>(e);
                Draw(view.template get<Collider>(e), t, MakePose(t, ppu), thick);
            }
        }
    }

    inline void DrawEntityColliders(entt::registry& reg, entt::entity e, float pixelsPerUnit, float lineScale = 1.0f)
    {
        if (!enabled || e == entt::null || !reg.valid(e)) return;
        const auto* t = reg.try_get<TransformComponent>(e);
        if (!t) return;
        const float thick = 1.5f * lineScale;
        const detail::Pose pose = detail::MakePose(*t, pixelsPerUnit);
        if (const auto* c = reg.try_get<BoxCollider2DComponent>(e))     detail::Draw(*c, *t, pose, thick);
        if (const auto* c = reg.try_get<CircleCollider2DComponent>(e))  detail::Draw(*c, *t, pose, thick);
        if (const auto* c = reg.try_get<CapsuleCollider2DComponent>(e)) detail::Draw(*c, *t, pose, thick);
    }

    inline void DrawColliders(entt::registry& reg, float pixelsPerUnit, float lineScale = 1.0f)
    {
        if (!enabled) return;
        const float thick = 1.5f * lineScale;
        detail::DrawAll<BoxCollider2DComponent>(reg, pixelsPerUnit, thick);
        detail::DrawAll<CircleCollider2DComponent>(reg, pixelsPerUnit, thick);
        detail::DrawAll<CapsuleCollider2DComponent>(reg, pixelsPerUnit, thick);
    }
}
