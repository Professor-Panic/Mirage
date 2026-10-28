#include "raylib.h"

#include <algorithm>
#include <iostream>
#include <string>

#include "mirage_core/runtime.h"
#include "mirage_core/serialise.h"
#include "mirage_core/line_socket.h"
#include "mirage_player/cerr_tee.h"

using json = nlohmann::json;

int main(int argc, char** argv)
{
    // ---- command line: --scene <file> [--port <n>] ----
    std::string scenePath;
    int port = 0;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--scene" && i + 1 < argc) scenePath = argv[++i];
        else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    }

    if (scenePath.empty())
    {
        TraceLog(LOG_ERROR, "usage: Mirage-Player --scene <file> [--port <n>]");
        return 1;
    }

    InitWindow(1200, 800, "Mirage-Player");
    // ---- connection to the editor (optional) ----
    // Declared before `core`, so it is still alive while core shuts down.
    LineSocket editor;
    if (port > 0)
    {
        std::string err;
        if (editor.Connect("127.0.0.1", port, &err))
            TraceLog(LOG_INFO, "connected to editor on port %d", port);
        else
            TraceLog(LOG_WARNING, "no editor connection: %s (running standalone)", err.c_str());
    }

    // Everything sent to the editor goes through here. The game never waits:
    // Send() only queues, and at most 200 messages per second are sent.
    double windowStart = GetTime();
    int    sentInWindow = 0;
    std::size_t rateDropped = 0;

    const auto sendMessage = [&](const char* type, const std::string& text)
    {
        if (!editor.Connected()) return;

        const double now = GetTime();
        if (now - windowStart >= 1.0) { windowStart = now; sentInWindow = 0; }
        if (sentInWindow >= 200) { ++rateDropped; return; }
        ++sentInWindow;

        json j;
        j["type"] = type;
        j["text"] = text;
        // 'replace': a script printing invalid UTF-8 must not throw here.
        editor.Send(j.dump(-1, ' ', false, json::error_handler_t::replace));
    };

    // Script errors are written to std::cerr by ScriptSystem: copy them to the editor too.
    CerrTee errorTee(std::cerr, [&](const std::string& line) { sendMessage("error", line); });

    MirageCoreRuntime core;

    // Set before loading: loading runs each script's factory, which may print.
    core.scriptLog.onAdd = [&](const std::string& line)
    {
        TraceLog(LOG_INFO, "[lua] %s", line.c_str());
        sendMessage("log", line);
    };

    auto r = Mirage::LoadSceneFile(core, scenePath);
    for (auto& w : r.warnings) TraceLog(LOG_WARNING, "[scene] %s", w.c_str());
    if (!r.ok)
    {
        TraceLog(LOG_ERROR, "[scene] %s", r.error.c_str());
        sendMessage("error", "[scene] " + r.error);
        editor.Poll();                       // flush before we leave
        core.scriptSystem.UnloadAll();
        CloseWindow();
        return 1;
    }

    if (editor.Connected())
        editor.Send(json{{"type", "ready"}, {"version", 1}}.dump());

    // ---- main loop: fixed-timestep update, draw at the display's rate ----
    const float step = 1.0f / 60.0f;
    float acc = 0.0f;
    bool running = true;
    bool wasConnected = editor.Connected();

    while (running && !WindowShouldClose())
    {
        // Messages from the editor.
        for (const std::string& line : editor.Poll())
        {
            const json m = json::parse(line, nullptr, false);
            if (!m.is_object() || !m.contains("type") || !m["type"].is_string()) continue;

            const std::string type = m["type"].get<std::string>();
            if (type == "stop") running = false;
            // Phase 6: "reload_script", "set_field"
        }

        if (wasConnected && !editor.Connected())
        {
            TraceLog(LOG_WARNING, "editor connection lost, continuing without it");
            wasConnected = false;
        }

        // Tell the editor if we had to drop messages (buffer full or rate limit).
        const std::size_t dropped = editor.TakeDropped() + rateDropped;
        rateDropped = 0;
        if (dropped > 0 && editor.Connected())
            editor.Send(json{{"type", "log"},
                             {"text", "[player] " + std::to_string(dropped) + " messages dropped"}}.dump());

        acc += std::min(GetFrameTime(), 0.25f);
        while (acc >= step)
        {
            core.scriptSystem.UpdateScripts(step);
            acc -= step;
        }

        BeginDrawing();
        ClearBackground(BLACK);
        DrawFPS(10, 10);
        EndDrawing();
    }

    core.scriptSystem.UnloadAll();
    CloseWindow();
    return 0;
}
