#pragma once
#include <filesystem>
#include <string>

#include "mirage_core/line_socket.h"
#include "mirage_core/runtime.h"
#include "mirage_core/serialise.h"          // Mirage::SaveSceneFile, nlohmann::json
#include "mirage_editor/editor_state.h"
#include "mirage_editor/player_process.h"

// Edit <-> Play. Play runs the scene in a separate Mirage-Player process, so
// the editor's scene is never touched while playing (no snapshot / restore).
//
// The UI writes state.mode (onClick="mode = 1"); Update() notices the change
// and starts or stops the player.
class PlaySession
{
public:
    PlaySession(MirageCoreRuntime& core, EditorState& state)
        : core_(core), state_(state), lastMode_(state.mode)
    {
        // Local connection from the player (it connects to a free port we pick).
        std::string err;
        if (!listener_.Listen(0, &err))
            core_.scriptLog.Add("[play] cannot open a local port: " + err);
    }

    PlaySession(const PlaySession&) = delete;
    PlaySession& operator=(const PlaySession&) = delete;

    // Once per frame, before anything that reads state.mode.
    void Update()
    {
        // Messages from the player. Read before the exit check below, so the
        // last lines it printed are not lost.
        if (state_.mode == Mode::Play)
        {
            if (!conn_.Connected()) listener_.Accept(conn_);
            for (const std::string& line : conn_.Poll()) HandleLine(line);
        }

        // The player closed itself (its window, a crash, a script error):
        // drop back to Edit. Only once it has really been launched
        // (lastMode_ == Play), or this would fire in the frame Play is pressed.
        if (state_.mode == Mode::Play && lastMode_ == Mode::Play && !player_.IsRunning())
        {
            core_.scriptLog.Add("[play] player exited (code " +
                                std::to_string(player_.LastExitCode()) + ")");
            state_.mode = Mode::Edit;
        }

        if (state_.mode != lastMode_)
        {
            if (state_.mode == Mode::Play) StartPlay();
            else                           StopPlay();
            lastMode_ = state_.mode;
        }
    }

    // After the main loop.
    void Shutdown() { player_.Stop(); }

private:
    // One message from the player. Game -> editor: log, error, ready.
    void HandleLine(const std::string& line)
    {
        const nlohmann::json m = nlohmann::json::parse(line, nullptr, false);
        if (!m.is_object() || !m.contains("type") || !m["type"].is_string()) return;

        const std::string type = m["type"].get<std::string>();
        const std::string text = (m.contains("text") && m["text"].is_string())
                                     ? m["text"].get<std::string>() : std::string{};

        if      (type == "log")   core_.scriptLog.Add(text);
        else if (type == "error") core_.scriptLog.Add("[error] " + text);
        else if (type == "ready") core_.scriptLog.Add("[play] player ready");
    }

    // Edit -> Play: save the scene to a temp file and launch the player on it.
    void StartPlay()
    {
        core_.scriptLog.Clear();
        conn_.Close();

        std::string err;
        bool ok = std::filesystem::exists(kPlayerExe);
        if (!ok) err = std::string("player not found: ") + kPlayerExe + " (build it with run-player.bat)";
        if (ok)  ok = Mirage::SaveSceneFile(core_, kPlayScene, &err);
        if (ok)  ok = player_.Start(std::filesystem::absolute(kPlayerExe).make_preferred().string(),
                                    std::filesystem::absolute(kPlayScene).make_preferred().string(),
                                    std::filesystem::current_path().string(),
                                    listener_.Port(), &err);
        if (ok)
            core_.scriptLog.Add("[play] player started");
        else
        {
            core_.scriptLog.Add("[play] " + err);
            state_.mode = Mode::Edit;          // could not start: stay in Edit
        }
    }

    // Play -> Edit (Stop pressed, or the player exited).
    void StopPlay()
    {
        for (const std::string& line : conn_.Poll()) HandleLine(line);
        player_.Stop();
        conn_.Close();
    }

    static constexpr const char* kPlayerExe = "build-player/Mirage-Player.exe";  // relative to the project root
    static constexpr const char* kPlayScene = ".mirage/play.scene";              // temp copy handed to the player

    MirageCoreRuntime& core_;
    EditorState&       state_;
    Mode               lastMode_;
    PlayerProcess      player_;
    LineListener       listener_;
    LineSocket         conn_;
};
