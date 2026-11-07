#pragma once

// HWND-backed parameter widgets overlaid on NodeGraph node cards.
//
// The graph surface is a single Canvas child HWND; real Win32 controls
// cannot live inside it, so this host owns one borderless child window
// ("XERParamHost") covering the canvas area. Param controls (EDIT,
// CHECKBOX, COMBOBOX) are children of that host, positioned each frame
// from canvas-local DIP specs computed by the editor widget.
//
// The host window itself is click-through (WM_NCHITTEST -> HTTRANSPARENT)
// so mouse/keyboard reach the canvas everywhere except directly over a
// control. Colors come from WM_CTLCOLO* sent to the host, which returns
// theme brushes supplied via SetColors.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace XER::UI::Widgets {

// One control request, rect in canvas-local DIPs (same space the editor
// uses for node geometry, before the canvas->parent offset).
struct ParamCtrlSpec {
    std::uint64_t node = 0;
    std::string param;
    int kind = 0;  // 0=number EDIT, 1=bool CHECKBOX, 2=options COMBOBOX,
                   // 3=text EDIT, 4=file EDIT + "…" button (aux)
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    std::string text;                 // current display text / "1"|"0"
    std::vector<std::string> options; // kind 2 only
};

class NodeParamHost {
public:
    // (node, param, new_text). For kind 1 text is "1"/"0"; kind 2 is the
    // selected option; kinds 0/3/4 the raw edited string.
    using CommitFn =
        std::function<void(std::uint64_t node, const std::string& param,
                           const std::string& text)>;
    // kind 4 "…" button.
    using BrowseFn =
        std::function<void(std::uint64_t node, const std::string& param)>;

    bool Create(void* parent_hwnd);
    void Destroy();
    bool IsCreated() const { return host_ != nullptr; }

    // Canvas rect in parent-client DIPs (same args as Canvas::SetBounds).
    void SetCanvasRect(float x, float y, float w, float h);

    void SetCommitHandler(CommitFn fn) { commit_ = std::move(fn); }
    void SetBrowseHandler(BrowseFn fn) { browse_ = std::move(fn); }

    // Theme colors as 0..1 floats. bg = control field background,
    // fg = text, border = accent-ish outline (reserved).
    void SetColors(float bg_r, float bg_g, float bg_b,
                   float fg_r, float fg_g, float fg_b,
                   float br_r, float br_g, float br_b);

    // Show/hide the whole overlay (host + child controls). Used when the
    // owning widget is swapped out of the window content — the widget
    // tree does not manage raw HWND children.
    void SetHostVisible(bool visible);

    // Reconcile controls with the desired spec set: creates missing,
    // repositions moved, updates values (unless focused), hides unused.
    void Sync(const std::vector<ParamCtrlSpec>& specs);

private:
    struct Ctrl {
        HWND hwnd = nullptr;   // main control (EDIT/CHECKBOX/COMBOBOX)
        HWND aux = nullptr;    // kind 4: "…" pushbutton
        int id = 0;
        int aux_id = 0;
        int kind = 0;
        std::uint64_t node = 0;
        std::string param;
        std::vector<std::string> options;
        RECT px{};             // last placed rect, host client px
        RECT aux_px{};
        bool placed = false;
        bool aux_placed = false;
    };

    static LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp,
                                     LPARAM lp);
    Ctrl* FindByHwnd(HWND hwnd, bool* is_aux);
    void EnsureControl(Ctrl& c, const ParamCtrlSpec& s);
    void PlaceControl(Ctrl& c, const ParamCtrlSpec& s);
    void UpdateControlText(Ctrl& c, const ParamCtrlSpec& s);
    void HideAll();
    std::string KeyOf(std::uint64_t node, const std::string& param) const;
    void RebuildBrushes();

    HWND host_ = nullptr;
    HWND parent_ = nullptr;
    float canvas_x_ = 0.0f, canvas_y_ = 0.0f;
    float canvas_w_ = 0.0f, canvas_h_ = 0.0f;
    float dpi_ = 1.0f;
    CommitFn commit_;
    BrowseFn browse_;

    std::unordered_map<std::string, Ctrl> ctrls_;
    std::unordered_map<HWND, std::pair<std::string, bool>> hwnd_map_;
    bool updating_ = false;  // suppress commit echo during Sync updates

    // Theme brushes (recreated when SetColors changes the inputs).
    HBRUSH brush_ = nullptr;
    HBRUSH key_brush_ = nullptr;  // colorkey fill for the transparent host
    HFONT font_ = nullptr;
    float bg_[3] = {-1, -1, -1}, fg_[3] = {-1, -1, -1},
          br_[3] = {-1, -1, -1};
    COLORREF text_color_ = RGB(230, 230, 235);
    COLORREF bg_color_ = RGB(30, 31, 36);
    int next_id_ = 1;
};

} // namespace XER::UI::Widgets
