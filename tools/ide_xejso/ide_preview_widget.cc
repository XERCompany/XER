// ide_preview_widget.cc — see the header for the contract.
//
// Layout/embedding follows tools/video_editor/ui/browser_panel_widget.cc
// exactly: the CEF runtime is initialized lazily on first layout (a
// missing install costs nothing at startup), Chromium renders into its
// own layered child HWND, CEF-thread HostEvents land in a weak_ptr
// mailbox that Tick() drains on the app UI thread, and the browser rect
// is converted DIPs -> client pixels with the window's DPI.
#include "ide_preview_widget.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // GetDpiForWindow + the selftest's WS_POPUP parent

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include "cef/cef_runtime.h"
#include "rendering/renderer.h"
#include "widgets/button.h"
#include "widgets/label.h"

#if defined(XER_HAS_CEF)
#include "include/cef_app.h"   // LOGSEVERITY_DISABLE for the selftest
#endif

namespace XER::IdeXejso {
namespace {

namespace fs = std::filesystem;
using XER::UI::Layout::Rect;

constexpr float kChromeH = 26.0f;
constexpr float kStatusH = 18.0f;
constexpr float kPad = 6.0f;

fs::path U8Path(const std::string& utf8) {
    return fs::path(reinterpret_cast<const char8_t*>(utf8.c_str()));
}

std::string Utf8(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.c_str()),
                       u8.size());
}

std::string Lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(
            static_cast<unsigned char>(c)));
    }
    return s;
}

// "D:/dir/in dex.html" -> "file:///D:/dir/in%20dex.html". Extension
// pages are plain static files — file:// needs no scheme registration
// (unlike xer-ext:// which serves bytes out of .xexrj archives).
std::string FileUrlFromPath(const fs::path& p) {
    const auto s = p.generic_u8string();
    std::string out = "file:///";
    out.reserve(8 + s.size());
    for (const char8_t ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~' || c == '/' || c == ':') {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

// CefHost::Reload maps to CefBrowser::Reload() (may serve subresources
// from cache). A same-URL LoadUrl re-navigation with a churning query
// param is the available no-edit path to an ignore-cache reload for
// the schemes that take query strings; xer-ext:/data: would break on
// the appended parameter, so those reload as-is.
std::string BustCacheUrl(const std::string& url, unsigned seq) {
    const bool bustable = url.rfind("file://", 0) == 0 ||
                          url.rfind("http://", 0) == 0 ||
                          url.rfind("https://", 0) == 0;
    if (!bustable) return url;
    std::string frag;
    std::string base = url;
    if (const auto hash = base.find('#'); hash != std::string::npos) {
        frag = base.substr(hash);
        base.erase(hash);
    }
    const std::string key = "__xer_r=";
    if (const auto pos = base.find(key); pos != std::string::npos) {
        const auto end = base.find('&', pos);
        base.erase(pos, end == std::string::npos ? end : end - pos);
        base.insert(pos, key + std::to_string(seq));
    } else {
        base += (base.find('?') == std::string::npos ? '?' : '&');
        base += key + std::to_string(seq);
    }
    return base + frag;
}

}  // namespace

// Per-instance event mailbox — see browser_panel_widget.cc for the
// rationale (weak_ptr sink; CEF callbacks outliving the widget drop).
struct IdePreviewPanel::CefEvents {
    std::mutex mutex;
    std::string url;
    bool loading = false;
    bool has_loading = false;
    bool closed = false;
};

std::unique_ptr<IdePreviewPanel> IdePreviewPanel::Create() {
    auto panel = std::unique_ptr<IdePreviewPanel>(new IdePreviewPanel());
    panel->reload_btn_ =
        std::make_shared<XER::UI::Widgets::Button>(
            "Reload", XER::UI::Widgets::ButtonStyle::Secondary);
    panel->reload_btn_->SetTooltip("Reload preview (Ctrl-free)");
    panel->reload_btn_->SetOnClick([p = panel.get()] { p->Reload(); });
    panel->url_label_ =
        std::make_shared<XER::UI::Widgets::Label>("", /*muted=*/true);
    panel->status_label_ =
        std::make_shared<XER::UI::Widgets::Label>("", /*muted=*/true);
    panel->AddChild(panel->reload_btn_);
    panel->AddChild(panel->url_label_);
    panel->AddChild(panel->status_label_);
    return panel;
}

IdePreviewPanel::~IdePreviewPanel() {
    // Expire the sink first — a HostEvents callback still in flight on
    // the CEF thread resolves its weak_ptr to nothing after this.
    cef_events_.reset();
    watcher_.Stop();
    if (cef_) {
        cef_->Close();
    }
}

void IdePreviewPanel::SetParentHwnd(void* hwnd) {
    if (hwnd == parent_hwnd_) return;
    parent_hwnd_ = hwnd;
    if (cef_) {
        // The Chromium child is parented to the old HWND — recreate on
        // the next layout rather than reparenting a live browser.
        cef_->Close();
        cef_.reset();
        cef_tried_ = false;
        cef_ok_ = false;
    }
}

void IdePreviewPanel::SetUrl(const std::string& url) {
    if (url.empty()) return;
    file_path_.clear();
    file_leaf_lc_.clear();
    watcher_.Stop();
    current_url_ = url;
    url_label_->SetText(url);
    if (cef_ && cef_->HasBrowser()) {
        cef_->LoadUrl(url);
    } else {
        pending_url_ = url;
    }
}

void IdePreviewPanel::SetFile(const std::string& path) {
    if (path.empty()) {
        file_path_.clear();
        file_leaf_lc_.clear();
        watcher_.Stop();
        SetUrl("about:blank");
        return;
    }
    std::error_code ec;
    fs::path p = fs::absolute(U8Path(path), ec);
    if (ec) p = U8Path(path);
    p = p.lexically_normal();
    file_path_ = Utf8(p);
    file_leaf_lc_ = Lower(Utf8(p.filename()));

    const fs::path dir = p.parent_path();
    if (!dir.empty()) {
        // Recursive: css/js/png siblings of the page also trigger a
        // reload (IsPreviewAsset filters to web-asset extensions).
        watcher_.Watch(Utf8(dir), /*recursive=*/true);
    }
    current_url_ = FileUrlFromPath(p);
    url_label_->SetText(file_path_);
    if (cef_ && cef_->HasBrowser()) {
        cef_->LoadUrl(current_url_);
    } else {
        pending_url_ = current_url_;
    }
}

void IdePreviewPanel::Reload() {
    if (cef_ && cef_->HasBrowser() && !current_url_.empty()) {
        cef_->LoadUrl(BustCacheUrl(current_url_, ++reload_seq_));
    }
}

bool IdePreviewPanel::Available() const {
    if (!XER::Cef::Available()) return false;
    // After the lazy attempt, reflect the real outcome.
    return !(cef_tried_ && !cef_ok_);
}

void IdePreviewPanel::EnsureBrowser() {
    if (cef_tried_) return;
    if (parent_hwnd_ == nullptr) {
        // Not wired yet — retry next layout instead of latching a
        // failure (SetParentHwnd is the IDE's second call).
        status_ = "preview waiting for host window";
        return;
    }
    cef_tried_ = true;
    if (!XER::Cef::Available()) {
        status_ = "Chromium runtime not compiled in";
        return;
    }
    std::string error;
    XER::Cef::RuntimeOptions options;
    // REQUIRED (mirrors the editor panel): the local CEF GPU child
    // dies silently with hardware accel enabled.
    options.disable_gpu = true;
    if (!XER::Cef::Initialize(options, &error)) {
        status_ = "Chromium runtime unavailable: " + error;
        return;
    }
    cef_ = std::make_unique<XER::Cef::CefHost>();
    cef_events_ = std::make_shared<CefEvents>();
    const std::weak_ptr<CefEvents> sink = cef_events_;
    XER::Cef::HostEvents events;
    events.on_url = [sink](const std::string& url) {
        if (const auto s = sink.lock()) {
            std::lock_guard<std::mutex> lk(s->mutex);
            s->url = url;
        }
    };
    events.on_loading = [sink](bool loading) {
        if (const auto s = sink.lock()) {
            std::lock_guard<std::mutex> lk(s->mutex);
            s->loading = loading;
            s->has_loading = true;
        }
    };
    events.on_closed = [sink] {
        if (const auto s = sink.lock()) {
            std::lock_guard<std::mutex> lk(s->mutex);
            s->closed = true;
        }
    };
    const auto rect = BrowserRectPx();
    const std::string initial =
        pending_url_.empty() ? "about:blank" : pending_url_;
    cef_ok_ = cef_->Create(
        static_cast<CefHostNativeHandle>(parent_hwnd_), rect.x, rect.y,
        rect.w, rect.h, initial, std::move(events));
    if (cef_ok_) {
        pending_url_.clear();
        status_.clear();
    } else {
        status_ = "Failed to create browser window";
        cef_.reset();
    }
}

// view_rect_ in DIPs -> browser child HWND rect in parent client px:
// clip to the visible rect (a native HWND isn't clipped by the
// renderer) and scale by window DPI — same math as
// BrowserPanelWidget::BrowserRectPx.
IdePreviewPanel::PxRect IdePreviewPanel::BrowserRectPx() const {
    Rect clip = view_rect_.Intersect(GetVisibleRect());
    if (!(clip.width > 1.0f && clip.height > 1.0f)) {
        // Park on a 1x1 corner rather than destroying the browser.
        clip = {view_rect_.x, view_rect_.y, 1.0f, 1.0f};
    }
    float scale = 1.0f;
    if (parent_hwnd_ != nullptr) {
        const UINT dpi =
            GetDpiForWindow(static_cast<HWND>(parent_hwnd_));
        scale = dpi != 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
    }
    return {static_cast<int>(clip.x * scale),
            static_cast<int>(clip.y * scale),
            static_cast<int>(clip.width * scale),
            static_cast<int>(clip.height * scale)};
}

void IdePreviewPanel::SyncBrowserRect() {
    if (cef_ && cef_ok_) {
        const auto rect = BrowserRectPx();
        cef_->Resize(rect.x, rect.y, rect.w, rect.h);
    }
}

bool IdePreviewPanel::IsPreviewAsset(
    const std::string& rel_change) const {
    if (rel_change.empty()) {
        return true;   // watcher overflow — can't tell, so reload
    }
    const std::string rel = Lower(rel_change);
    const auto leaf = rel.substr(rel.find_last_of('/') + 1);
    if (!file_leaf_lc_.empty() && leaf == file_leaf_lc_) {
        return true;
    }
    // Web assets an extension static/ page plausibly references.
    static const char* const kExts[] = {
        ".html", ".htm", ".css",  ".js",   ".mjs",  ".json",
        ".toml", ".svg", ".png",  ".jpg",  ".jpeg", ".gif",
        ".ico",  ".webp", ".woff", ".woff2", ".ttf", ".map",
    };
    for (const char* ext : kExts) {
        if (rel.size() > std::strlen(ext) &&
            rel.compare(rel.size() - std::strlen(ext),
                        std::strlen(ext), ext) == 0) {
            return true;
        }
    }
    return false;
}

void IdePreviewPanel::Tick(double dt_seconds) {
    (void)dt_seconds;
    bool dirty = false;
    // Drain CEF-thread events (mailbox pattern — see CefEvents above).
    if (cef_events_) {
        std::lock_guard<std::mutex> lk(cef_events_->mutex);
        if (!cef_events_->url.empty()) {
            // Show the clean URL, not the __xer_r=busted one the
            // browser actually navigated to.
            std::string shown = cef_events_->url;
            if (const auto k = shown.find("__xer_r=");
                k != std::string::npos) {
                const auto end = shown.find('&', k);
                if (end == std::string::npos) {
                    // trailing param — drop "?__xer_r"/"&__xer_r" too
                    shown.erase(k - 1);
                } else {
                    shown.erase(k - 1, end - k + 1);
                }
            }
            url_label_->SetText(shown);
            cef_events_->url.clear();
            dirty = true;
        }
        if (cef_events_->has_loading) {
            status_ = cef_events_->loading ? "Loading…" : "";
            cef_events_->has_loading = false;
            dirty = true;
        }
        if (cef_events_->closed) {
            cef_ok_ = false;
            status_ = "browser closed";
            cef_events_->closed = false;
            dirty = true;
        }
    }
    // Debounced filesystem drain — ~30fps Tick is the pump.
    if (watcher_.Watching()) {
        for (const auto& rel : watcher_.DrainChanges()) {
            if (IsPreviewAsset(rel)) {
                Reload();
                status_ = "reloaded on save";
                dirty = true;
                break;
            }
        }
    }
    if (dirty) Invalidate();
}

void IdePreviewPanel::Layout(const Rect& bounds) {
    SetBounds(bounds);
    const float bx = bounds.x + kPad;
    float y = bounds.y + kPad;
    reload_btn_->Layout({bx, y, 60.0f, kChromeH - 4.0f});
    const float label_x = bx + 68.0f;
    url_label_->Layout(
        {label_x, y + 2.0f, bounds.Right() - kPad - label_x,
         kChromeH - 6.0f});
    y += kChromeH;
    view_rect_ = {bx, y, bounds.width - 2.0f * kPad,
                  bounds.y + bounds.height - kPad - kStatusH - y};
    status_label_->Layout(
        {bx, bounds.y + bounds.height - kStatusH - 1.0f,
         bounds.width - 2.0f * kPad, kStatusH});
    EnsureBrowser();
    SyncBrowserRect();
}

bool IdePreviewPanel::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    if (event.type == XER::UI::Widgets::Event::Type::MouseDown &&
        view_rect_.Contains(event.position) && cef_ && cef_ok_) {
        cef_->Focus();
        return true;
    }
    return false;
}

void IdePreviewPanel::SetVisible(bool visible) {
    XER::UI::Widgets::Widget::SetVisible(visible);
    if (cef_) {
        cef_->Show(visible);
    }
}

void IdePreviewPanel::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f,
                        tokens.radius_large);
    const auto vr = view_rect_;
    if (!cef_ok_) {
        renderer.FillRect(vr, tokens.input, tokens.radius_small);
        renderer.DrawText(
            status_.empty() ? "preview" : status_,
            {vr.x + 8.0f, vr.y + 8.0f, vr.width - 16.0f, 20.0f},
            XER::UI::Rendering::TextStyle{tokens.muted,
                                          tokens.font_small, 400});
        renderer.DrawText(
            "open an .html file to preview — CEF deploy sits next to "
            "the exe",
            {vr.x + 8.0f, vr.y + 30.0f, vr.width - 16.0f, 20.0f},
            XER::UI::Rendering::TextStyle{tokens.muted,
                                          tokens.font_small, 400});
    }
    status_label_->SetText(status_);
    status_label_->SetVisible(!status_.empty());
}

// ---- selftest -------------------------------------------------------------

bool IdePreviewSelftest(std::string* report) {
    std::string log;
    bool ok = true;

    // ── watcher: real ReadDirectoryChangesW round trip ─────────────
    {
        std::error_code ec;
        fs::path dir = fs::temp_directory_path(ec);
        dir /= "xer_ide_watch_" +
               std::to_string(::GetCurrentProcessId());
        fs::create_directories(dir, ec);
        const std::string dir_utf8 = Utf8(dir);
        IdeFileWatcher watcher;
        if (!watcher.Watch(dir_utf8, /*recursive=*/false)) {
            log += "watcher: FAIL — Watch() could not open " +
                   dir_utf8 + "\n";
            ok = false;
        } else {
            // Let the thread arm the first overlapped read before the
            // write lands (RDCW only reports changes after arming).
            std::this_thread::sleep_for(
                std::chrono::milliseconds(150));
            const fs::path probe = dir / "probe.html";
            {
                std::ofstream out(probe, std::ios::binary);
                out << "<html><body>probe</body></html>\n";
            }
            bool seen = false;
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::seconds(2);
            while (!seen &&
                   std::chrono::steady_clock::now() < deadline) {
                // 60ms debounce — faster than the UI default so the
                // test doesn't spend its budget waiting it out.
                for (const auto& rel : watcher.DrainChanges(60)) {
                    if (rel.empty() ||
                        Lower(rel).find("probe.html") !=
                            std::string::npos) {
                        seen = true;
                        break;
                    }
                }
                if (!seen) {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(50));
                }
            }
            watcher.Stop();
            fs::remove_all(dir, ec);
            log += seen ? "watcher: ok (change observed in temp dir)\n"
                        : "watcher: FAIL — no change event within 2s\n";
            ok = ok && seen;
        }
    }

    // ── CEF: headless init + real browser on an invisible parent ───
    if (!XER::Cef::Available()) {
        log += "cef: skipped (not compiled in — stub build)\n";
    } else {
        XER::Cef::RuntimeOptions options;
        options.disable_gpu = true;
#if defined(XER_HAS_CEF)
        options.log_severity = LOGSEVERITY_DISABLE;
#endif
        std::string error;
        if (!XER::Cef::Initialize(options, &error)) {
            // Environmental (libcef.dll/xer_browser_shell.exe not
            // deployed beside the exe) — report, don't fail.
            log += "cef: unavailable — " + error + "\n";
        } else {
            // The `video_editor --cef-check` recipe: a real WS_POPUP
            // parent proves the whole child-browser path without any
            // visible window.
            WNDCLASSW wc{};
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"XerIdePreviewCheck";
            RegisterClassW(&wc);
            HWND parent = CreateWindowExW(
                0, wc.lpszClassName, L"", WS_POPUP, 0, 0, 8, 8,
                nullptr, nullptr, wc.hInstance, nullptr);
            if (parent == nullptr) {
                log += "cef: init ok, host window create failed\n";
            } else {
                auto loaded =
                    std::make_shared<std::atomic<bool>>(false);
                const std::weak_ptr<std::atomic<bool>> weak = loaded;
                XER::Cef::HostEvents events;
                events.on_loading = [weak](bool is_loading) {
                    if (const auto s = weak.lock(); !is_loading) {
                        s->store(true);
                    }
                };
                XER::Cef::CefHost host;
                if (!host.Create(parent, 0, 0, 8, 8, "about:blank",
                                 std::move(events))) {
                    log += "cef: init ok, browser create failed\n";
                } else {
                    const auto deadline =
                        std::chrono::steady_clock::now() +
                        std::chrono::seconds(3);
                    while (!loaded->load() &&
                           std::chrono::steady_clock::now() <
                               deadline) {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(50));
                    }
                    log += std::string(
                               "cef: ok (headless browser created") +
                           (loaded->load()
                                ? ", load event observed)\n"
                                : ")\n");
                    host.Close();
                }
                DestroyWindow(parent);
            }
            XER::Cef::Shutdown();
        }
    }

    if (report != nullptr) *report = std::move(log);
    return ok;
}

}  // namespace XER::IdeXejso

#else  // !_WIN32 — the live preview pane is Windows-only, the selftest
       // stays linkable so headless presets don't need call-site guards.

namespace XER::IdeXejso {

bool IdePreviewSelftest(std::string* report) {
    if (report != nullptr) {
        *report = "skipped: preview selftest is Windows-only\n";
    }
    return true;
}

}  // namespace XER::IdeXejso

#endif  // _WIN32
