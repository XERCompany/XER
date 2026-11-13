#include "watch/watch_recompile_service.h"

#include "error_handler/err_capture.h"
#include "watch/watch_lock_recovery.h"
#include "helper/string_utils.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <chrono>
#include <thread>

namespace {

#ifdef _WIN32

std::string CurrentExecutablePath() {
    wchar_t buf[MAX_PATH * 4]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0) {
        return "./build-copilot/EngineBuilder";
    }
    return XER::Helper::String::WideToUtf8(std::wstring_view(buf, n));
}

#else

std::string CurrentExecutablePath() {
    char buf[PATH_MAX];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return "./build-copilot/EngineBuilder";
    }
    buf[n] = '\0';
    return std::string(buf);
}

#endif

}  // namespace

WatchRecompileService::WatchRecompileService() = default;

WatchRecompileService::~WatchRecompileService() {
    Stop();
}

bool WatchRecompileService::Recompile(const std::string& script_path,
                                      std::string* error_message) const {
#ifdef _WIN32
    if (child_process_ != nullptr) {
        if (!const_cast<WatchRecompileService*>(this)->StopChild(800, error_message)) {
            return false;
        }
    }
#else
    if (child_pid_ > 0) {
        if (!const_cast<WatchRecompileService*>(this)->StopChild(800, error_message)) {
            return false;
        }
    }
#endif

    std::string lock_error;
    const std::filesystem::path script_fs(script_path);
    if (!WatchLockRecovery::CleanupStaleLockForScript(script_fs, &lock_error)) {
        if (error_message) {
            *error_message = lock_error;
        }
        return false;
    }

	XER::ErrorHandler::UpdateCrashContext(script_path, ".");

    return const_cast<WatchRecompileService*>(this)->SpawnChild(script_path, error_message);
}

void WatchRecompileService::Stop() {
    std::string ignored;
    (void)StopChild(500, &ignored);
}

bool WatchRecompileService::SpawnChild(const std::string& script_path,
                                       std::string* error_message) {
    const std::string exe = CurrentExecutablePath();

#ifdef _WIN32
    const std::wstring exe_w = XER::Helper::String::Utf8ToWide(exe);
    const std::wstring script_w = XER::Helper::String::Utf8ToWide(script_path);
    std::wstring command = L"\"" + exe_w + L"\" run \"" + script_w + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe_w.c_str(), command.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &si, &pi)) {
        if (error_message) {
            *error_message = "CreateProcess failed while starting watch child (error " +
                std::to_string(GetLastError()) + ")";
        }
        return false;
    }
    CloseHandle(pi.hThread);
    child_process_ = pi.hProcess;
    return true;
#else
    const pid_t pid = ::fork();
    if (pid < 0) {
        if (error_message) {
            *error_message = "fork() failed while starting watch child";
        }
        return false;
    }

    if (pid == 0) {
        ::execl(exe.c_str(), exe.c_str(), "run", script_path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    child_pid_ = static_cast<int>(pid);
    return true;
#endif
}

bool WatchRecompileService::StopChild(int grace_ms,
                                      std::string* error_message) {
#ifdef _WIN32
    if (child_process_ == nullptr) {
        return true;
    }
    HANDLE process = static_cast<HANDLE>(child_process_);

    // If already exited, reap immediately.
    if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
        CloseHandle(process);
        child_process_ = nullptr;
        return true;
    }

    // Grace period: wait for natural exit before terminating.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(grace_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const DWORD rc = WaitForSingleObject(process, 20);
        if (rc == WAIT_OBJECT_0) {
            CloseHandle(process);
            child_process_ = nullptr;
            return true;
        }
        if (rc == WAIT_FAILED) {
            CloseHandle(process);
            child_process_ = nullptr;
            return true;
        }
    }

    TerminateProcess(process, 1);
    WaitForSingleObject(process, 5000);
    CloseHandle(process);
    child_process_ = nullptr;

    if (error_message) {
        *error_message = "Watch child force-killed after timeout";
    }
    return true;
#else
    if (child_pid_ <= 0) {
        return true;
    }

    const pid_t pid = static_cast<pid_t>(child_pid_);
    int status = 0;

    // If already exited, reap immediately.
    if (::waitpid(pid, &status, WNOHANG) == pid) {
        child_pid_ = -1;
        return true;
    }

    ::kill(pid, SIGTERM);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(grace_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t rc = ::waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            child_pid_ = -1;
            return true;
        }
        if (rc < 0) {
            child_pid_ = -1;
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    ::kill(pid, SIGKILL);
    (void)::waitpid(pid, &status, 0);
    child_pid_ = -1;

    if (error_message) {
        *error_message = "Watch child force-killed after timeout";
    }
    return true;
#endif
}
