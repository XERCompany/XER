#pragma once

// win_job.h — process-group containment for XER child processes.
//
// A Windows Job Object groups child processes so they live/die with
// the parent: JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE reaps every assigned
// child the moment the last job handle closes — i.e. when the parent
// exits, crashes, or is killed from Task Manager. That is what keeps
// orphaned media workers / render workers / scan probes from holding
// build-output locks after the editor dies.
//
// Header-only (like env_utils.h): the Job handle is a per-process
// singleton created lazily on the first Assign(), so tools that never
// spawn children never create a job.

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <mutex>
#include <string>

namespace XER::Helper {

// WinJobGroup owns one Job Object for the lifetime of the process and
// assigns each directly-spawned child to it:
//
//   PROCESS_INFORMATION pi{};
//   if (CreateProcessW(..., &pi)) {
//       WinJobGroup::Shared().Assign(pi.hProcess);
//       ...
//   }
//
// Assign() never fails the spawn: a breakaway-protected child
// (ERROR_ACCESS_DENIED — it already lives in a job that disallows
// breakaway) or a child that already exited simply stays outside the
// group and runs ungrouped; the failure is logged to stderr.
//
// Do NOT assign:
//   * CEF/Chromium subprocesses — CEF manages its own job for
//     renderers/GPU children; nesting them is neither needed nor wanted.
//   * The crash-handler restart child (src/app/crash_handler.cc) — it
//     must OUTLIVE the dying parent to take over the session;
//     KILL_ON_JOB_CLOSE would reap it instantly.
//   * ShellExecute* children (explorer.exe, elevated helpers) — the
//     shell owns their lifetime and returns no usable process handle.
class WinJobGroup {
public:
    static WinJobGroup& Shared() {
        static WinJobGroup group;
        return group;
    }

    WinJobGroup(const WinJobGroup&) = delete;
    WinJobGroup& operator=(const WinJobGroup&) = delete;

    // Adds |child| (a process handle from CreateProcess*) to this
    // process's job. Returns true when the child is grouped; false
    // means it runs ungrouped (the reason was already logged).
    bool Assign(HANDLE child) {
        if (child == nullptr) {
            return false;
        }
        std::call_once(once_, [this] { Create(); });
        if (job_ == nullptr) {
            return false;
        }
        if (::AssignProcessToJobObject(job_, child) != FALSE) {
            return true;
        }
        const DWORD err = ::GetLastError();
        std::fprintf(stderr,
                     "WinJobGroup: AssignProcessToJobObject failed "
                     "(error %lu)%s — child left ungrouped\n",
                     static_cast<unsigned long>(err),
                     err == ERROR_ACCESS_DENIED
                         ? " [breakaway-protected child]"
                         : "");
        return false;
    }

    // The job handle (nullptr when job creation failed). Exposed for
    // diagnostics; callers must not close it.
    HANDLE handle() const { return job_; }

private:
    WinJobGroup() = default;
    ~WinJobGroup() {
        // Closing the last handle is what triggers KILL_ON_JOB_CLOSE —
        // at normal process exit this is exactly the desired "children
        // die with the parent" semantics; on abnormal exit the kernel
        // closes the handle for us.
        if (job_ != nullptr) {
            ::CloseHandle(job_);
        }
    }

    void Create() {
        // Per-process name: a shared global name would pool children
        // of independent instances into ONE job, and kill-on-close
        // only fires when the LAST handle to it closes — the first
        // dead parent would then leave its children orphaned anyway.
        const std::wstring name =
            L"XER.ChildGroup." +
            std::to_wstring(::GetCurrentProcessId());
        job_ = ::CreateJobObjectW(nullptr, name.c_str());
        if (job_ == nullptr) {
            std::fprintf(stderr,
                         "WinJobGroup: CreateJobObject failed "
                         "(error %lu) — children will run ungrouped\n",
                         static_cast<unsigned long>(::GetLastError()));
            return;
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
            JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
        if (::SetInformationJobObject(
                job_, JobObjectExtendedLimitInformation, &info,
                sizeof(info)) == FALSE) {
            std::fprintf(stderr,
                         "WinJobGroup: SetInformationJobObject failed "
                         "(error %lu) — children will run ungrouped\n",
                         static_cast<unsigned long>(::GetLastError()));
            ::CloseHandle(job_);
            job_ = nullptr;
        }
    }

    std::once_flag once_;
    HANDLE job_ = nullptr;
};

}  // namespace XER::Helper

#endif  // _WIN32
