// NodeParamHost — see node_param_host.h for the design notes.

#include "node_param_host.h"

#include <cstdlib>
#include <cstring>

namespace XER::UI::Widgets {
namespace {

const wchar_t kHostClass[] = L"XERParamHost";
// Magenta colorkey — the host's only painted color, made transparent
// by LWA_COLORKEY. Child controls never use this color.
const COLORREF kKeyColor = RGB(255, 0, 255);
bool g_class_registered = false;

void RegisterHostClass() {
    if (g_class_registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &NodeParamHost::HostProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.lpszClassName = kHostClass;
    g_class_registered = RegisterClassExW(&wc) != 0;
}

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                      static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        w.data(), n);
    return w;
}

} // namespace

bool NodeParamHost::Create(void* parent_hwnd) {
    if (host_ != nullptr || parent_hwnd == nullptr) return false;
    parent_ = static_cast<HWND>(parent_hwnd);
    RegisterHostClass();
    dpi_ = static_cast<float>(GetDpiForWindow(parent_)) / 96.0f;
    // WS_EX_LAYERED + colorkey makes the host's own pixels fully
    // transparent (children still render opaque); WS_EX_TRANSPARENT +
    // HTTRANSPARENT pass all mouse input through to the canvas except
    // directly over a control; WS_EX_NOACTIVATE keeps keyboard focus.
    host_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        kHostClass, L"", WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, 10, 10, parent_, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (host_ == nullptr) return false;
    SetLayeredWindowAttributes(host_, kKeyColor, 0, LWA_COLORKEY);
    key_brush_ = CreateSolidBrush(kKeyColor);
    SetWindowLongPtrW(host_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    RebuildBrushes();
    return true;
}

void NodeParamHost::Destroy() {
    for (auto& [key, c] : ctrls_) {
        if (c.hwnd) DestroyWindow(c.hwnd);
        if (c.aux) DestroyWindow(c.aux);
    }
    ctrls_.clear();
    hwnd_map_.clear();
    if (host_ != nullptr) {
        DestroyWindow(host_);
        host_ = nullptr;
    }
    if (key_brush_) { DeleteObject(key_brush_); key_brush_ = nullptr; }
    if (brush_) { DeleteObject(brush_); brush_ = nullptr; }
    if (font_) { DeleteObject(font_); font_ = nullptr; }
    parent_ = nullptr;
}

void NodeParamHost::SetHostVisible(bool visible) {
    if (host_ != nullptr) {
        // SW_HIDE on the host hides the whole child-control subtree.
        ShowWindow(host_, visible ? SW_SHOWNA : SW_HIDE);
    }
}

void NodeParamHost::SetCanvasRect(float x, float y, float w, float h) {
    canvas_x_ = x; canvas_y_ = y; canvas_w_ = w; canvas_h_ = h;
    if (host_ == nullptr || parent_ == nullptr) return;
    dpi_ = static_cast<float>(GetDpiForWindow(parent_)) / 96.0f;
    const int px = static_cast<int>(x * dpi_ + 0.5f);
    const int py = static_cast<int>(y * dpi_ + 0.5f);
    const int pw = std::max(1, static_cast<int>(w * dpi_ + 0.5f));
    const int ph = std::max(1, static_cast<int>(h * dpi_ + 0.5f));
    SetWindowPos(host_, nullptr, px, py, pw, ph, SWP_NOZORDER | SWP_NOACTIVATE);
}

void NodeParamHost::SetColors(float br, float bg, float bb,
                              float fr, float fg, float fb,
                              float rr, float rg, float rb) {
    const float nbg[3] = {br, bg, bb}, nfg[3] = {fr, fg, fb},
                nbr[3] = {rr, rg, rb};
    bool same = true;
    for (int i = 0; i < 3; ++i) {
        same = same && nbg[i] == bg_[i] && nfg[i] == fg_[i] && nbr[i] == br_[i];
    }
    if (same) return;
    for (int i = 0; i < 3; ++i) {
        bg_[i] = nbg[i]; fg_[i] = nfg[i]; br_[i] = nbr[i];
    }
    auto to_rgb = [](const float* c) {
        return RGB(static_cast<int>(c[0] * 255.0f),
                   static_cast<int>(c[1] * 255.0f),
                   static_cast<int>(c[2] * 255.0f));
    };
    bg_color_ = to_rgb(bg_);
    text_color_ = to_rgb(fg_);
    RebuildBrushes();
}

void NodeParamHost::RebuildBrushes() {
    if (brush_) DeleteObject(brush_);
    brush_ = CreateSolidBrush(bg_color_);
    if (font_ == nullptr) {
        // ~9.5 DIP at 96dpi -> 13px logical, scaled by dpi.
        const int h = -MulDiv(13, static_cast<int>(dpi_ * 96.0f), 96);
        font_ = CreateFontW(h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }
    // Propagate new colors to existing controls' parents via WM_CTLCOLOR*
    // is automatic — the host answers with the new brush next paint.
}

std::string NodeParamHost::KeyOf(std::uint64_t node,
                                 const std::string& param) const {
    return std::to_string(node) + "\x1f" + param;
}

NodeParamHost::Ctrl* NodeParamHost::FindByHwnd(HWND hwnd, bool* is_aux) {
    auto it = hwnd_map_.find(hwnd);
    if (it == hwnd_map_.end()) return nullptr;
    auto ct = ctrls_.find(it->second.first);
    if (ct == ctrls_.end()) return nullptr;
    *is_aux = it->second.second;
    return &ct->second;
}

void NodeParamHost::EnsureControl(Ctrl& c, const ParamCtrlSpec& s) {
    const wchar_t* cls = L"EDIT";
    DWORD style = WS_CHILD | ES_AUTOHSCROLL;
    if (s.kind == 1) { cls = L"BUTTON"; style = WS_CHILD | BS_AUTOCHECKBOX; }
    else if (s.kind == 2) {
        cls = L"COMBOBOX";
        style = WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL;
    }
    c.hwnd = CreateWindowExW(
        0, cls, L"", style, 0, 0, 10, 10, host_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(c.id)),
        GetModuleHandleW(nullptr), nullptr);
    if (font_) SendMessageW(c.hwnd, WM_SETFONT,
                            reinterpret_cast<WPARAM>(font_), TRUE);
    hwnd_map_[c.hwnd] = {KeyOf(s.node, s.param), false};

    if (s.kind == 4) {
        c.aux_id = next_id_++;
        c.aux = CreateWindowExW(
            0, L"BUTTON", L"\x2026",
            WS_CHILD | BS_PUSHBUTTON, 0, 0, 10, 10, host_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(c.aux_id)),
            GetModuleHandleW(nullptr), nullptr);
        if (font_) SendMessageW(c.aux, WM_SETFONT,
                                reinterpret_cast<WPARAM>(font_), TRUE);
        hwnd_map_[c.aux] = {KeyOf(s.node, s.param), true};
    }
}

void NodeParamHost::PlaceControl(Ctrl& c, const ParamCtrlSpec& s) {
    const int x = static_cast<int>(s.x * dpi_ + 0.5f);
    const int y = static_cast<int>(s.y * dpi_ + 0.5f);
    const int w = std::max(8, static_cast<int>(s.w * dpi_ + 0.5f));
    const int h = std::max(8, static_cast<int>(s.h * dpi_ + 0.5f));
    RECT r{x, y, x + w, y + h};
    if (!c.placed || std::memcmp(&r, &c.px, sizeof(RECT)) != 0) {
        SetWindowPos(c.hwnd, nullptr, x, y, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        c.px = r;
        c.placed = true;
    }
    if (c.aux != nullptr) {
        // "…" button sits just right of the EDIT (widget leaves the gap).
        const int aw = std::max(16, static_cast<int>(20.0f * dpi_ + 0.5f));
        const int ax = x + w + 2;
        const int ay = y + 1;
        const int ah = std::max(8, h - 2);
        RECT ar{ax, ay, ax + aw, ay + ah};
        if (!c.aux_placed || std::memcmp(&ar, &c.aux_px, sizeof(RECT)) != 0) {
            SetWindowPos(c.aux, nullptr, ax, ay, aw, ah,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            c.aux_px = ar;
            c.aux_placed = true;
        }
    }
    ShowWindow(c.hwnd, SW_SHOWNA);
    if (c.aux) ShowWindow(c.aux, SW_SHOWNA);
}

void NodeParamHost::UpdateControlText(Ctrl& c, const ParamCtrlSpec& s) {
    const bool focused = GetFocus() == c.hwnd;
    // Programmatic mutations below fire EN_CHANGE/CBN_SELCHANGE — the
    // updating_ flag keeps them from being echoed back as commits.
    struct UpdateGuard {
        NodeParamHost* h;
        UpdateGuard(NodeParamHost* hh) : h(hh) { h->updating_ = true; }
        ~UpdateGuard() { h->updating_ = false; }
    } guard(this);
    if (s.kind == 2) {
        // Rebuild items only when the option list changed.
        if (c.options != s.options) {
            SendMessageW(c.hwnd, CB_RESETCONTENT, 0, 0);
            for (const auto& o : s.options) {
                const std::wstring wo = ToWide(o);
                SendMessageW(c.hwnd, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(wo.c_str()));
            }
            c.options = s.options;
        }
        int sel = CB_ERR;
        for (size_t i = 0; i < c.options.size(); ++i) {
            if (c.options[i] == s.text) { sel = static_cast<int>(i); break; }
        }
        if (sel == CB_ERR) sel = 0;
        if (!focused &&
            static_cast<int>(SendMessageW(c.hwnd, CB_GETCURSEL, 0, 0)) != sel) {
            SendMessageW(c.hwnd, CB_SETCURSEL, sel, 0);
        }
        return;
    }
    if (s.kind == 1) {
        const bool on = s.text == "1" || s.text == "true";
        const LRESULT cur = SendMessageW(c.hwnd, BM_GETCHECK, 0, 0);
        const UINT want = on ? BST_CHECKED : BST_UNCHECKED;
        if (static_cast<UINT>(cur) != want) {
            SendMessageW(c.hwnd, BM_SETCHECK, want, 0);
        }
        return;
    }
    // EDIT kinds 0/3/4 — don't clobber text while the user is typing.
    if (focused) return;
    wchar_t buf[1024];
    GetWindowTextW(c.hwnd, buf, 1024);
    const std::wstring want = ToWide(s.text);
    if (want != buf) {
        SetWindowTextW(c.hwnd, want.c_str());
        // Keep caret at end for append-style edits.
        const int len = static_cast<int>(want.size());
        SendMessageW(c.hwnd, EM_SETSEL, len, len);
    }
}

void NodeParamHost::HideAll() {
    for (auto& [key, c] : ctrls_) {
        if (c.hwnd) ShowWindow(c.hwnd, SW_HIDE);
        if (c.aux) ShowWindow(c.aux, SW_HIDE);
    }
}

void NodeParamHost::Sync(const std::vector<ParamCtrlSpec>& specs) {
    if (host_ == nullptr) return;
    dpi_ = static_cast<float>(GetDpiForWindow(parent_)) / 96.0f;
    // Host covers the canvas area (cheap no-op when unchanged).
    SetCanvasRect(canvas_x_, canvas_y_, canvas_w_, canvas_h_);

    // Mark-and-sweep: specs seen this frame stay; everything else hides.
    std::unordered_map<std::string, bool> seen;
    seen.reserve(specs.size());
    for (const auto& s : specs) {
        if (s.w < 24.0f || s.h < 10.0f) continue;  // sliver — skip
        const std::string key = KeyOf(s.node, s.param);
        seen[key] = true;
        auto it = ctrls_.find(key);
        if (it == ctrls_.end()) {
            Ctrl c;
            c.id = next_id_++;
            c.kind = s.kind;
            c.node = s.node;
            c.param = s.param;
            EnsureControl(c, s);
            it = ctrls_.emplace(key, std::move(c)).first;
        }
        Ctrl& c = it->second;
        c.kind = s.kind;
        c.node = s.node;
        c.param = s.param;
        PlaceControl(c, s);
        UpdateControlText(c, s);
    }
    for (auto& [key, c] : ctrls_) {
        if (!seen.count(key)) {
            if (c.hwnd) ShowWindow(c.hwnd, SW_HIDE);
            if (c.aux) ShowWindow(c.aux, SW_HIDE);
        }
    }
}

LRESULT CALLBACK NodeParamHost::HostProc(HWND hwnd, UINT msg, WPARAM wp,
                                         LPARAM lp) {
    NodeParamHost* self =
        reinterpret_cast<NodeParamHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    switch (msg) {
    case WM_NCHITTEST:
        // Click-through: the host never takes mouse input itself — only
        // its child controls do. Everything else falls to the canvas.
        return HTTRANSPARENT;
    case WM_ERASEBKGND: {
        // Paint the whole client area with the colorkey so the layered
        // surface is fully transparent; child controls draw on top.
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(reinterpret_cast<HDC>(wp), &rc, self->key_brush_);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND: {
        const HWND c_hwnd = reinterpret_cast<HWND>(lp);
        const int code = HIWORD(wp);
        bool is_aux = false;
        if (self->updating_) break;
        Ctrl* c = self->FindByHwnd(c_hwnd, &is_aux);
        if (c == nullptr) break;
        if (is_aux) {
            if (code == BN_CLICKED && self->browse_) {
                self->browse_(c->node, c->param);
            }
            break;
        }
        if (self->commit_ == nullptr) break;
        if ((c->kind == 0 || c->kind == 3 || c->kind == 4) &&
            code == EN_CHANGE) {
            wchar_t buf[2048];
            GetWindowTextW(c->hwnd, buf, 2048);
            char utf8[4096];
            const int n = WideCharToMultiByte(
                CP_UTF8, 0, buf, -1, utf8, sizeof(utf8), nullptr, nullptr);
            if (n > 0) self->commit_(c->node, c->param,
                                     std::string(utf8, n - 1));
        } else if (c->kind == 1 && code == BN_CLICKED) {
            const bool on =
                SendMessageW(c->hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
            self->commit_(c->node, c->param, on ? "1" : "0");
        } else if (c->kind == 2 && code == CBN_SELCHANGE) {
            const int sel = static_cast<int>(
                SendMessageW(c->hwnd, CB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(c->options.size())) {
                self->commit_(c->node, c->param, c->options[sel]);
            }
        }
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetBkColor(hdc, self->bg_color_);
        SetTextColor(hdc, self->text_color_);
        return reinterpret_cast<LRESULT>(self->brush_);
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace XER::UI::Widgets
