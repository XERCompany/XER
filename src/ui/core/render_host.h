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
class RenderStore;
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

    // Pre-spawns |count| hidden worker processes, each bound to its own
    // channel. Called at Application::Initialize so the first
    // CreateRemoteRenderer is instant; extra renderers spawn on demand.
    bool EnsureWorkers(int count, std::string* error_out,
                       const std::string& worker_arg = {});
    // Live workers (process handles still running).
    int WorkerCount() const;
    // Pool workers that are spawned but not checked out yet.
    int WarmWorkerCount() const;
    // Reaps finished worker handles; warm slots that lost their process
    // are respawned so the pool stays at its configured size. Returns
    // the number of processes restarted.
    int ReapAndRespawn();

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

    // ---- shared resource store ----------------------------------
    // Named shared-memory segment (xer.ui.res.<pid>) carrying pixel
    // payloads too large for the command channel: decoded images,
    // live video frames, audio levels. Created lazily on first write
    // (or via Store()); workers attach through kAttachStore.
    Rendering::RenderStore* Store();
    // Uploads a static BGRA image under |id| — workers draw it via
    // DrawResource(id, ...). Rewrites replace the payload.
    bool UploadImage(std::uint32_t id, const void* bgra, int width,
                     int height);
    // Writes one live frame into the kVideoFrame slot |id|; widgets
    // repaint it via DrawVideoFrame(id, ...) every frame.
    bool WriteVideoFrame(std::uint32_t id, const void* bgra, int width,
                         int height);
    // Publishes float levels (0..1) for DrawSpectrum widgets.
    bool WriteSpectrum(std::uint32_t id, const float* levels,
                       std::uint32_t count);
    // Frees the slot and broadcasts kFreeResource so every worker
    // drops its GPU bitmap for the id.
    void FreeResource(std::uint32_t id);

private:
    UiRenderHost() = default;
    ~UiRenderHost();

    struct ChannelEntry {
        std::shared_ptr<Cache::SharedMemoryChannel> channel;
        void* worker_process = nullptr;  // HANDLE
        unsigned long worker_pid = 0;
        bool in_use = false;             // checked out by a renderer
        int channel_id = -1;
    };

    std::string ExePath() const;
    bool SpawnWorker(const std::wstring& cmd, std::string* error_out,
                     ChannelEntry* entry);
    // Creates channel + hidden worker for entry (no --hwnd: the worker
    // retargets via kSetTargetHwnd on checkout).
    bool SpawnPooledWorker(ChannelEntry* entry, std::string* error_out);
    void CloseWorkerHandle(ChannelEntry* entry);
    // StreamRenderer teardown hook: clears the entry's checkout so the
    // worker returns to the warm pool (retargeted onto its hidden
    // surface), or reaps it when the process died while checked out.
    void ReleaseChannel(int channel_id);
    void Broadcast(const std::vector<std::uint8_t>& msg);

    std::vector<ChannelEntry> channels_;
    int next_channel_id_ = 0;
    int warm_target_ = 0;
    std::string worker_arg_;  // extra arg from EnsureWorkers
    std::unique_ptr<Rendering::RenderStore> store_;
};

}  // namespace XER::UI::Core

#endif  // XER_UI_CORE_RENDER_HOST_H
