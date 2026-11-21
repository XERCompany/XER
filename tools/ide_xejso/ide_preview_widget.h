// ide_preview_widget.h — live CEF preview pane for IDEXEJSO.
//
// IdePreviewPanel hosts a real Chromium browser (src/cef CefHost,
// parented as a child HWND under the IDE window — the same embedding
// the video_editor Browser tab uses) and auto-reloads when the
// previewed file or one of its sibling web assets changes on disk
// (IdeFileWatcher + the widget's per-frame Tick drain).
//
// CEF is initialized LAZILY on the panel's first layout so a missing
// CEF install costs nothing at IDE startup; when the runtime is
// absent the panel degrades to an explanatory label instead of
// failing. Runtime notes for IDEXEJSO specifically:
//   * libcef.dll, the *.pak/icudtl.dat resources and
//     xer_browser_shell.exe must sit next to the exe — the shared
//     bin/ deploy step (video_editor_cef_deploy) already stages them;
//     the IDE target depends on it when present.
//   * disable_gpu=true is REQUIRED — the local CEF GPU child dies
//     silently otherwise (same as the editor's panel).
//   * multi_threaded_message_loop is already set inside
//     XER::Cef::Initialize — the app's Win32 loop needs no CEF pump.
//   * IDEXEJSO itself is never a CEF subprocess (children launch
//     xer_browser_shell.exe), so main() needs no RunSubprocessIfAny.
#pragma once

#include <string>

#if defined(_WIN32)
#include <memory>

#include "cef/cef_host.h"
#include "ide_file_watch.h"
#include "widgets/widget.h"
#endif

namespace XER::UI::Widgets {
class Button;
class Label;
}

namespace XER::IdeXejso {

#if defined(_WIN32)
class IdePreviewPanel final : public XER::UI::Widgets::Widget {
public:
    // Contract for IdeAppWidget wiring:
    //   preview_ = IdePreviewPanel::Create();   // may wrap in shared_ptr
    //   preview_->SetParentHwnd(hwnd_);
    //   AddChild(std::move(preview_));
    static std::unique_ptr<IdePreviewPanel> Create();
    ~IdePreviewPanel() override;

    // Top-level HWND the Chromium child is parented under. Call before
    // the first layout; afterwards a changed value recreates the
    // browser.
    void SetParentHwnd(void* hwnd);

    // Navigate the preview. If the browser doesn't exist yet the URL
    // is stashed and loaded right after Create (same pending-URL
    // pattern as the editor's panel).
    void SetUrl(const std::string& url);
    // Load a local file through file:// and start watching its
    // directory (recursive — css/js siblings count too). Empty path
    // clears the watch and shows about:blank.
    void SetFile(const std::string& path);
    // Cache-busting reload (watcher notifications call this). CEF's
    // CefHost only exposes a cached Reload(), so file:/http(s): URLs
    // are re-navigated with a churning __xer_r= query parameter —
    // other schemes (xer-ext:, data:) reload as-is.
    void Reload();
    // false once CEF is known-broken (not compiled in, or init/create
    // failed): the panel renders an error label instead of a browser.
    // Before the lazy first-init attempt it reports the compiled-in
    // capability.
    bool Available() const;

    // The file currently being previewed ('' when SetUrl was used).
    const std::string& File() const { return file_path_; }

    // ── Widget contract ──────────────────────────────────────────
    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;
    XER::UI::Layout::Size GetMinSize() const override {
        return {160.0f, 100.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {420.0f, 560.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {8192.0f, 8192.0f};
    }
    // Shadow (base is non-virtual): toggles the Chromium child HWND
    // alongside the widget flag — call through the derived pointer.
    void SetVisible(bool visible);

private:
    IdePreviewPanel() = default;

    void EnsureBrowser();
    void SyncBrowserRect();
    // view_rect_ (DIPs) -> browser child HWND rect in parent client
    // pixels: clipped to the visible rect and scaled by window DPI —
    // mirrors BrowserPanelWidget::BrowserRectPx.
    struct PxRect {
        int x, y, w, h;
    };
    PxRect BrowserRectPx() const;
    bool IsPreviewAsset(const std::string& rel_change) const;

    // CEF-thread event mailbox — callbacks hold a weak_ptr here so a
    // callback in flight during teardown resolves to nothing.
    struct CefEvents;
    std::shared_ptr<CefEvents> cef_events_;

    void* parent_hwnd_ = nullptr;
    std::shared_ptr<XER::UI::Widgets::Button> reload_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> url_label_;
    std::shared_ptr<XER::UI::Widgets::Label> status_label_;
    std::unique_ptr<XER::Cef::CefHost> cef_;
    XER::UI::Layout::Rect view_rect_{};
    std::string status_;          // UI thread only
    std::string current_url_;     // clean URL, never carries __xer_r
    std::string pending_url_;     // arrived before the browser existed
    bool cef_tried_ = false;
    bool cef_ok_ = false;

    // Live-reload state (SetFile only).
    IdeFileWatcher watcher_;
    std::string file_path_;       // normalized absolute, UTF-8
    std::string file_leaf_lc_;    // lowercase leaf for compare
    unsigned reload_seq_ = 0;
};
#endif  // _WIN32

// Headless verification called by RunSelftest (--selftest):
//   * watches a scratch dir in %TEMP%, writes a file, waits up to ~2s
//     for the debounced drain to surface it — hard failure if not;
//   * then reports whether CEF initializes headless on this machine:
//     a WS_POPUP parent + about:blank CefHost (the proven
//     `video_editor --cef-check` recipe — works without a visible
//     window). A missing/stub CEF is reported "skipped"/"unavailable"
//     in `report`, it does NOT fail the selftest.
// Returns true when the watcher works (the only thing this test can
// prove deterministically); `report` always receives the full log.
bool IdePreviewSelftest(std::string* report);

}  // namespace XER::IdeXejso
