#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "video_audio_player.h"
#include "video_command_buffer.h"
#include "video_frame.h"
#include "video_frame_cache.h"
#include "video_info.h"
#include "video_session.h"

namespace XER::Content::Video {

// VideoEngine — the whole preview subsystem facade. Composes:
//
//   VideoSession      decode worker + transport + PTS position
//   VideoCommandBuffer async event queue (frames coalesced, state, errors)
//   VideoAudioPlayer  audio-track decode → own WASAPI render → A/V sync
//   VideoFrameCache   memory + on-disk poster cache for instant re-render
//
// Usage: Open() once, Tick() every UI frame, transport calls anytime.
// All getters are cheap atomic/mutex reads safe from the UI thread.
class VideoEngine {
public:
    VideoEngine();
    ~VideoEngine();

    VideoEngine(const VideoEngine&) = delete;
    VideoEngine& operator=(const VideoEngine&) = delete;

    // Probes synchronously, then starts decode + audio extraction.
    // Returns false only when the container probe fails.
    bool Open(const std::string& path, std::string* error_out = nullptr);
    void Close();
    bool IsOpen() const { return session_.IsOpen(); }

    // Transport — UI thread.
    void Play();
    void Pause();
    void Stop();
    void Seek(double seconds);
    void SetLoop(bool loop);

    // Per-UI-frame pump: drains the command buffer, updates the mirrored
    // state, attaches audio when ready, stores the poster frame once,
    // and drift-corrects A/V sync. Call from the widget Tick.
    void Tick();

    // State — consistent as of the last Tick().
    VideoState GetState() const { return ui_state_; }
    double GetPosition() const;
    double GetDuration() const;
    double GetFps() const;
    int GetWidth() const;
    int GetHeight() const;
    bool HasAudio() const;
    bool AudioReady() const;
    bool IsPlaying() const;
    const VideoInfo& GetInfo() const { return session_.GetInfo(); }

    // Newest decoded frame for drawing (nullptr until first decode).
    // Dedup via GetFrameSequence().
    VideoFramePtr GetLatestFrame() const;
    std::uint64_t GetFrameSequence() const { return frame_seq_; }

    // Human-readable status for the widget's status line.
    std::string GetStatusText() const;

    // Frame cache access: RecallFrame() returns the in-memory last frame
    // for a path (instant tab-switch-back); GetCachedPoster() loads the
    // persisted poster still from disk (instant first paint on reopen).
    VideoFramePtr RecallFrame(const std::string& path) const;
    VideoFramePtr GetCachedPoster(const std::string& path) const;

private:
    void HandleCommands();

    VideoSession session_;
    VideoCommandBuffer commands_;
    VideoAudioPlayer audio_;
    VideoFrameCache frame_cache_;

    // UI-side mirror — written only by Tick() on the UI thread.
    VideoFramePtr latest_frame_;
    VideoState ui_state_ = VideoState::kIdle;
    std::string status_text_;
    std::uint64_t frame_seq_ = 0;
    bool audio_ready_ = false;
    bool audio_failed_ = false;
    bool audio_attached_ = false;
    bool want_playing_ = false;
    bool poster_stored_ = false;
    std::string current_path_;
    std::vector<VideoCommand> drain_scratch_;
};

}  // namespace XER::Content::Video
