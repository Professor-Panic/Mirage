#pragma once
#include <algorithm>
#include <cmath>
#include "raylib.h"
#include "raymath.h"
#include "rui.h"
#include "mirage_core/component_systems.h"   // SpriteBounds, GetSpriteBounds, SpriteCorners, HitSprite
#include "mirage_core/components.h"
#include "mirage_core/constants.h"           // kPixelsPerUnit, kArrowHandleSize (screen pixels)
#include "mirage_core/runtime.h"
#include "mirage_core/sprite_renderer.h"
#include "mirage_editor/editor_state.h"
#include "mirage_editor/undo_stack.h"
#include <css_emulator.h>

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
// Picking, the selection outline, the move/rotate gizmo and undo live here.
//
// Gizmo (drawn at the selected sprite's pivot, world-aligned):
//   red arrow   -> move along X          green arrow -> move along Y (+Y is down)
//   centre box  -> free move             ring        -> rotate about the pivot
//   dragging inside the sprite also moves it freely
class ViewportTools
{
public:
    ViewportTools(rui::Context& ui, MirageCoreRuntime& core, EditorState& state, SpriteRenderer& sprites)
        : core_(core), state_(state), sprites_(sprites), ui_(ui)
    {
        ui.runtime.Function("sceneViewUpdate", [this](const rui::Event& e) { OnUpdate(e); });
        ui.runtime.Function("sceneViewDraw",   [this](const rui::Event& e) { OnDraw(e); });
    }

    ViewportTools(const ViewportTools&) = delete;
    ViewportTools& operator=(const ViewportTools&) = delete;

    // Call when a scene loads: the commands refer to the old scene's uuids.
    void ResetHistory() { undo_.Clear(); }

private:
    enum class Drag   { Idle, Pending, Dragging };
    enum class Handle { None, Free, MoveX, MoveY, Rotate };

    // All gizmo sizes, derived from kArrowHandleSize (screen pixels) and the
    // zoom, so the gizmo keeps the same on-screen size at any zoom.
    struct Metrics
    {
        float u;          // one screen pixel, in world pixels
        float s;          // kArrowHandleSize, in world pixels
        float square;     // half-size of the free-move box
        float shaft;      // arrow shaft length from the pivot
        float headLen;    // arrow head length
        float headHalf;   // arrow head half-width
        float ring;       // rotation ring radius
        float tol;        // grab tolerance around lines
        float thick;      // line thickness

        explicit Metrics(float zoom)
        {
            u        = 1.0f / zoom;
            s        = static_cast<float>(kArrowHandleSize) * u;
            square   = 1.5f * s;
            shaft    = 10.0f * s;
            headLen  = 2.0f * s;
            headHalf = s;
            ring     = 14.0f * s;
            tol      = s;
            thick    = 2.0f * u;
        }
    };

    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------
    static TransformSnapshot Snap(const TransformComponent& tf)
    {
        return { tf.position, tf.scale, tf.rotation };
    }

    static void Apply(TransformComponent& tf, const TransformSnapshot& s)
    {
        tf.position = s.position;
        tf.scale    = s.scale;
        tf.rotation = s.rotation;
    }

    static bool Differs(const TransformSnapshot& a, const TransformSnapshot& b)
    {
        return a.position.x != b.position.x || a.position.y != b.position.y ||
               a.rotation.x != b.rotation.x || a.rotation.y != b.rotation.y ||
               a.rotation.z != b.rotation.z || a.rotation.w != b.rotation.w;
    }

    static float DistToSegment(Vector2 p, Vector2 a, Vector2 b)
    {
        const Vector2 ab = { b.x - a.x, b.y - a.y };
        const float len2 = ab.x * ab.x + ab.y * ab.y;
        float t = len2 > 0.0f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2 : 0.0f;
        t = Clamp(t, 0.0f, 1.0f);
        const Vector2 q = { a.x + ab.x * t, a.y + ab.y * t };
        return Vector2Distance(p, q);
    }

    // Bounds of the selected sprite, if there is one with a transform + sprite.
    bool SelectedBounds(SpriteBounds& out) const
    {
        if (!core_.registry.valid(state_.selected)) return false;
        const auto* tf = core_.registry.try_get<TransformComponent>(state_.selected);
        const auto* sp = core_.registry.try_get<SpriteComponent>(state_.selected);
        if (!tf || !sp) return false;
        out = GetSpriteBounds(*tf, *sp, kPixelsPerUnit);
        return true;
    }

    // Which gizmo handle is under `m` (world pixels). Priority: centre box,
    // arrows, then the ring. The sprite body is not a handle (see OnUpdate).
    Handle PickHandle(Vector2 p, Vector2 m, float zoom) const
    {
        const Metrics k(zoom);

        if (std::fabs(m.x - p.x) <= k.square && std::fabs(m.y - p.y) <= k.square)
            return Handle::Free;

        const float reach = k.shaft + k.headLen;
        const float grab  = std::max(k.tol, k.headHalf);

        if (DistToSegment(m, { p.x + k.square, p.y }, { p.x + reach, p.y }) <= grab) return Handle::MoveX;
        if (DistToSegment(m, { p.x, p.y + k.square }, { p.x, p.y + reach }) <= grab) return Handle::MoveY;

        if (std::fabs(Vector2Distance(m, p) - k.ring) <= k.tol) return Handle::Rotate;

        return Handle::None;
    }

    // ------------------------------------------------------------------
    // Update: hover, press, drag, release, undo
    // ------------------------------------------------------------------
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

        hover_ = Handle::None;
        if (state_.mode != Mode::Edit) return;

        const Vector2 mouseWorld = view->world_mouse;
        const float   zoom       = view->camera.zoom;

        // --- hover (for highlighting) ---
        SpriteBounds sel;
        if (drag_ == Drag::Idle && view->hovered && SelectedBounds(sel))
            hover_ = PickHandle(sel.position, mouseWorld, zoom);

        // --- press ---
        if (view->hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            pressScreen_ = GetMousePosition();
            pressWorld_  = mouseWorld;
            handle_      = Handle::None;

            if (SelectedBounds(sel))
            {
                Handle h = PickHandle(sel.position, mouseWorld, zoom);
                if (h == Handle::None && HitSprite(sel, mouseWorld))
                    h = Handle::Free;                         // drag the body

                if (h != Handle::None)
                {
                    drag_       = Drag::Pending;              // might become a drag
                    handle_     = h;
                    dragEntity_ = state_.selected;
                    startTf_    = Snap(core_.registry.get<TransformComponent>(dragEntity_));
                    pivot_      = sel.position;
                    startAngleDeg_ = sel.angle;
                    grabAngle_  = std::atan2(mouseWorld.y - pivot_.y, mouseWorld.x - pivot_.x);
                }
            }
            if (drag_ == Drag::Idle) pendingPick_ = true;     // plain click: select on release
        }

        // --- Pending -> Dragging once the mouse has moved ---
        if (drag_ == Drag::Pending &&
            Vector2Distance(GetMousePosition(), pressScreen_) >= 4.0f)
            drag_ = Drag::Dragging;

        // --- dragging: always computed from the START values, never from per-frame deltas ---
        if (drag_ == Drag::Dragging && core_.registry.valid(dragEntity_))
        {
            auto& tf = core_.registry.get<TransformComponent>(dragEntity_);

            if (handle_ == Handle::Rotate)
            {
                const float now = std::atan2(mouseWorld.y - pivot_.y, mouseWorld.x - pivot_.x);
                float deg = startAngleDeg_ + (now - grabAngle_) * RAD2DEG;
                if (IsKeyDown(KEY_LEFT_CONTROL)) deg = std::round(deg / 15.0f) * 15.0f;   // snap to 15 degrees
                tf.rotation = QuaternionFromAxisAngle({ 0.0f, 0.0f, 1.0f }, deg * DEG2RAD);
            }
            else
            {
                Vector2 d = { (mouseWorld.x - pressWorld_.x) / kPixelsPerUnit,
                              (mouseWorld.y - pressWorld_.y) / kPixelsPerUnit };
                if (handle_ == Handle::MoveX) d.y = 0.0f;
                if (handle_ == Handle::MoveY) d.x = 0.0f;

                if (IsKeyDown(KEY_LEFT_CONTROL))                  // snap to half units
                {
                    const float snap = 0.5f;
                    if (handle_ != Handle::MoveY) d.x = std::round((startTf_.position.x + d.x) / snap) * snap - startTf_.position.x;
                    if (handle_ != Handle::MoveX) d.y = std::round((startTf_.position.y + d.y) / snap) * snap - startTf_.position.y;
                }
                tf.position = { startTf_.position.x + d.x, startTf_.position.y + d.y, startTf_.position.z };
            }
            ui_.runtime.Invalidate("Entity_Fields");              // inspector follows live

            if (IsKeyPressed(KEY_ESCAPE))                         // cancel
            {
                Apply(tf, startTf_);
                drag_ = Drag::Idle;
                pendingPick_ = false;
                dragEntity_ = entt::null;
                handle_ = Handle::None;
            }
        }

        // --- release ---
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            if (drag_ == Drag::Dragging && core_.registry.valid(dragEntity_))
            {
                const TransformSnapshot after = Snap(core_.registry.get<TransformComponent>(dragEntity_));
                if (Differs(startTf_, after))
                    undo_.Push({ core_.entityBus.IdOf(dragEntity_), startTf_, after });   // one entry per drag
            }
            else if (drag_ == Drag::Pending || pendingPick_)      // it was a click
            {
                // A click on a gizmo handle without moving keeps the selection.
                if (handle_ == Handle::None || handle_ == Handle::Free)
                    state_.selected = sprites_.Pick(core_.registry, mouseWorld, kPixelsPerUnit);
            }
            drag_ = Drag::Idle;
            pendingPick_ = false;
            dragEntity_ = entt::null;
            handle_ = Handle::None;
        }

        // --- undo / redo ---
        if (drag_ == Drag::Idle && IsKeyDown(KEY_LEFT_CONTROL))
        {
            if (IsKeyPressed(KEY_Z) && undo_.CanUndo()) ApplyCommand(undo_.PopUndo(), false);
            if (IsKeyPressed(KEY_Y) && undo_.CanRedo()) ApplyCommand(undo_.PopRedo(), true);
        }
    }

    void ApplyCommand(const TransformCommand& c, bool useAfter)
    {
        const entt::entity e = core_.entityBus.Resolve(EntityRef{c.id});
        if (e == entt::null) return;                  // deleted since: skip silently
        if (auto* tf = core_.registry.try_get<TransformComponent>(e))
            Apply(*tf, useAfter ? c.after : c.before);
        state_.selected = e;
        ui_.runtime.Invalidate("Entity_Fields");
    }

    // ------------------------------------------------------------------
    // Draw
    // ------------------------------------------------------------------
    void OnDraw(const rui::Event& e)
    {
        const auto* view = static_cast<const rui::Viewport*>(e.native_widget);
        const float zoom = view ? view->camera.zoom : 1.0f;

        // No ClearBackground here: that would wipe the whole window. The panel's
        // background comes from the viewport's RCSS (background-color).
        DrawRectangleLinesEx({0, 0, 1600, 900}, 2.0f / zoom, WHITE);
        sprites_.Draw(core_.registry, kPixelsPerUnit, kPixelsPerUnit);

        DrawSelection(zoom);
        if (state_.mode == Mode::Edit)
            DrawGizmo(zoom, view ? view->world_mouse : Vector2{0, 0});
    }

    // Outline of the selected sprite plus a pivot marker.
    void DrawSelection(float zoom)
    {
        SpriteBounds b;
        if (!SelectedBounds(b)) return;

        Vector2 c[4];
        SpriteCorners(b, c);

        const float thick = 2.0f / zoom;   // two screen pixels at any zoom
        for (int i = 0; i < 4; ++i)
            DrawLineEx(c[i], c[(i + 1) % 4], thick, YELLOW);
        DrawCircleV(b.position, 4.0f / zoom, YELLOW);
    }

    // DrawTriangle only shows one winding (raylib culls back faces), so draw
    // both orders; one of them is always visible.
    static void DrawTri(Vector2 a, Vector2 b, Vector2 c, Color col)
    {
        DrawTriangle(a, b, c, col);
        DrawTriangle(a, c, b, col);
    }

    void DrawGizmo(float zoom, Vector2 mouseWorld)
    {
        SpriteBounds b;
        if (!SelectedBounds(b)) return;

        const Metrics k(zoom);
        const Vector2 p = b.position;
        const Handle  lit = (drag_ == Drag::Dragging) ? handle_ : hover_;   // highlighted handle

        const Color red   = { 230,  70,  70, 255 };
        const Color green = {  80, 200,  90, 255 };
        const Color blue  = {  90, 160, 255, 255 };
        const Color hot   = YELLOW;

        const Color cx = (lit == Handle::MoveX)  ? hot : red;
        const Color cy = (lit == Handle::MoveY)  ? hot : green;
        const Color cr = (lit == Handle::Rotate) ? hot : blue;

        // Rotation ring (drawn first so the arrows sit on top).
        DrawRing(p, k.ring - k.thick * 0.5f, k.ring + k.thick * 0.5f, 0.0f, 360.0f, 64, cr);

        // X arrow (red).
        DrawLineEx({ p.x + k.square, p.y }, { p.x + k.shaft, p.y }, k.thick, cx);
        DrawTri({ p.x + k.shaft + k.headLen, p.y },
                { p.x + k.shaft, p.y - k.headHalf },
                { p.x + k.shaft, p.y + k.headHalf }, cx);

        // Y arrow (green). +Y is down on screen, like the position values.
        DrawLineEx({ p.x, p.y + k.square }, { p.x, p.y + k.shaft }, k.thick, cy);
        DrawTri({ p.x, p.y + k.shaft + k.headLen },
                { p.x - k.headHalf, p.y + k.shaft },
                { p.x + k.headHalf, p.y + k.shaft }, cy);

        // Free-move box at the pivot.
        const Color box = (lit == Handle::Free) ? hot : Fade(WHITE, 0.6f);
        DrawRectangleV({ p.x - k.square, p.y - k.square }, { k.square * 2.0f, k.square * 2.0f }, Fade(box, 0.35f));
        DrawRectangleLinesEx({ p.x - k.square, p.y - k.square, k.square * 2.0f, k.square * 2.0f }, k.thick * 0.75f, box);

        // While rotating: a line from the pivot to the mouse.
        if (drag_ == Drag::Dragging && handle_ == Handle::Rotate)
            DrawLineEx(p, mouseWorld, k.thick * 0.5f, Fade(WHITE, 0.7f));
    }

    // ------------------------------------------------------------------
    // Members (declared in the order the constructor initialises them)
    // ------------------------------------------------------------------
    MirageCoreRuntime& core_;
    EditorState&       state_;
    SpriteRenderer&    sprites_;
    rui::Context&      ui_;

    bool    centred_     = false;
    Vector2 pressScreen_ = {0, 0};
    Vector2 pressWorld_  = {0, 0};

    Drag         drag_        = Drag::Idle;
    Handle       handle_      = Handle::None;   // handle grabbed by the current press
    Handle       hover_       = Handle::None;   // handle under the mouse (for highlighting)
    bool         pendingPick_ = false;          // press landed off the selection: a click selects on release
    entt::entity dragEntity_  = entt::null;
    TransformSnapshot startTf_{};               // the transform at press
    Vector2      pivot_       = {0, 0};         // rotation centre, world pixels
    float        startAngleDeg_ = 0.0f;
    float        grabAngle_   = 0.0f;           // mouse angle about the pivot at press, radians

    UndoStack undo_;
};