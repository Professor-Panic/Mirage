#pragma once
#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <unordered_map>
#include "constants.h"
#include "raylib.h"
#include "sol/sol.hpp"
#include "mirage_core/script_log.h"
#include "mirage_core/lua_binding_backend.h"
#include "mirage_core/constants.h"

namespace lua_input_detail {

// One-frame events (pressed / released, mouse delta, wheel) are true for a
// single rendered frame. The player updates scripts in fixed steps, so a frame
// can run zero steps and the event would be missed. The latch holds them until
// a script step has seen them. While `enabled` is false (the editor), the
// functions read raylib directly, exactly as before.
struct Latch {
    bool    enabled = false;
    bool    keyPressed[512]  = {};
    bool    keyReleased[512] = {};
    bool    mousePressed[8]  = {};
    bool    mouseReleased[8] = {};
    Vector2 delta = {0, 0};
    float   wheel = 0.0f;
};
inline Latch& GetLatch() { static Latch l; return l; }

inline std::string NormalizeKeyName(std::string name) {
    for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (char& c : name) if (c == ' ' || c == '-') c = '_';
    if (name.rfind("KEY_", 0) == 0) name.erase(0, 4);
    return name;
}

// Keys are stored without the KEY_ prefix, so "KEY_SPACE", "key_space",
// "Space" and "SPACE" all resolve to KEY_SPACE.
inline const std::unordered_map<std::string, int>& KeyTable() {
    static const std::unordered_map<std::string, int> table = [] {
        std::unordered_map<std::string, int> t;
#define RAY_KEY(name) t[#name] = KEY_##name
        RAY_KEY(A); RAY_KEY(B); RAY_KEY(C); RAY_KEY(D); RAY_KEY(E); RAY_KEY(F); RAY_KEY(G);
        RAY_KEY(H); RAY_KEY(I); RAY_KEY(J); RAY_KEY(K); RAY_KEY(L); RAY_KEY(M); RAY_KEY(N);
        RAY_KEY(O); RAY_KEY(P); RAY_KEY(Q); RAY_KEY(R); RAY_KEY(S); RAY_KEY(T); RAY_KEY(U);
        RAY_KEY(V); RAY_KEY(W); RAY_KEY(X); RAY_KEY(Y); RAY_KEY(Z);
        RAY_KEY(ZERO); RAY_KEY(ONE); RAY_KEY(TWO); RAY_KEY(THREE); RAY_KEY(FOUR);
        RAY_KEY(FIVE); RAY_KEY(SIX); RAY_KEY(SEVEN); RAY_KEY(EIGHT); RAY_KEY(NINE);
        RAY_KEY(SPACE); RAY_KEY(ENTER); RAY_KEY(ESCAPE); RAY_KEY(TAB); RAY_KEY(BACKSPACE);
        RAY_KEY(DELETE); RAY_KEY(INSERT); RAY_KEY(HOME); RAY_KEY(END);
        RAY_KEY(PAGE_UP); RAY_KEY(PAGE_DOWN);
        RAY_KEY(UP); RAY_KEY(DOWN); RAY_KEY(LEFT); RAY_KEY(RIGHT);
        RAY_KEY(LEFT_SHIFT); RAY_KEY(RIGHT_SHIFT); RAY_KEY(LEFT_CONTROL); RAY_KEY(RIGHT_CONTROL);
        RAY_KEY(LEFT_ALT); RAY_KEY(RIGHT_ALT);
        RAY_KEY(F1); RAY_KEY(F2); RAY_KEY(F3); RAY_KEY(F4); RAY_KEY(F5); RAY_KEY(F6);
        RAY_KEY(F7); RAY_KEY(F8); RAY_KEY(F9); RAY_KEY(F10); RAY_KEY(F11); RAY_KEY(F12);
        RAY_KEY(MINUS); RAY_KEY(EQUAL); RAY_KEY(COMMA); RAY_KEY(PERIOD); RAY_KEY(SLASH);
        RAY_KEY(SEMICOLON); RAY_KEY(APOSTROPHE); RAY_KEY(GRAVE);
        RAY_KEY(LEFT_BRACKET); RAY_KEY(RIGHT_BRACKET); RAY_KEY(BACKSLASH);
        RAY_KEY(KP_0); RAY_KEY(KP_1); RAY_KEY(KP_2); RAY_KEY(KP_3); RAY_KEY(KP_4);
        RAY_KEY(KP_5); RAY_KEY(KP_6); RAY_KEY(KP_7); RAY_KEY(KP_8); RAY_KEY(KP_9);
        RAY_KEY(KP_ENTER);
#undef RAY_KEY
        // Friendly aliases.
        t["0"] = KEY_ZERO;  t["1"] = KEY_ONE;   t["2"] = KEY_TWO;   t["3"] = KEY_THREE;
        t["4"] = KEY_FOUR;  t["5"] = KEY_FIVE;  t["6"] = KEY_SIX;   t["7"] = KEY_SEVEN;
        t["8"] = KEY_EIGHT; t["9"] = KEY_NINE;
        t["ESC"] = KEY_ESCAPE; t["RETURN"] = KEY_ENTER;
        t["SHIFT"] = KEY_LEFT_SHIFT; t["CTRL"] = KEY_LEFT_CONTROL; t["ALT"] = KEY_LEFT_ALT;
        t["PGUP"] = KEY_PAGE_UP; t["PGDN"] = KEY_PAGE_DOWN;
        return t;
    }();
    return table;
}

// Returns KEY_NULL (0) when the name is unknown.
inline int ParseKey(const std::string& name) {
    const auto& table = KeyTable();
    const auto it = table.find(NormalizeKeyName(name));
    return it == table.end() ? KEY_NULL : it->second;
}

// Mouse buttons: "left", "right", "middle", "side", "extra", "forward", "back",
// optionally written MOUSE_BUTTON_LEFT / MOUSE_LEFT, in any case. A plain
// number (0 = left, 1 = right, 2 = middle ...) is accepted too.
// Returns -1 when the name is unknown.
inline int ParseMouseButton(std::string name) {
    for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (char& c : name) if (c == ' ' || c == '-') c = '_';
    if (name.rfind("MOUSE_BUTTON_", 0) == 0) name.erase(0, 13);
    else if (name.rfind("MOUSE_", 0) == 0)   name.erase(0, 6);

    if (name == "LEFT")    return MOUSE_BUTTON_LEFT;
    if (name == "RIGHT")   return MOUSE_BUTTON_RIGHT;
    if (name == "MIDDLE")  return MOUSE_BUTTON_MIDDLE;
    if (name == "SIDE")    return MOUSE_BUTTON_SIDE;
    if (name == "EXTRA")   return MOUSE_BUTTON_EXTRA;
    if (name == "FORWARD") return MOUSE_BUTTON_FORWARD;
    if (name == "BACK")    return MOUSE_BUTTON_BACK;
    return -1;
}

} // namespace lua_input_detail

// Player loop usage (the editor does not call these):
//
//   acc += std::min(GetFrameTime(), 0.25f);
//   LatchInputFrame();                  // record this frame's presses
//   while (acc >= step) {
//       core.scriptSystem.UpdateScripts(step);
//       ClearLatchedInput();            // a press is seen by exactly one step
//       acc -= step;
//   }

// Once per rendered frame, before the update steps.
inline void LatchInputFrame() {
    auto& l = lua_input_detail::GetLatch();
    l.enabled = true;
    for (const auto& kv : lua_input_detail::KeyTable()) {
        const int k = kv.second;
        if (k <= 0 || k >= 512) continue;
        if (IsKeyPressed(k))  l.keyPressed[k]  = true;
        if (IsKeyReleased(k)) l.keyReleased[k] = true;
    }
    for (int b = 0; b <= MOUSE_BUTTON_BACK; ++b) {
        if (IsMouseButtonPressed(b))  l.mousePressed[b]  = true;
        if (IsMouseButtonReleased(b)) l.mouseReleased[b] = true;
    }
    const Vector2 d = GetMouseDelta();
    l.delta.x += d.x;
    l.delta.y += d.y;
    l.wheel   += GetMouseWheelMove();
}

// After every script step: a press is seen by exactly one step.
inline void ClearLatchedInput() {
    auto& l = lua_input_detail::GetLatch();
    std::fill(std::begin(l.keyPressed),    std::end(l.keyPressed),    false);
    std::fill(std::begin(l.keyReleased),   std::end(l.keyReleased),   false);
    std::fill(std::begin(l.mousePressed),  std::end(l.mousePressed),  false);
    std::fill(std::begin(l.mouseReleased), std::end(l.mouseReleased), false);
    l.delta = {0, 0};
    l.wheel = 0.0f;
}

inline ScriptApiCallable InputApiCall(std::string name, std::string description,
                                      std::vector<ScriptApiParameter> parameters,
                                      std::vector<std::string> returns)
{
    ScriptApiCallable call{ std::move(name), std::move(description),
                            std::move(parameters), std::move(returns), false };
    call.customBinding = true;
    call.bindingNotes = "Uses Mirage's Raylib input bridge, key/button name normalization, and fixed-step input latch.";
    return call;
}

inline ScriptApiParameter InputApiParam(std::string name, std::string type)
{
    return { std::move(name), std::move(type), false, false, {} };
}

// Creates a global `input` table.
//
// Keyboard (key names are case-insensitive, KEY_ prefix optional):
//   input.IsKeyDown("KEY_A")        -> true while the key is held
//   input.IsKeyUp("KEY_A")          -> true while the key is NOT held
//   input.IsKeyPressed("SPACE")     -> true only on the frame it goes down
//   input.IsKeyReleased("SPACE")    -> true only on the frame it is let go
//
// Mouse (button: "left", "right", "middle", "side", "extra", "forward",
// "back", MOUSE_BUTTON_LEFT, or the numbers 0, 1, 2 ...):
//   input.IsMouseDown("left")       -> true while the button is held
//   input.IsMouseUp("left")         -> true while the button is NOT held
//   input.IsMousePressed("left")    -> true only on the frame it goes down
//   input.IsMouseReleased("left")   -> true only on the frame it is let go
//   input.GetMousePosition()        -> { x, y, z = 0 } in window pixels
//   input.GetMouseWorldPosition()   -> { x, y, z = 0 } in world units
//                                      (window pixels / kPixelsPerUnit; valid
//                                      for the player, which has no camera)
//   input.GetMouseX() / GetMouseY()
//   input.GetMouseDelta()           -> { x = ..., y = ... } movement this frame
//   input.GetMouseWheel()           -> scroll this frame (positive = away from you)
//
// Unknown key or button names return false and are reported once to `log`.
inline void BindLuaInput(sol::state& lua, ScriptLog& log, LuaBindingBackend& api) {
    sol::table input = api.CreateGlobalTable(lua,
        { "input", "Keyboard and mouse input state for the current frame.", false, {} });

    auto resolve = [&log](const std::string& name) -> int {
        const int key = lua_input_detail::ParseKey(name);
        if (key == KEY_NULL) {
            static std::unordered_map<std::string, bool> reported;
            if (!reported[name]) {
                reported[name] = true;
                log.Add("input: unknown key '" + name + "'");
            }
        }
        return key;
    };

    // A button may be given as a name or as a number. -1 = unknown.
    auto resolveButton = [&log](const sol::object& value) -> int {
        if (value.get_type() == sol::type::number) {
            const int button = value.as<int>();
            return (button >= 0 && button <= MOUSE_BUTTON_BACK) ? button : -1;
        }

        if (value.get_type() == sol::type::string) {
            const std::string name = value.as<std::string>();
            const int button = lua_input_detail::ParseMouseButton(name);
            if (button < 0) {
                static std::unordered_map<std::string, bool> reported;
                if (!reported[name]) {
                    reported[name] = true;
                    log.Add("input: unknown mouse button '" + name + "'");
                }
            }
            return button;
        }

        return -1;
    };

    // --- keyboard ----------------------------------------------------------

    api.BindTableFunction(input, "input",
        InputApiCall("IsKeyDown", "Check whether a keyboard key is currently held down.", { InputApiParam("key", "string") }, { "boolean" }), [resolve](const std::string& name) -> bool {
        const int key = resolve(name);
        return key != KEY_NULL && IsKeyDown(key);
    });

    api.BindTableFunction(input, "input",
        InputApiCall("IsKeyUp", "Check whether a keyboard key is currently up.", { InputApiParam("key", "string") }, { "boolean" }), [resolve](const std::string& name) -> bool {
        const int key = resolve(name);
        return key != KEY_NULL && IsKeyUp(key);
    });

    api.BindTableFunction(input, "input",
        InputApiCall("IsKeyPressed", "Check whether a keyboard key was pressed this frame.", { InputApiParam("key", "string") }, { "boolean" }), [resolve](const std::string& name) -> bool {
        const int key = resolve(name);
        if (key == KEY_NULL) return false;
        const auto& l = lua_input_detail::GetLatch();
        return l.enabled ? l.keyPressed[key] : IsKeyPressed(key);
    });

    api.BindTableFunction(input, "input",
        InputApiCall("IsKeyReleased", "Check whether a keyboard key was released this frame.", { InputApiParam("key", "string") }, { "boolean" }), [resolve](const std::string& name) -> bool {
        const int key = resolve(name);
        if (key == KEY_NULL) return false;
        const auto& l = lua_input_detail::GetLatch();
        return l.enabled ? l.keyReleased[key] : IsKeyReleased(key);
    });

    // --- mouse buttons -----------------------------------------------------

    api.BindTableFunction(input, "input",
        InputApiCall("IsMouseDown", "Check whether a mouse button is currently held down.", { InputApiParam("button", "string | integer") }, { "boolean" }), [resolveButton](sol::object button) -> bool {
        const int b = resolveButton(button);
        return b >= 0 && IsMouseButtonDown(b);
    });

    api.BindTableFunction(input, "input",
        InputApiCall("IsMouseUp", "Check whether a mouse button is currently up.", { InputApiParam("button", "string | integer") }, { "boolean" }), [resolveButton](sol::object button) -> bool {
        const int b = resolveButton(button);
        return b >= 0 && IsMouseButtonUp(b);
    });

    api.BindTableFunction(input, "input",
        InputApiCall("IsMousePressed", "Check whether a mouse button was pressed this frame.", { InputApiParam("button", "string | integer") }, { "boolean" }), [resolveButton](sol::object button) -> bool {
        const int b = resolveButton(button);
        if (b < 0) return false;
        const auto& l = lua_input_detail::GetLatch();
        return l.enabled ? l.mousePressed[b] : IsMouseButtonPressed(b);
    });

    api.BindTableFunction(input, "input",
        InputApiCall("IsMouseReleased", "Check whether a mouse button was released this frame.", { InputApiParam("button", "string | integer") }, { "boolean" }), [resolveButton](sol::object button) -> bool {
        const int b = resolveButton(button);
        if (b < 0) return false;
        const auto& l = lua_input_detail::GetLatch();
        return l.enabled ? l.mouseReleased[b] : IsMouseButtonReleased(b);
    });

    // --- mouse position, movement and wheel --------------------------------

    api.BindTableFunction(input, "input",
        InputApiCall("GetMousePosition", "Get the mouse position in window pixels as an x/y/z table.", {  }, { "{ x: number, y: number, z: number }" }), [&lua]() -> sol::table {
        const Vector2 p = GetMousePosition();
        sol::table t = lua.create_table();
        t["x"] = p.x;
        t["y"] = p.y;
        t["z"] = 0.0f;
        return t;
    });

    api.BindTableFunction(input, "input",
        InputApiCall("GetMouseWorldPosition", "Get the mouse position in world units as an x/y/z table.", {  }, { "{ x: number, y: number, z: number }" }), [&lua]() -> sol::table {
        const Vector2 p = GetMousePosition();
        sol::table t = lua.create_table();
        t["x"] = p.x / kPixelsPerUnit;
        t["y"] = p.y / kPixelsPerUnit;
        t["z"] = 0.0f;
        return t;
    });

    api.BindTableFunction(input, "input",
        InputApiCall("GetMouseX", "Get the mouse X coordinate in window pixels.", {  }, { "number" }), []() -> float { return static_cast<float>(GetMouseX()); });
    api.BindTableFunction(input, "input",
        InputApiCall("GetMouseY", "Get the mouse Y coordinate in window pixels.", {  }, { "number" }), []() -> float { return static_cast<float>(GetMouseY()); });

    api.BindTableFunction(input, "input",
        InputApiCall("GetMouseDelta", "Get mouse movement accumulated for the current script step.", {  }, { "{ x: number, y: number }" }), [&lua]() -> sol::table {
        const auto& l = lua_input_detail::GetLatch();
        const Vector2 d = l.enabled ? l.delta : GetMouseDelta();
        sol::table t = lua.create_table();
        t["x"] = d.x;
        t["y"] = d.y;
        return t;
    });

    api.BindTableFunction(input, "input",
        InputApiCall("GetMouseWheel", "Get the mouse-wheel movement for the current script step.", {  }, { "number" }), []() -> float {
        const auto& l = lua_input_detail::GetLatch();
        return l.enabled ? l.wheel : GetMouseWheelMove();
    });
}