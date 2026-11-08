#include "watch/watch_recompile_service.h"

#include "error_handler/err_capture.h"
#include "watch/watch_lock_recovery.h"

#include <filesystem>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <thread>

namespace {

std::string CurrentExecutablePath() {
    char buf[PATH_MAX];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return "./build-copilot/EngineBuilder";
    }
    buf[n] = '\0';
    return std::string(buf);
}

}  // namespace

WatchRecompileService::WatchRecompileService() = default;

WatchRecompileService::~WatchRecompileService() {
    Stop();
}

bool WatchRecompileService::Recompile(const std::string& script_path,
                                      std::string* error_message) const {
    if (child_pid_ > 0) {
        if (!const_cast<WatchRecompileService*>(this)->StopChild(800, error_message)) {
            return false;
        }
    }

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
}

bool WatchRecompileService::StopChild(int grace_ms,
                                      std::string* error_message) {
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
}
