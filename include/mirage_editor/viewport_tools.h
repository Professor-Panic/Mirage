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
#include "mirage_editor/editor_state.h"      // Tool, Mode, EditorState
#include "mirage_editor/undo_stack.h"
#include "mirage_core/physics_debug.h"
#include <css_emulator.h>

// ---------------------------------------------------------------------------
// Pure gizmo geometry: no raylib calls and no state, so it can be unit tested.
// ---------------------------------------------------------------------------
namespace gizmo_math
{
    // The sprite's own X and Y axes in world pixels, from its Z angle in degrees
    // (+Y is down, so a positive angle turns clockwise on screen).
    struct Axes { Vector2 x; Vector2 y; };

    inline Axes AxesFromAngle(float degrees)
    {
        const float r = degrees * DEG2RAD;
        return { { std::cos(r), std::sin(r) }, { -std::sin(r), std::cos(r) } };
    }

    inline float Dot2(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }

    inline float WrapRadians(float a)
    {
        while (a >  PI) a -= 2.0f * PI;
        while (a <= -PI) a += 2.0f * PI;
        return a;
    }

    inline float WrapDegrees(float d)
    {
        while (d >  180.0f) d -= 360.0f;
        while (d <= -180.0f) d += 360.0f;
        return d;
    }

    inline float SnapStep(float value, float step) { return std::round(value / step) * step; }

    inline float DistToSegment(Vector2 p, Vector2 a, Vector2 b)
    {
        const Vector2 ab = { b.x - a.x, b.y - a.y };
        const float len2 = ab.x * ab.x + ab.y * ab.y;
        float t = len2 > 0.0f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2 : 0.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        const Vector2 q = { a.x + ab.x * t, a.y + ab.y * t };
        return std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y));
    }

    // ---- Rotation rings --------------------------------------------------
    // Three great circles of a sphere around the pivot, one per LOCAL axis, turned
    // by the sprite's full rotation and then projected straight down the view axis
    // (drop z). That is what Unity's rotate gizmo is: a sphere of rings that follows
    // the object's rotation. A ring that faces the view is a circle, one seen edge-on
    // is a line, anything between is an ellipse, so the shape is a result of the
    // rotation and not a fixed tilt.
    //
    // Ring i (0 = X, 1 = Y, 2 = Z) lies in the plane perpendicular to local axis i.
    // n is that axis in world space, u and v span the plane with u x v = n, and the
    // ring is  centre + R * (cos t * u + sin t * v).  Increasing t is a positive
    // right-handed rotation about n.
    struct RingBasis { Vector3 n, u, v; };

    inline RingBasis RingBasisFor(int i, Quaternion q)
    {
        const Vector3 X = { 1, 0, 0 }, Y = { 0, 1, 0 }, Z = { 0, 0, 1 };
        Vector3 a, u, v;
        if      (i == 0) { a = X; u = Y; v = Z; }
        else if (i == 1) { a = Y; u = Z; v = X; }
        else             { a = Z; u = X; v = Y; }
        return { Vector3RotateByQuaternion(a, q),
                 Vector3RotateByQuaternion(u, q),
                 Vector3RotateByQuaternion(v, q) };
    }

    // Point on the ring at parameter t, relative to the pivot (x, y = screen, z = depth).
    inline Vector3 RingPoint3(const RingBasis& b, float R, float t)
    {
        const float c = std::cos(t) * R, s = std::sin(t) * R;
        return { b.u.x * c + b.v.x * s, b.u.y * c + b.v.y * s, b.u.z * c + b.v.z * s };
    }

    // Distance on screen from d (mouse relative to the pivot) to the projected ring.
    // Optionally returns the ring parameter of the closest point.
    inline float DistToRing(Vector2 d, const RingBasis& b, float R, float* outT = nullptr)
    {
        constexpr int kSeg = 96;
        float best = 1e30f, bestT = 0.0f;
        Vector3 last3 = RingPoint3(b, R, 0.0f);
        for (int i = 1; i <= kSeg; ++i)
        {
            const float t0 = 2.0f * PI * static_cast<float>(i - 1) / kSeg;
            const float t1 = 2.0f * PI * static_cast<float>(i) / kSeg;
            const Vector3 p3 = RingPoint3(b, R, t1);
            const Vector2 a = { last3.x, last3.y }, c = { p3.x, p3.y };
            const Vector2 ac = { c.x - a.x, c.y - a.y };
            const float len2 = ac.x * ac.x + ac.y * ac.y;
            float f = len2 > 0.0f ? ((d.x - a.x) * ac.x + (d.y - a.y) * ac.y) / len2 : 0.0f;
            f = std::clamp(f, 0.0f, 1.0f);
            const Vector2 q = { a.x + ac.x * f, a.y + ac.y * f };
            const float dist = std::sqrt((d.x - q.x) * (d.x - q.x) + (d.y - q.y) * (d.y - q.y));
            if (dist < best) { best = dist; bestT = t0 + (t1 - t0) * f; }
            last3 = p3;
        }
        if (outT) *outT = bestT;
        return best;
    }

    // Direction (unit, screen) a point on the ring moves when t increases. Zero length
    // if the ring is seen edge-on exactly at that point (the end of the line).
    inline Vector2 RingTangent2(const RingBasis& b, float R, float t)
    {
        const float c = std::cos(t), s = std::sin(t);
        const Vector2 d = { (-s * b.u.x + c * b.v.x) * R, (-s * b.u.y + c * b.v.y) * R };
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        return len > 1e-4f * R ? Vector2{ d.x / len, d.y / len } : Vector2{ 0.0f, 0.0f };
    }

    // Angle of the mouse on the ring, in the ring's own plane: the view ray through
    // the mouse is intersected with the plane through the pivot. Only usable while the
    // ring faces the view well enough (|n.z| not near 0).
    inline float PlaneAngle(Vector2 d, const RingBasis& b)
    {
        const float z = -(b.n.x * d.x + b.n.y * d.y) / b.n.z;
        const Vector3 p = { d.x, d.y, z };
        return std::atan2(p.x * b.v.x + p.y * b.v.y + p.z * b.v.z,
                          p.x * b.u.x + p.y * b.u.y + p.z * b.u.z);
    }

    // Snap the distance moved along `axis` (all in world units). If the axis lines
    // up with the world X or Y the ABSOLUTE position lands on the grid (like the old
    // gizmo); on a rotated axis the distance moved is snapped instead.
    inline float SnapAlongAxis(Vector2 start, Vector2 axis, float along, float step)
    {
        if (std::fabs(axis.y) < 1e-4f && std::fabs(axis.x) > 0.5f)
        {
            const float target = SnapStep(start.x + axis.x * along, step);
            return (target - start.x) / axis.x;
        }
        if (std::fabs(axis.x) < 1e-4f && std::fabs(axis.y) > 0.5f)
        {
            const float target = SnapStep(start.y + axis.y * along, step);
            return (target - start.y) / axis.y;
        }
        return SnapStep(along, step);
    }

    // Scale factor for dragging an axis handle: how far the mouse is from the pivot
    // along the axis now, relative to where the drag began. Never below 0.01, so a
    // sprite is not flipped inside out or collapsed to nothing.
    inline float AxisScaleFactor(float pressDist, float nowDist, float minAbs)
    {
        float p = pressDist;
        if (std::fabs(p) < minAbs) p = p < 0.0f ? -minAbs : minAbs;
        return std::max(nowDist / p, 0.01f);
    }
}

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
// Picking, the selection outline, the gizmos and undo live here.
//
// Tools (EditorState::tool, switched with W / E / R while the UI does not want
// the keyboard, e.g. while no text field is being edited). Only the current
// tool's gizmo is drawn. It sits at the selected sprite's pivot and follows the
// sprite's rotation, so the axes are the sprite's own.
//
//   W  Move    red arrow -> along the sprite's X     green arrow -> along its Y
//              centre box -> free move (dragging inside the sprite also moves it)
//   E  Rotate  a sphere of three rings, one per LOCAL axis (red X, green Y, blue Z),
//              turned by the sprite's rotation and projected down the view axis, so
//              each ring is a circle, an ellipse or a line depending on the rotation.
//              Dragging a ring turns the sprite about that local axis.
//   R  Scale   red / green square handles -> scale along X / Y
//              centre box -> uniform (drag right or up to grow)
//
// Hold Ctrl while dragging to snap (0.5 units, 15 degrees, 0.1 scale).
// Escape cancels a drag. Ctrl+Z / Ctrl+Y undo and redo.
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
    enum class Handle { None, Free, MoveX, MoveY, RotateX, RotateY, RotateZ, ScaleX, ScaleY, ScaleAll };

    static constexpr float kMoveSnap   = 0.5f;     // world units
    static constexpr float kRotateSnap = 15.0f;    // degrees
    static constexpr float kScaleSnap  = 0.1f;
    static constexpr float kRingFacing = 0.35f;    // |n.z| above this: drag by angle in the ring's plane, else along its tangent
    static constexpr float kNearSign   = 1.0f;     // +1: positive z is towards the viewer (flip to -1 if front and back look swapped)
    static constexpr float kUniformScalePixels = 200.0f;   // screen pixels of drag that double the scale

    // All gizmo sizes, derived from kArrowHandleSize (screen pixels) and the
    // zoom, so the gizmo keeps the same on-screen size at any zoom.
    struct Metrics
    {
        float u;          // one screen pixel, in world pixels
        float s;          // kArrowHandleSize, in world pixels
        float square;     // half-size of the centre box
        float shaft;      // arrow shaft length from the pivot
        float headLen;    // arrow head length
        float headHalf;   // arrow head half-width
        float cube;       // half-size of the square on a scale handle
        float ring;       // radius of the rotation sphere
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
            cube     = 1.2f * s;
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
        return { tf.position, tf.scale, tf.rotation, tf.eulerHint };
    }

    static void Apply(TransformComponent& tf, const TransformSnapshot& s)
    {
        tf.position  = s.position;
        tf.scale     = s.scale;
        tf.rotation  = s.rotation;
        tf.eulerHint = s.euler;
    }

    static bool Differs(const TransformSnapshot& a, const TransformSnapshot& b)
    {
        return a.position.x != b.position.x || a.position.y != b.position.y || a.position.z != b.position.z ||
               a.scale.x    != b.scale.x    || a.scale.y    != b.scale.y    || a.scale.z    != b.scale.z    ||
               a.rotation.x != b.rotation.x || a.rotation.y != b.rotation.y ||
               a.rotation.z != b.rotation.z || a.rotation.w != b.rotation.w;
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

    // W / E / R pick the tool. Ignored while the UI wants the keyboard (a text
    // field is being edited, a picker or combo box is open), during a drag, and
    // with Ctrl / Alt held (those belong to other shortcuts).
    void HandleToolKeys()
    {
        if (state_.mode != Mode::Edit || drag_ != Drag::Idle) return;
        if (ui_.WantsKeyboard()) return;
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
            IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT)) return;

        if (IsKeyPressed(KEY_W)) state_.tool = Tool::Move;
        if (IsKeyPressed(KEY_E)) state_.tool = Tool::Rotate;
        if (IsKeyPressed(KEY_R)) state_.tool = Tool::Scale;
    }

    // Which gizmo handle of `tool` is under `m` (world pixels). `p` is the pivot and
    // `ax` the sprite's axes. The sprite body is not a handle (see OnUpdate).
    Handle PickHandle(Tool tool, Vector2 p, const gizmo_math::Axes& ax, Quaternion rot, Vector2 m, float zoom) const
    {
        using namespace gizmo_math;
        const Metrics k(zoom);
        const Vector2 d  = { m.x - p.x, m.y - p.y };
        const float   lx = Dot2(d, ax.x);        // the mouse in the sprite's own axes
        const float   ly = Dot2(d, ax.y);

        if (tool == Tool::Rotate)
        {
            // The ring closest to the mouse, if any is within the tolerance.
            Handle hit = Handle::None;
            float best = k.tol;
            const Handle which[3] = { Handle::RotateX, Handle::RotateY, Handle::RotateZ };
            for (int r = 0; r < 3; ++r)
            {
                const float dr = DistToRing(d, RingBasisFor(r, rot), k.ring);
                if (dr < best) { best = dr; hit = which[r]; }
            }

            return hit;
        }

        const bool scale = (tool == Tool::Scale);

        if (std::fabs(lx) <= k.square && std::fabs(ly) <= k.square)
            return scale ? Handle::ScaleAll : Handle::Free;

        const float reach = scale ? k.shaft + k.cube : k.shaft + k.headLen;
        const float grab  = std::max(k.tol, scale ? k.cube : k.headHalf);

        const Vector2 xa = { p.x + ax.x.x * k.square, p.y + ax.x.y * k.square };
        const Vector2 xb = { p.x + ax.x.x * reach,    p.y + ax.x.y * reach };
        if (DistToSegment(m, xa, xb) <= grab) return scale ? Handle::ScaleX : Handle::MoveX;

        const Vector2 ya = { p.x + ax.y.x * k.square, p.y + ax.y.y * k.square };
        const Vector2 yb = { p.x + ax.y.x * reach,    p.y + ax.y.y * reach };
        if (DistToSegment(m, ya, yb) <= grab) return scale ? Handle::ScaleY : Handle::MoveY;

        return Handle::None;
    }

    static bool IsRotateHandle(Handle h)
    {
        return h == Handle::RotateX || h == Handle::RotateY || h == Handle::RotateZ;
    }

    // ------------------------------------------------------------------
    // Update: tool keys, hover, press, drag, release, undo
    // ------------------------------------------------------------------
    void OnUpdate(const rui::Event& e)
    {
        auto* view = static_cast<rui::Viewport*>(e.native_widget);
        if (!view) return;

        HandleToolKeys();

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
            hover_ = PickHandle(state_.tool, sel.position, gizmo_math::AxesFromAngle(sel.angle), sel.rotation, mouseWorld, zoom);

        // --- press ---
        if (view->hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            pressScreen_ = GetMousePosition();
            pressWorld_  = mouseWorld;
            handle_      = Handle::None;

            if (SelectedBounds(sel))
            {
                const gizmo_math::Axes axes = gizmo_math::AxesFromAngle(sel.angle);
                Handle h = PickHandle(state_.tool, sel.position, axes, sel.rotation, mouseWorld, zoom);
                if (state_.tool == Tool::Move && h == Handle::None && HitSprite(sel, mouseWorld))
                    h = Handle::Free;                         // drag the body

                if (h != Handle::None)
                    BeginDrag(h, sel, axes, zoom);
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
            ApplyDrag(mouseWorld);
            ui_.runtime.Invalidate("Entity_Fields");              // inspector follows live

            if (IsKeyPressed(KEY_ESCAPE))                         // cancel
            {
                Apply(core_.registry.get<TransformComponent>(dragEntity_), startTf_);
                EndDrag();
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
            EndDrag();
        }

        // --- undo / redo ---
        if (drag_ == Drag::Idle && IsKeyDown(KEY_LEFT_CONTROL))
        {
            if (IsKeyPressed(KEY_Z) && undo_.CanUndo()) ApplyCommand(undo_.PopUndo(), false);
            if (IsKeyPressed(KEY_Y) && undo_.CanRedo()) ApplyCommand(undo_.PopRedo(), true);
        }
    }

    // Remember everything a drag needs at the moment of the press.
    void BeginDrag(Handle h, const SpriteBounds& sel, const gizmo_math::Axes& axes, float zoom)
    {
        using namespace gizmo_math;
        const Metrics k(zoom);

        drag_       = Drag::Pending;                              // might become a drag
        handle_     = h;
        dragEntity_ = state_.selected;

        const auto& tf = core_.registry.get<TransformComponent>(dragEntity_);
        startTf_    = Snap(tf);
        startEuler_ = tf.GetEulerAngles();
        pivot_      = sel.position;
        axes_       = axes;                                       // frozen for the whole drag
        axisMinDist_ = k.square;

        const Vector2 fromPivot = { pressWorld_.x - pivot_.x, pressWorld_.y - pivot_.y };

        if (IsRotateHandle(h))
        {
            ringIndex_ = (h == Handle::RotateX) ? 0 : (h == Handle::RotateY) ? 1 : 2;
            ringBasis_ = RingBasisFor(ringIndex_, tf.rotation);   // frozen for the whole drag
            ringR_     = k.ring;
            accum_     = 0.0f;                                    // total angle turned, continuous across +-180

            usePlane_ = std::fabs(ringBasis_.n.z) > kRingFacing;
            if (usePlane_)
                lastPhi_ = PlaneAngle(fromPivot, ringBasis_);
            else
            {
                // Ring seen (nearly) edge-on: its plane is not a good target for the mouse,
                // so the drag follows the ring's direction on screen at the grab point.
                float t = 0.0f;
                DistToRing(fromPivot, ringBasis_, ringR_, &t);
                tangent_ = RingTangent2(ringBasis_, ringR_, t);
                if (tangent_.x == 0.0f && tangent_.y == 0.0f)    // exactly at the end of the line
                    tangent_ = RingTangent2(ringBasis_, ringR_, t + 0.2f);
            }
        }
        if (h == Handle::ScaleX) axisPressDist_ = Dot2(fromPivot, axes_.x);
        if (h == Handle::ScaleY) axisPressDist_ = Dot2(fromPivot, axes_.y);
    }

    void EndDrag()
    {
        drag_        = Drag::Idle;
        pendingPick_ = false;
        dragEntity_  = entt::null;
        handle_      = Handle::None;
    }

    void ApplyDrag(Vector2 mouseWorld)
    {
        using namespace gizmo_math;
        auto& tf = core_.registry.get<TransformComponent>(dragEntity_);
        const bool snap = IsKeyDown(KEY_LEFT_CONTROL);
        const Vector2 fromPivot = { mouseWorld.x - pivot_.x, mouseWorld.y - pivot_.y };

        switch (handle_)
        {
            case Handle::Free:
            case Handle::MoveX:
            case Handle::MoveY:
            {
                const Vector2 delta = { (mouseWorld.x - pressWorld_.x) / kPixelsPerUnit,
                                        (mouseWorld.y - pressWorld_.y) / kPixelsPerUnit };
                const Vector2 start = { startTf_.position.x, startTf_.position.y };
                Vector2 d;

                if (handle_ == Handle::Free)
                {
                    d = delta;
                    if (snap)
                    {
                        d.x = SnapStep(start.x + d.x, kMoveSnap) - start.x;
                        d.y = SnapStep(start.y + d.y, kMoveSnap) - start.y;
                    }
                }
                else
                {
                    // Along the sprite's own axis, however it is rotated.
                    const Vector2 axis = (handle_ == Handle::MoveX) ? axes_.x : axes_.y;
                    float along = Dot2(delta, axis);
                    if (snap) along = SnapAlongAxis(start, axis, along, kMoveSnap);
                    d = { axis.x * along, axis.y * along };
                }
                tf.position = { start.x + d.x, start.y + d.y, startTf_.position.z };
                break;
            }

            case Handle::RotateX:
            case Handle::RotateY:
            case Handle::RotateZ:
            {
                // Turn about the ring's axis, which is the sprite's LOCAL axis (so the
                // rotation composes like Unity's local-space rotate tool).
                if (usePlane_)
                {
                    const float phi = PlaneAngle(fromPivot, ringBasis_);
                    accum_ += WrapRadians(phi - lastPhi_);
                    lastPhi_ = phi;
                }
                else
                {
                    const Vector2 moved = { mouseWorld.x - pressWorld_.x, mouseWorld.y - pressWorld_.y };
                    accum_ = Dot2(moved, tangent_) / ringR_;      // one radius of travel = one radian
                }

                float degrees = accum_ * RAD2DEG;
                if (snap) degrees = SnapStep(degrees, kRotateSnap);   // snaps the amount turned

                const Quaternion turn = QuaternionFromAxisAngle(ringBasis_.n, degrees * DEG2RAD);
                tf.rotation  = QuaternionNormalize(QuaternionMultiply(turn, startTf_.rotation));
                tf.eulerHint = rotation::ToEulerDegrees(tf.rotation);
                break;
            }

            case Handle::ScaleX:
            case Handle::ScaleY:
            {
                const Vector2 axis = (handle_ == Handle::ScaleX) ? axes_.x : axes_.y;
                const float factor = AxisScaleFactor(axisPressDist_, Dot2(fromPivot, axis), axisMinDist_);

                float value = ((handle_ == Handle::ScaleX) ? startTf_.scale.x : startTf_.scale.y) * factor;
                if (snap) value = std::max(SnapStep(value, kScaleSnap), kScaleSnap);

                if (handle_ == Handle::ScaleX) tf.scale.x = value;
                else                           tf.scale.y = value;
                break;
            }

            case Handle::ScaleAll:
            {
                const Vector2 mouse = GetMousePosition();
                const float pixels = (mouse.x - pressScreen_.x) - (mouse.y - pressScreen_.y);   // right / up = bigger
                const float factor = std::max(1.0f + pixels / kUniformScalePixels, 0.01f);

                Vector3 s = { startTf_.scale.x * factor, startTf_.scale.y * factor, startTf_.scale.z * factor };
                if (snap)
                {
                    s.x = std::max(SnapStep(s.x, kScaleSnap), kScaleSnap);
                    s.y = std::max(SnapStep(s.y, kScaleSnap), kScaleSnap);
                }
                tf.scale = s;
                break;
            }

            case Handle::None:
                break;
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
        DrawRectangleLinesEx({0, 0, 1600, 900}, 2.0f / zoom, WHITE);
        sprites_.Draw(core_.registry, kPixelsPerUnit, kPixelsPerUnit);
        physics_debug::HandleToggle();
        physics_debug::DrawColliders(core_.registry, kPixelsPerUnit, 1.0f / zoom);
        //DrawSelection(zoom);
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

    // A square centred on `c`, turned to the sprite's axes.
    static void DrawBox(Vector2 c, const gizmo_math::Axes& ax, float half, Color fill, Color line, float thick)
    {
        const auto at = [&](float sx, float sy) -> Vector2
        {
            return { c.x + (ax.x.x * sx + ax.y.x * sy) * half,
                     c.y + (ax.x.y * sx + ax.y.y * sy) * half };
        };
        const Vector2 p0 = at(-1, -1), p1 = at(1, -1), p2 = at(1, 1), p3 = at(-1, 1);

        DrawTri(p0, p1, p2, fill);
        DrawTri(p0, p2, p3, fill);
        DrawLineEx(p0, p1, thick, line);
        DrawLineEx(p1, p2, thick, line);
        DrawLineEx(p2, p3, thick, line);
        DrawLineEx(p3, p0, thick, line);
    }

    // Shaft from the centre box to the head, and the arrow head.
    static void DrawArrow(Vector2 p, Vector2 dir, Vector2 perp, const Metrics& k, Color col)
    {
        const Vector2 a   = { p.x + dir.x * k.square, p.y + dir.y * k.square };
        const Vector2 b   = { p.x + dir.x * k.shaft,  p.y + dir.y * k.shaft };
        const Vector2 tip = { p.x + dir.x * (k.shaft + k.headLen), p.y + dir.y * (k.shaft + k.headLen) };

        DrawLineEx(a, b, k.thick, col);
        DrawTri(tip,
                { b.x + perp.x * k.headHalf, b.y + perp.y * k.headHalf },
                { b.x - perp.x * k.headHalf, b.y - perp.y * k.headHalf }, col);
    }

    // Shaft with a square at the end (the scale handle).
    static void DrawCubeHandle(Vector2 p, Vector2 dir, const gizmo_math::Axes& ax, const Metrics& k, Color col)
    {
        const Vector2 a = { p.x + dir.x * k.square, p.y + dir.y * k.square };
        const Vector2 b = { p.x + dir.x * k.shaft,  p.y + dir.y * k.shaft };

        DrawLineEx(a, b, k.thick, col);
        DrawBox(b, ax, k.cube, col, col, k.thick * 0.5f);
    }

    // One rotation ring: a great circle of the sphere, projected. The half that is
    // turned away from the viewer is drawn faded, like Unity's gizmo.
    static void DrawRing3(Vector2 p, const gizmo_math::RingBasis& b, float R, float thick, Color col)
    {
        constexpr int kSegments = 64;
        Vector3 last = gizmo_math::RingPoint3(b, R, 0.0f);
        for (int i = 1; i <= kSegments; ++i)
        {
            const float t = 2.0f * PI * static_cast<float>(i) / kSegments;
            const Vector3 next = gizmo_math::RingPoint3(b, R, t);
            const bool facing = (last.z + next.z) * kNearSign >= 0.0f;
            DrawLineEx({ p.x + last.x, p.y + last.y }, { p.x + next.x, p.y + next.y },
                       facing ? thick : thick * 0.75f, facing ? col : Fade(col, 0.15f));
            last = next;
        }
    }

    void DrawGizmo(float zoom, Vector2 mouseWorld)
    {
        SpriteBounds b;
        if (!SelectedBounds(b)) return;

        const Metrics k(zoom);
        const Vector2 p = b.position;
        const gizmo_math::Axes ax = gizmo_math::AxesFromAngle(b.angle);   // follows the sprite's rotation
        const Handle lit = (drag_ == Drag::Dragging) ? handle_ : hover_;  // highlighted handle

        const Color red   = { 230,  70,  70, 255 };
        const Color green = {  80, 200,  90, 255 };
        const Color blue  = {  90, 160, 255, 255 };
        const Color hot   = YELLOW;

        switch (state_.tool)
        {
            case Tool::Move:
            {
                const Color cx = (lit == Handle::MoveX) ? hot : red;
                const Color cy = (lit == Handle::MoveY) ? hot : green;
                DrawArrow(p, ax.x, ax.y, k, cx);
                DrawArrow(p, ax.y, ax.x, k, cy);

                const Color box = (lit == Handle::Free) ? hot : Fade(WHITE, 0.6f);
                DrawBox(p, ax, k.square, Fade(box, 0.35f), box, k.thick * 0.75f);
                break;
            }

            case Tool::Rotate:
            {
                const Color cx = (lit == Handle::RotateX) ? hot : red;
                const Color cy = (lit == Handle::RotateY) ? hot : green;
                const Color cz = (lit == Handle::RotateZ) ? hot : blue;

                // The sphere: a faint disc and its outline, then one ring per local axis.
                DrawCircleV(p, k.ring, Fade(WHITE, 0.04f));
                DrawRing(p, k.ring - k.thick * 0.25f, k.ring + k.thick * 0.25f, 0.0f, 360.0f, 64, Fade(WHITE, 0.35f));

                DrawRing3(p, gizmo_math::RingBasisFor(0, b.rotation), k.ring, k.thick, cx);
                DrawRing3(p, gizmo_math::RingBasisFor(1, b.rotation), k.ring, k.thick, cy);
                DrawRing3(p, gizmo_math::RingBasisFor(2, b.rotation), k.ring, k.thick, cz);
                break;
            }

            case Tool::Scale:
            {
                const Color cx = (lit == Handle::ScaleX) ? hot : red;
                const Color cy = (lit == Handle::ScaleY) ? hot : green;
                DrawCubeHandle(p, ax.x, ax, k, cx);
                DrawCubeHandle(p, ax.y, ax, k, cy);

                const Color box = (lit == Handle::ScaleAll) ? hot : Fade(WHITE, 0.6f);
                DrawBox(p, ax, k.square, Fade(box, 0.35f), box, k.thick * 0.75f);
                break;
            }
        }
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
    Vector3      startEuler_  = {0, 0, 0};      // its Euler angles at press, degrees
    Vector2      pivot_       = {0, 0};         // gizmo centre, world pixels
    gizmo_math::Axes axes_    = { {1, 0}, {0, 1} };   // the sprite's axes at press, frozen for the drag
    gizmo_math::RingBasis ringBasis_ = { {1, 0, 0}, {0, 1, 0}, {0, 0, 1} };   // the grabbed ring at press, frozen for the drag
    int          ringIndex_   = 2;              // 0 = X, 1 = Y, 2 = Z
    float        ringR_       = 1.0f;           // sphere radius at press (world pixels)
    bool         usePlane_    = true;           // drag by angle in the ring's plane (else along `tangent_`)
    Vector2      tangent_     = {1, 0};         // screen direction of +rotation at the grab point (edge-on rings)
    float        lastPhi_     = 0.0f;           // ring angle last frame, radians
    float        accum_       = 0.0f;           // total angle turned this drag, radians
    float        axisPressDist_ = 1.0f;         // pivot -> mouse along the scale axis at press
    float        axisMinDist_ = 1.0f;           // smallest distance used as the scale reference

    UndoStack undo_;
};