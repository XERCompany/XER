// media_worker_client.h — parent-side driver for the media-decode
// worker subprocess (Windows-only today). Spawns the dedicated
// XERVideoLoader.exe from the editor's own directory when present,
// falling back to self-spawning `video_editor --media-worker` on dev
// builds that lack it. MediaPrep routes its poster/peaks decodes
// through Shared() so a crashing decoder (malformed files, FFmpeg
// edge cases) kills the WORKER, not the editor; every call falls
// back to the in-process path on spawn/timeout/death.
//
// Env switches:  XER_VE_MEDIA_WORKER=0  — force off (in-process only)
//                XER_VE_MEDIA_WORKER=1  — force on (default when unset)
#pragma once

#include <cstdint>
#include <string>

#include "media_catalog.h"   // AudioPeaks

namespace video { class Frame; }

namespace XER::VideoEditor {

class MediaWorkerClient {
public:
    static MediaWorkerClient& Shared();

    MediaWorkerClient(const MediaWorkerClient&) = delete;
    MediaWorkerClient& operator=(const MediaWorkerClient&) = delete;

    // false → caller runs the in-process decode (env kill-switch,
    // spawn failure, or the worker died too many times).
    bool Available();

    // Synchronous request/response with a timeout. false → in-process
    // fallback (timeout, worker death, malformed reply).
    bool PosterAt(const std::string& path, double seconds,
                  video::Frame* out);
    bool Peaks(const std::string& path, AudioPeaks* out);

    // Liveness probe (no media decode). false → worker dead/absent.
    // role_out, when non-null, receives the worker's role string —
    // "XERVideoLoader" for the dedicated exe, "media-worker" for the
    // self-spawn fallback — so callers can verify which binary was
    // actually spawned.
    bool Ping(std::string* role_out = nullptr);

private:
    MediaWorkerClient() = default;
    ~MediaWorkerClient();   // kills + closes the child

    struct Impl;
    Impl* impl_ = nullptr;  // pimpl — owns the Win32 handles
};

}  // namespace XER::VideoEditor
