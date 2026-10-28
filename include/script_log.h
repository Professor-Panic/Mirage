// script_log.h
#pragma once
#include <deque>
#include <string>
#include <sol/sol.hpp>

class ScriptLog {
public:
    static constexpr size_t kMax = 2000;      // cap so it can't grow forever
    void Add(std::string line) {
        lines_.push_back(std::move(line));
        if (lines_.size() > kMax) lines_.pop_front();
        dirty_ = true;
    }

    void Clear() { lines_.clear(); dirty_ = true; }

    bool ConsumeDirty() { bool d = dirty_; dirty_ = false; return d; }

    std::string Text() const {
        std::string out;
        for (const auto& l : lines_) { out += l; out += '\n'; }
        return out;
    }

private:
    std::deque<std::string> lines_;
    bool dirty_ = false;
};

// Replaces Lua's print(). Matches the original: args joined by tabs,
// each run through tostring() so numbers, bools, nil and __tostring work.
inline void BindLuaPrint(sol::state& lua, ScriptLog& log) {
    lua.set_function("print", [&lua, &log](sol::variadic_args args) {
        sol::function tostr = lua["tostring"];
        std::string line;
        bool first = true;

        for (sol::stack_proxy a : args) {
            if (!first) line += '\t';
            first = false;
            std::string s = tostr(a);
            line += s;
        }

        log.Add(std::move(line));
    });
}