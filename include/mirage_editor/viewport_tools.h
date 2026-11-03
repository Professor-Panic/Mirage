#pragma once
#include "raylib.h"
#include "raymath.h"
#include "rui.h"
#include "mirage_core/component_systems.h"   // SpriteBounds, GetSpriteBounds, SpriteCorners
#include "mirage_core/components.h"
#include "mirage_core/constants.h"           // kPixelsPerUnit
#include "mirage_core/runtime.h"
#include "mirage_core/sprite_renderer.h"
#include "mirage_editor/editor_state.h"

// The scene viewport (the <viewport> in ui/main.rxml).
//
// The viewport widget owns pan (middle mouse) and wheel zoom, and clips drawing
// to its panel. Its draw handler runs inside its transform, so everything drawn
// here is in world pixels (world units * kPixelsPerUnit).
//
// Markup: <viewport id="scene_view" minZoom="0.1" maxZoom="10"
//                   onUpdate="sceneViewUpdate"
//                   onBackgroundDraw="sceneViewDraw"/>
//
// Picking, the selection outline and (next) the gizmos and drag live here.
class ViewportTools
{
public:
    ViewportTools(rui::Context& ui, MirageCoreRuntime& core, EditorState& state,
                  SpriteRenderer& sprites)
        : core_(core), state_(state), sprites_(sprites)
    {
        ui.runtime.Function("sceneViewUpdate", [this](const rui::Event& e) { OnUpdate(e); });
        ui.runtime.Function("sceneViewDraw",   [this](const rui::Event& e) { OnDraw(e); });
    }

    ViewportTools(const ViewportTools&) = delete;
    ViewportTools& operator=(const ViewportTools&) = delete;

private:
    void OnUpdate(const rui::Event& e)
    {
        auto* view = static_cast<rui::Viewport*>(e.native_widget);
        if (!view) return;

        // Show world (0,0) at the panel centre, like the old Camera2D did.
        // Done once, as soon as layout has given the panel a real size.
        // (A markup rebuild keeps the camera, so this does not repeat.)
        if (!centred_ && view->rect.transform.width > 1.0f)
        {
            view->CenterOn({ 0.0f, 0.0f });
            centred_ = true;
        }

        if (state_.mode != Mode::Edit) return;

        // Click-to-select: press and release close together (screen pixels, so
        // the threshold does not depend on zoom).
        if (view->hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            pressInView_ = true;
            pressScreen_ = GetMousePosition();
        }
        if (pressInView_ && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            pressInView_ = false;
            const float moved = Vector2Distance(GetMousePosition(), pressScreen_);
            if (moved < 4.0f)
                state_.selected = sprites_.Pick(core_.registry, view->world_mouse, kPixelsPerUnit);
        }
    }

    void OnDraw(const rui::Event& e)
    {
        const auto* view = static_cast<const rui::Viewport*>(e.native_widget);
        const float zoom = view ? view->camera.zoom : 1.0f;

        // No ClearBackground here: that would wipe the whole window. The panel's
        // background comes from the viewport's RCSS (background-color).
        DrawRectangleLinesEx({0, 0, 1600, 900}, 2.0f / zoom, WHITE);
        sprites_.Draw(core_.registry, kPixelsPerUnit, kPixelsPerUnit);

        DrawSelection(zoom);
    }

    // Outline of the selected sprite plus a pivot marker.
    void DrawSelection(float zoom)
    {
        if (!core_.registry.valid(state_.selected)) return;

        const auto* tf     = core_.registry.try_get<TransformComponent>(state_.selected);
        const auto* sprite = core_.registry.try_get<SpriteComponent>(state_.selected);
        if (!tf || !sprite) return;

        const SpriteBounds b = GetSpriteBounds(*tf, *sprite, kPixelsPerUnit);
        Vector2 c[4];
        SpriteCorners(b, c);

        const float thick = 2.0f / zoom;   // two screen pixels at any zoom
        for (int i = 0; i < 4; ++i)
            DrawLineEx(c[i], c[(i + 1) % 4], thick, YELLOW);
        DrawCircleV(b.position, 4.0f / zoom, YELLOW);
    }

    MirageCoreRuntime& core_;
    EditorState&       state_;
    SpriteRenderer&    sprites_;

    bool    centred_     = false;
    bool    pressInView_ = false;
    Vector2 pressScreen_ = {0, 0};
};
