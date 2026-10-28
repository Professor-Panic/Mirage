// Win32 only. Do NOT include raylib.h in this file.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "mirage_editor/player_process.h"

namespace {

std::wstring ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), w.data(), n);
    return w;
}

}  // namespace

PlayerProcess::PlayerProcess() {
    // A job with KILL_ON_JOB_CLOSE: when the editor exits (even by crashing),
    // Windows closes our handle and kills every process in the job.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof info);
    }
    job_ = job;
}

PlayerProcess::~PlayerProcess() {
    Stop();
    if (job_) CloseHandle((HANDLE)job_);   // kills anything still in the job
}

bool PlayerProcess::Start(const std::string& exePath, const std::string& scenePath,
                          const std::string& workingDir, int port, std::string* error) {
    Stop();

    const std::wstring exe  = ToWide(exePath);
    const std::wstring cwd  = ToWide(workingDir);
    std::wstring cmd = L"\"" + exe + L"\" --scene \"" + ToWide(scenePath) + L"\"";
    if (port > 0) cmd += L" --port " + std::to_wstring(port);

    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};

    // Suspended so it cannot run (or spawn children) before it is in the job.
    // Own console so the player's log output is visible.
    const BOOL ok = CreateProcessW(
        exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
        CREATE_SUSPENDED | CREATE_NEW_CONSOLE,
        nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);

    if (!ok) {
        if (error) *error = "could not start player (Win32 error " +
                            std::to_string(GetLastError()) + "): " + exePath;
        return false;
    }

    // If this fails (e.g. the editor already sits in a job that forbids it)
    // the player still runs, it just will not die with the editor.
    if (job_) AssignProcessToJobObject((HANDLE)job_, pi.hProcess);

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    process_  = pi.hProcess;
    exitCode_ = 0;
    return true;
}

bool PlayerProcess::IsRunning() {
    if (!process_) return false;

    HANDLE h = (HANDLE)process_;
    if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT) return true;

    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    exitCode_ = code;
    CloseHandle(h);
    process_ = nullptr;
    return false;
}

void PlayerProcess::Stop() {
    if (!process_) return;

    HANDLE h = (HANDLE)process_;
    TerminateProcess(h, 0);
    WaitForSingleObject(h, 2000);
    CloseHandle(h);
    process_ = nullptr;
}
