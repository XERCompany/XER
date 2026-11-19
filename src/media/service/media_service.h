/**
 * MediaService — the media↔process boundary (design doc): media work
 * can run in-process (media service embedded) or in a dedicated media
 * process behind mojo-style remoting. This header is the seam:
 * MediaService owns factories and decides where each component lives.
 */
#pragma once

#include <memory>
#include <string>

namespace XER::Media {

class RendererFactory;
class Demuxer;

struct MediaServiceConfig {
    bool out_of_process = false;   // dedicated media process
    bool gpu_decoder = true;       // prefer hardware decode
    int io_threads = 2;            // demuxer I/O pool size
};

// Owns the process-local media runtime. Concrete factories are created
// through this seam so a remote (IPC) implementation can replace them
// without touching pipeline code.
class MediaService {
public:
    explicit MediaService(MediaServiceConfig config = {});
    ~MediaService() = default;
    MediaService(const MediaService&) = delete;
    MediaService& operator=(const MediaService&) = delete;

    const MediaServiceConfig& config() const { return config_; }

    // Process-wide accessor; embedded service is created on first use.
    static MediaService& Instance();

private:
    MediaServiceConfig config_;
};

}  // namespace XER::Media
