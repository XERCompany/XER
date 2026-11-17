// cef_host.cc — see cef_host.h. Stub path keeps the interface intact
// when XER_HAS_CEF is undefined.
#include "cef_host.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <utility>

#if defined(XER_HAS_CEF)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"   // full OnceCallback definition
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_load_handler.h"
#include "include/wrapper/cef_closure_task.h"  // CefPostTask(OnceClosure)

#include "cef_runtime.h"
#include "shell_commands.h"
#endif

namespace XER::Cef {

#if defined(XER_HAS_CEF)
namespace {

HWND HwndOf(CefHostNativeHandle h) { return static_cast<HWND>(h); }

// ---- UI-thread task bodies -------------------------------------------
// CEF's base::BindOnce deliberately rejects capturing lambdas — every
// browser op is a stateless function and arguments are bound directly.

void CreateBrowserOnUI(CefRefPtr<CefClient> client,
                       CefHostNativeHandle parent, CefRect rect,
                       CefString url, CefRefPtr<CefBrowser>* out,
                       bool* done, std::mutex* m,
                       std::condition_variable* cv) {
    CefWindowInfo info;
    info.SetAsChild(static_cast<HWND>(parent), rect);
    CefBrowserSettings bs;
    bs.background_color = CefColorSetARGB(255, 24, 24, 28);   // dark
    CefRefPtr<CefBrowser> b = CefBrowserHost::CreateBrowserSync(
        info, client, url, bs, nullptr, nullptr);
    {
        std::lock_guard<std::mutex> lk(*m);
        *out = std::move(b);
        *done = true;
    }
    cv->notify_one();
}

void DoWasHidden(CefRefPtr<CefBrowser> b, bool hidden) {
    b->GetHost()->WasHidden(hidden);
}
void DoLoadUrl(CefRefPtr<CefBrowser> b, CefString url) {
    if (auto f = b->GetMainFrame()) {
        f->LoadURL(url);
    }
}
void DoGoBack(CefRefPtr<CefBrowser> b) { b->GoBack(); }
void DoGoForward(CefRefPtr<CefBrowser> b) { b->GoForward(); }
void DoReload(CefRefPtr<CefBrowser> b) { b->Reload(); }
void DoStop(CefRefPtr<CefBrowser> b) { b->StopLoad(); }
void DoFocus(CefRefPtr<CefBrowser> b) { b->GetHost()->SetFocus(true); }
void DoShowDevTools(CefRefPtr<CefBrowser> b, CefRefPtr<CefClient> c) {
    CefWindowInfo wi;
    wi.SetAsPopup(nullptr, "DevTools");
    CefBrowserSettings bs;
    b->GetHost()->ShowDevTools(wi, c, bs, CefPoint());
}
void DoClose(CefRefPtr<CefBrowser> b) {
    b->GetHost()->CloseBrowser(/*force_close=*/true);
}

// Minimal client surface: lifespan (closed notify) + display (title,
// url) + load (loading flag) — everything else uses CEF defaults.
class XERCefClient final : public CefClient,
                           public CefDisplayHandler,
                           public CefLifeSpanHandler,
                           public CefLoadHandler {
public:
    XERCefClient(HostEvents events, std::atomic<bool>* loading)
        : events_(std::move(events)), loading_(loading) {}

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
        return this;
    }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

    void OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title)
        override {
        if (events_.on_title) {
            events_.on_title(title.ToString());
        }
    }
    void OnAddressChange(CefRefPtr<CefBrowser>,
                         CefRefPtr<CefFrame> frame,
                         const CefString& url) override {
        if (frame->IsMain() && events_.on_url) {
            events_.on_url(url.ToString());
        }
    }
    void OnLoadingStateChange(CefRefPtr<CefBrowser>, bool is_loading,
                              bool, bool) override {
        if (loading_ != nullptr) {
            loading_->store(is_loading);
        }
        if (events_.on_loading) {
            events_.on_loading(is_loading);
        }
    }

    void OnBeforeClose(CefRefPtr<CefBrowser>) override {
        if (events_.on_closed) {
            events_.on_closed();
        }
    }

    bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
        CefProcessId,
        CefRefPtr<CefProcessMessage> message) override {
        // xer.call from pages (injected by xer_browser_shell.exe's
        // renderer handler) → src/ API command registry.
        return HandleShellMessage(browser, frame, message);
    }

private:
    HostEvents events_;
    std::atomic<bool>* loading_;   // owned by CefHost::Impl

    XERCefClient(const XERCefClient&) = delete;
    XERCefClient& operator=(const XERCefClient&) = delete;

    IMPLEMENT_REFCOUNTING(XERCefClient);
};

}  // namespace

struct CefHost::Impl {
    HostEvents events;
    CefRefPtr<XERCefClient> client;
    CefRefPtr<CefBrowser> browser;   // valid until Close()
    std::atomic<bool> loading{false};
    std::string current_url;
    HWND child = nullptr;            // Chromium's own child window
};

CefHost::CefHost() : impl_(std::make_unique<Impl>()) {}
CefHost::~CefHost() { Close(); }

bool CefHost::Create(CefHostNativeHandle parent, int x, int y, int w,
                     int h, const std::string& url, HostEvents events) {
    if (!IsInitialized()) {
        return false;
    }
    impl_->events = std::move(events);
    impl_->client = new XERCefClient(impl_->events, &impl_->loading);
    impl_->current_url = url.empty() ? "about:blank" : url;

    // CreateBrowserSync must run on the CEF UI thread; wait for it — the
    // caller's thread is the app UI thread, not CEF's.
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    CefPostTask(TID_UI,
                base::BindOnce(&CreateBrowserOnUI,
                               CefRefPtr<CefClient>(impl_->client.get()),
                               parent, CefRect(x, y, w, h),
                               CefString(impl_->current_url),
                               &impl_->browser, &done, &m, &cv));
    std::unique_lock<std::mutex> lk(m);
    cv.wait(lk, [&] { return done; });
    if (!impl_->browser) {
        impl_->client = nullptr;
        return false;
    }
    impl_->child = impl_->browser->GetHost()->GetWindowHandle();
    return true;
}

void CefHost::Resize(int x, int y, int w, int h) {
    // The browser HWND is a real child of our parent — SetWindowPos
    // re-fits it; Chromium repaints itself.
    if (impl_->child != nullptr) {
        ::SetWindowPos(impl_->child, nullptr, x, y, w, h,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void CefHost::Show(bool visible) {
    if (impl_->child != nullptr) {
        ::ShowWindow(impl_->child, visible ? SW_SHOW : SW_HIDE);
    }
    if (impl_->browser) {
        CefPostTask(TID_UI, base::BindOnce(&DoWasHidden, impl_->browser,
                                           !visible));
    }
}

void CefHost::LoadUrl(const std::string& url) {
    if (url.empty() || !impl_->browser) {
        return;
    }
    impl_->current_url = url;
    CefPostTask(TID_UI, base::BindOnce(&DoLoadUrl, impl_->browser,
                                       CefString(url)));
}

void CefHost::LoadHtml(const std::string& html,
                       const std::string& base_url) {
    // data: URLs render small pages without a scheme handler.
    std::string encoded = "data:text/html;charset=utf-8,";
    encoded.reserve(encoded.size() + html.size());
    for (const unsigned char c : html) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            encoded.push_back(static_cast<char>(c));
        } else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            encoded += buf;
        }
    }
    (void)base_url;
    LoadUrl(encoded);
}

void CefHost::GoBack() {
    if (impl_->browser) {
        CefPostTask(TID_UI, base::BindOnce(&DoGoBack, impl_->browser));
    }
}
void CefHost::GoForward() {
    if (impl_->browser) {
        CefPostTask(TID_UI,
                    base::BindOnce(&DoGoForward, impl_->browser));
    }
}
void CefHost::Reload() {
    if (impl_->browser) {
        CefPostTask(TID_UI, base::BindOnce(&DoReload, impl_->browser));
    }
}
void CefHost::Stop() {
    if (impl_->browser) {
        CefPostTask(TID_UI, base::BindOnce(&DoStop, impl_->browser));
    }
}
void CefHost::Focus() {
    if (impl_->browser) {
        CefPostTask(TID_UI, base::BindOnce(&DoFocus, impl_->browser));
    }
}
void CefHost::ShowDevTools() {
    if (impl_->browser) {
        CefPostTask(TID_UI,
                    base::BindOnce(&DoShowDevTools, impl_->browser,
                                   CefRefPtr<CefClient>(
                                       impl_->client.get())));
    }
}

bool CefHost::HasBrowser() const {
    return impl_->browser.get() != nullptr;
}
std::string CefHost::CurrentUrl() const { return impl_->current_url; }
bool CefHost::Loading() const { return impl_->loading.load(); }

void CefHost::Close() {
    if (!impl_ || !impl_->browser) {
        return;
    }
    const auto browser = impl_->browser;
    impl_->browser = nullptr;
    impl_->child = nullptr;
    impl_->client = nullptr;
    CefPostTask(TID_UI, base::BindOnce(&DoClose, browser));
}

#else   // !XER_HAS_CEF — inert stubs

struct CefHost::Impl {};

CefHost::CefHost() : impl_(std::make_unique<Impl>()) {}
CefHost::~CefHost() = default;
bool CefHost::Create(CefHostNativeHandle, int, int, int, int,
                     const std::string&, HostEvents) {
    return false;
}
void CefHost::Resize(int, int, int, int) {}
void CefHost::Show(bool) {}
void CefHost::LoadUrl(const std::string&) {}
void CefHost::LoadHtml(const std::string&, const std::string&) {}
void CefHost::GoBack() {}
void CefHost::GoForward() {}
void CefHost::Reload() {}
void CefHost::Stop() {}
void CefHost::Focus() {}
void CefHost::ShowDevTools() {}
bool CefHost::HasBrowser() const { return false; }
std::string CefHost::CurrentUrl() const { return {}; }
bool CefHost::Loading() const { return false; }
void CefHost::Close() {}

#endif  // XER_HAS_CEF

}  // namespace XER::Cef
