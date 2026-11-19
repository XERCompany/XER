#ifndef XER_UI_CORE_RENDER_HOST_H
#define XER_UI_CORE_RENDER_HOST_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"

namespace XER::Cache {
class SharedMemoryChannel;
}

namespace XER::UI::Rendering {
class StreamRenderer;
}

namespace XER::UI::Core {

// Owns a pool of background xer_engine_ui_render.exe worker processes
// and the Cache::SharedMemoryChannel each window renders through.
//
// The UI system runs several render processes: each remote renderer
// owns one hidden worker process plus one channel, so N windows can
// render in N separate processes. CreateRemoteRenderer hands out a
// StreamRenderer bound to a fresh channel — hand it to
// Window::SetRenderer.
//
// Channel names: "xer.ui.render.<ui pid>.<channel id>".
class UiRenderHost {
public:
    static UiRenderHost& Instance();

    UiRenderHost(const UiRenderHost&) = delete;
    UiRenderHost& operator=(const UiRenderHost&) = delete;

    // Workers are per-channel: CreateRemoteRenderer spawns one hidden
    // process per remote renderer, so the pool grows on demand. This
    // call is a capacity hint / no-op that returns true when worker
    // spawning is possible on this platform.
    bool EnsureWorkers(int count, std::string* error_out,
                       const std::string& worker_arg = {});
    int WorkerCount() const;

    // Creates a channel on the next worker and returns the
    // StreamRenderer that feeds it. `target_hwnd` (optional, Win32
    // HANDLE value) makes the worker present directly into that
    // window's client area — process-separated rendering with pixels
    // still landing in the app window. Falls back to the worker's own
    // hidden surface when the foreign HWND is rejected.
    std::unique_ptr<Rendering::StreamRenderer> CreateRemoteRenderer(
        uintptr_t target_hwnd, std::string* error_out);

    // Sends kShutdown to every channel and lets workers exit, then
    // terminates stragglers. Called automatically at process exit;
    // apps may call it early to free GPU memory.
    void ShutdownAll();

private:
    UiRenderHost() = default;
    ~UiRenderHost();

    struct ChannelEntry {
        std::shared_ptr<Cache::SharedMemoryChannel> channel;
        void* worker_process = nullptr;  // HANDLE
        unsigned long worker_pid = 0;
    };

    std::string ExePath() const;
    bool SpawnWorker(const std::wstring& cmd, std::string* error_out);

    std::vector<ChannelEntry> channels_;
    int next_channel_id_ = 0;
};

}  // namespace XER::UI::Core

#endif  // XER_UI_CORE_RENDER_HOST_H
