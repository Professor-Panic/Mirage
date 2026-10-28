#pragma once
#include <cctype>
#include <string>
#include <unordered_map>
#include "raylib.h"
#include "sol/sol.hpp"
#include "mirage_core/script_log.h"

namespace lua_input_detail {

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

} // namespace lua_input_detail

// Creates a global `input` table:
//   input.IsKeyDown("KEY_A")     -> true while the key is held
//   input.IsKeyPressed("KEY_SPACE") -> true only on the frame it goes down
// Unknown key names return false and are reported once to `log`.
inline void BindLuaInput(sol::state& lua, ScriptLog& log) {
    sol::table input = lua.create_named_table("input");

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

    input.set_function("IsKeyDown", [resolve](const std::string& name) -> bool {
        const int key = resolve(name);
        return key != KEY_NULL && IsKeyDown(key);
    });

    input.set_function("IsKeyPressed", [resolve](const std::string& name) -> bool {
        const int key = resolve(name);
        return key != KEY_NULL && IsKeyPressed(key);
    });
}