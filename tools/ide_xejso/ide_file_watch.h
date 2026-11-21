// ide_file_watch.h — in-process directory watcher for the IDE's live
// preview pane.
//
// One background thread per watcher drives ReadDirectoryChangesW
// (subtree-capable) and folds every notification into a per-file
// "last changed" timestamp. The UI side calls DrainChanges() from the
// widget Tick — the ~30fps frame callback the Window host already
// pumps — which returns only paths quiet for at least `debounce_ms`,
// so an editor save burst (create + write + rename) collapses into a
// single reload signal. No callback ever fires on the watch thread
// into widget code; the mailbox pattern is the same one
// BrowserPanelWidget uses for CEF UI-thread events.
//
// Compiles on every platform: the non-Windows half is an inert stub
// (Watch reports false) so headless/CI presets keep linking — the
// live preview is a Windows-only feature like the rest of the IDE UI.
#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace XER::IdeXejso {

class IdeFileWatcher {
public:
    // Quiet period a path must reach before DrainChanges releases it.
    static constexpr unsigned kDefaultDebounceMs = 300;

    IdeFileWatcher();
    ~IdeFileWatcher();
    IdeFileWatcher(const IdeFileWatcher&) = delete;
    IdeFileWatcher& operator=(const IdeFileWatcher&) = delete;

    // Starts watching `dir` (UTF-8, may be relative — resolved at
    // call time). Replaces any previous watch. `recursive` includes
    // subdirectories (extension pages pull css/js from subfolders).
    // Returns false when the directory cannot be opened or the
    // platform has no watcher — callers treat that as "preview won't
    // auto-reload", never as fatal.
    bool Watch(const std::string& dir, bool recursive = true);
    void Stop();
    bool Watching() const { return watching_.load(); }
    const std::string& Dir() const { return dir_; }

    // UI-thread drain. Returns UTF-8 paths RELATIVE to the watched
    // dir ('/'-separated) whose last change is at least `debounce_ms`
    // old; each path is returned once per quiet period. An empty
    // string in the result means "notification overflow — something
    // changed but we can't say what" and should be treated as a
    // match-everything signal.
    std::vector<std::string> DrainChanges(
        unsigned debounce_ms = kDefaultDebounceMs);

private:
    void ThreadMain();
    void RecordChange(std::string rel_path);

    std::string dir_;
    bool recursive_ = true;

    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> watching_{false};
    // Win32 handles kept as void* so the header stays platform-clean:
    // stop_event_ signals the watch thread to exit, dir_handle_ is the
    // overlapped directory handle ReadDirectoryChangesW consumes.
    void* stop_event_ = nullptr;
    void* dir_handle_ = nullptr;

    // path (relative, '/'-separated) -> timestamp of the most recent
    // notification. Last-write-wins so bursts collapse to one entry.
    std::mutex pending_mutex_;
    std::unordered_map<std::string,
                       std::chrono::steady_clock::time_point>
        pending_;
};

}  // namespace XER::IdeXejso
