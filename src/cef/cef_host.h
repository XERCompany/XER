// cef_host.h — embeds a real Chromium browser into a native HWND.
//
// CefHost owns ONE CefBrowser parented to a caller HWND via
// CefWindowInfo::SetAsChild — Chromium renders into its own layered
// child window, so no custom painting is needed: resize the host and
// the page follows. Navigation callbacks let the UI track title/URL/
// loading state. Requires Cef::Initialize() to have succeeded; when the
// stub build (XER_HAS_CEF=0) is in effect Create() reports false.
//
// Threading: public methods are UI-thread calls. Internally all CEF
// objects are touched on CEF's UI thread via CefPostTask(TID_UI) — the
// multi_threaded_message_loop mode means callers never pump CEF.
#pragma once

#include <functional>
#include <memory>
#include <string>

#ifdef _WIN32
using CefHostNativeHandle = void*;   // HWND without dragging windows.h in
#else
using CefHostNativeHandle = void*;
#endif

namespace XER::Cef {

struct HostEvents {
    // Fired on CEF's UI thread — marshal to the app UI thread before
    // touching widgets (mirrors PreviewEngine::Sink's contract).
    std::function<void(const std::string&)> on_title;
    std::function<void(const std::string&)> on_url;
    std::function<void(bool loading)> on_loading;
    // Browser window closed/crashed — host becomes unusable.
    std::function<void()> on_closed;

    // Audio capture (CefAudioHandler). When on_audio_packet is set,
    // Chromium routes the browser's decoded audio to these callbacks
    // INSTEAD of the system output device — the browser is silent on
    // speakers and playback becomes the caller's job.
    // Nonzero audio_* fields are applied via GetAudioParameters.
    int audio_sample_rate = 0;        // Hz; 0 keeps CEF's default
    int audio_channel_layout = 0;     // cef_channel_layout_t; 0 = default
    int audio_frames_per_buffer = 0;  // 0 keeps CEF's default
    // Stream lifecycle — CEF UI thread.
    std::function<void(int sample_rate, int channels)> on_audio_started;
    // PCM packets — planar float32, one pointer per channel. Runs on a
    // realtime audio thread: no blocking/allocation; push into a FIFO.
    std::function<void(const float* const* data, int channels, int frames,
                       int64_t pts_ms)> on_audio_packet;
    std::function<void()> on_audio_stopped;
};

class CefHost final {
public:
    CefHost();
    ~CefHost();   // closes the browser (async) and detaches events
    CefHost(const CefHost&) = delete;
    CefHost& operator=(const CefHost&) = delete;

    // Parents a new Chromium browser under `parent`, sized to `rect`
    // (client coords), loading `url` (may be "about:blank"; HTML strings
    // can go through LoadHtml later). Events are invoked until Close().
    // Returns false when CEF is unavailable — check Cef::Available().
    bool Create(CefHostNativeHandle parent, int x, int y, int w, int h,
                const std::string& url, HostEvents events = {});

    void Resize(int x, int y, int w, int h);   // moves the child window
    void Show(bool visible);
    void LoadUrl(const std::string& url);
    // Convenience: data: URL — Chromium renders the markup directly.
    void LoadHtml(const std::string& html, const std::string& base_url =
                                              "https://xer.local/");
    void GoBack();
    void GoForward();
    void Reload();
    void Stop();
    void Focus();
    // DevTools in a popup window (remote inspector).
    void ShowDevTools();

    bool HasBrowser() const;
    std::string CurrentUrl() const;
    bool Loading() const;

    // Releases the browser (posts the close; the HWND child is removed
    // by Chromium). Idempotent; destructor calls it.
    void Close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::Cef
