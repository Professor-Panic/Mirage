// mirage_editor/player_process.h
//
// Launches and controls the Mirage-Player process (Windows).
//
// Deliberately free of <windows.h>: Win32 clashes with raylib's names
// (Rectangle, CloseWindow, ShowCursor, ...). All Win32 code lives in
// src/mirage_editor/player_process.cpp, which must NOT include raylib.h.
#pragma once
#include <string>

class PlayerProcess {
public:
    PlayerProcess();
    ~PlayerProcess();                      // stops the player; the job object also kills it
    PlayerProcess(const PlayerProcess&) = delete;
    PlayerProcess& operator=(const PlayerProcess&) = delete;

    // Starts `exePath --scene <scenePath> [--port <port>]` with `workingDir` as
    // its current directory. port <= 0 means "no connection back to the editor".
    // Replaces a player that is already running.
    bool Start(const std::string& exePath, const std::string& scenePath,
               const std::string& workingDir, int port = 0, std::string* error = nullptr);

    // True while the player is alive. Once it has exited this returns false
    // (and records the exit code).
    bool IsRunning();

    // Kills the player if it is running. Safe to call any time.
    void Stop();

    unsigned long LastExitCode() const { return exitCode_; }

private:
    void*         job_     = nullptr;      // HANDLE: kill-on-close job object
    void*         process_ = nullptr;      // HANDLE: the running player
    unsigned long exitCode_ = 0;
};
