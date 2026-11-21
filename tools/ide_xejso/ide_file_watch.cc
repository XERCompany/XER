// ide_file_watch.cc — see the header for the contract.
//
// Windows implementation: one overlapped ReadDirectoryChangesW loop on
// a dedicated thread. The wait blocks on TWO handles — the overlapped
// completion event and the stop event — so Stop() wakes the thread
// instantly instead of waiting for the next filesystem tick.
#include "ide_file_watch.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace XER::IdeXejso {

#if defined(_WIN32)
namespace {

std::wstring WideFromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1,
                                      nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    out.pop_back();
    return out;
}

std::string Utf8FromWide(const wchar_t* w, std::size_t chars) {
    if (w == nullptr || chars == 0) return {};
    const int n = WideCharToMultiByte(
        CP_UTF8, 0, w, static_cast<int>(chars), nullptr, 0, nullptr,
        nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(chars),
                        out.data(), n, nullptr, nullptr);
    // '\' -> '/' so consumers compare with '/'-separated rel paths.
    for (char& c : out) {
        if (c == '\\') c = '/';
    }
    return out;
}

}  // namespace
#endif  // _WIN32

IdeFileWatcher::IdeFileWatcher() = default;

IdeFileWatcher::~IdeFileWatcher() { Stop(); }

bool IdeFileWatcher::Watch(const std::string& dir, bool recursive) {
#if defined(_WIN32)
    Stop();
    if (dir.empty()) return false;

    // Open the directory SYNCHRONOUSLY on the calling thread: a bad
    // path fails here instead of on the watch thread, so the caller's
    // false return is meaningful. FILE_FLAG_OVERLAPPED is required for
    // the event-based wait the thread uses.
    const std::wstring wdir = WideFromUtf8(dir);
    HANDLE handle = CreateFileW(
        wdir.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    HANDLE stop_event =
        CreateEventW(nullptr, /*bManualReset=*/TRUE,
                     /*bInitialState=*/FALSE, nullptr);
    if (stop_event == nullptr) {
        CloseHandle(handle);
        return false;
    }

    dir_ = dir;
    recursive_ = recursive;
    {
        std::lock_guard<std::mutex> lk(pending_mutex_);
        pending_.clear();
    }
    dir_handle_ = handle;
    stop_event_ = stop_event;
    stop_.store(false);
    watching_.store(true);
    thread_ = std::thread([this] { ThreadMain(); });
    return true;
#else
    (void)dir;
    (void)recursive;
    return false;
#endif
}

void IdeFileWatcher::Stop() {
#if defined(_WIN32)
    stop_.store(true);
    if (stop_event_ != nullptr) {
        SetEvent(static_cast<HANDLE>(stop_event_));
    }
    // Break any pending synchronous read the thread may be sitting in
    // (the overlapped wait also wakes on stop_event_, so this only
    // covers the ReadDirectoryChangesW call window).
    if (dir_handle_ != nullptr) {
        CancelIoEx(static_cast<HANDLE>(dir_handle_), nullptr);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (dir_handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(dir_handle_));
        dir_handle_ = nullptr;
    }
    if (stop_event_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(stop_event_));
        stop_event_ = nullptr;
    }
    watching_.store(false);
    dir_.clear();
#else
    watching_.store(false);
#endif
}

void IdeFileWatcher::RecordChange(std::string rel_path) {
    std::lock_guard<std::mutex> lk(pending_mutex_);
    pending_[std::move(rel_path)] = std::chrono::steady_clock::now();
}

#if defined(_WIN32)
void IdeFileWatcher::ThreadMain() {
    HANDLE dir_handle = static_cast<HANDLE>(dir_handle_);
    HANDLE stop_event = static_cast<HANDLE>(stop_event_);
    HANDLE change_event =
        CreateEventW(nullptr, /*bManualReset=*/TRUE, FALSE, nullptr);
    if (change_event == nullptr) {
        watching_.store(false);
        return;
    }

    // 64 KiB — Microsoft's recommended max for RDCW on local volumes.
    alignas(DWORD) char buffer[64 * 1024];
    HANDLE wait_handles[2] = {change_event, stop_event};

    for (;;) {
        if (stop_.load()) break;
        OVERLAPPED ov{};
        ov.hEvent = change_event;
        ResetEvent(change_event);
        DWORD bytes = 0;
        const BOOL issued = ReadDirectoryChangesW(
            dir_handle, buffer, sizeof(buffer), recursive_,
            FILE_NOTIFY_CHANGE_FILE_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_SIZE |
                FILE_NOTIFY_CHANGE_CREATION,
            &bytes, &ov, nullptr);
        if (!issued) {
            break;  // handle died / cancelled — Stop() owns cleanup
        }
        const DWORD wait =
            WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        if (wait != WAIT_OBJECT_0) {
            // stop_event_ fired or the wait failed — exit promptly;
            // CancelIoEx from Stop() unwinds the pending read.
            CancelIoEx(dir_handle, &ov);
            break;
        }
        DWORD got = 0;
        if (!GetOverlappedResult(dir_handle, &ov, &got, FALSE)) {
            break;
        }
        if (got == 0) {
            // ERROR_NOTIFY_ENUM_DIR territory: the queue overflowed.
            // Report the wildcard — the panel treats "" as "reload
            // unconditionally" rather than dropping the save.
            RecordChange("");
            continue;
        }
        const auto* info =
            reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer);
        for (;;) {
            // FileNameLength is BYTES; the name is not NUL-terminated.
            RecordChange(Utf8FromWide(
                info->FileName, info->FileNameLength / sizeof(WCHAR)));
            if (info->NextEntryOffset == 0) break;
            info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                reinterpret_cast<const char*>(info) +
                info->NextEntryOffset);
        }
    }
    CloseHandle(change_event);
    watching_.store(false);
}
#endif  // _WIN32

std::vector<std::string> IdeFileWatcher::DrainChanges(
    unsigned debounce_ms) {
    std::vector<std::string> out;
    const auto now = std::chrono::steady_clock::now();
    const auto quiet = std::chrono::milliseconds(debounce_ms);
    std::lock_guard<std::mutex> lk(pending_mutex_);
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now - it->second >= quiet) {
            out.push_back(std::move(it->first));
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

}  // namespace XER::IdeXejso
