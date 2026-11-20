// media_worker_client.h — parent-side driver for the `--media-worker`
// subprocess (Windows-only today). MediaPrep routes its poster/peaks
// decodes through Shared() so a crashing decoder (malformed files,
// FFmpeg edge cases) kills the WORKER, not the editor; every call
// falls back to the in-process path on spawn/timeout/death.
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
    bool AudioPeaks(const std::string& path, AudioPeaks* out);

private:
    MediaWorkerClient() = default;
    ~MediaWorkerClient();   // kills + closes the child

    struct Impl;
    Impl* impl_ = nullptr;  // pimpl — owns the Win32 handles
};

}  // namespace XER::VideoEditor
