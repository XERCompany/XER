// photo_workspace.cc — see photo_workspace.h.
#include "photo_workspace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#endif

#include "helper/path_utils.h"
#include "icon_helpers.h"
#include "lab/object_ops.h"
#include "photo/adjust_ops.h"
#include "photo/layer_ops.h"
#include "photo/mask_ops.h"
#include "photo/retouch.h"
#include "photo/xerphoto.h"
#include "rendering/renderer.h"
#include "shell_widgets.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/card.h"
#include "widgets/color_picker.h"
#include "widgets/context_menu.h"
#include "widgets/dialog.h"
#include "widgets/eyedropper.h"
#include "widgets/label.h"
#include "widgets/pie_menu.h"
#include "widgets/scroll_area.h"
#include "widgets/scrub_number_field.h"
#include "widgets/select.h"
#include "widgets/surface.h"
#include "widgets/text_input.h"
#include "widgets/tree_view.h"
#include "widgets/virtual_list.h"
#include "widgets/widget_tree.h"
#include "wrapper/skia/skia_engine_bridge.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
namespace W = XER::UI::Widgets;
namespace ph = XER::VideoEditor::Photo;
using XER::UI::Theme::Color;
using XER::VideoEditor::Ui2::VeIcons::DrawIcon;
using XER::VideoEditor::Ui2::VeIcons::InstallButtonIconResolver;
using XER::VideoEditor::Ui2::Theme::WithAlpha;

// ---- geometry constants ------------------------------------------------
constexpr float kTopBarH = 38.0f;
constexpr float kToolColW = 44.0f;
constexpr float kRightDockW = 304.0f;
constexpr float kRowH = 26.0f;   // layers TreeView row height

// ---- helpers ------------------------------------------------------------

std::string Basename(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

Color ToColor(ph::Pixel p) {
    return {p.r / 255.0f, p.g / 255.0f, p.b / 255.0f, p.a / 255.0f};
}

ph::Pixel ToPixel(const Color& c) {
    const auto byte = [](float v) {
        return static_cast<std::uint8_t>(
            std::clamp(std::lround(v * 255.0f), 0l, 255l));
    };
    return ph::Pixel::RGBA(byte(c.r), byte(c.g), byte(c.b), byte(c.a));
}

// Bridge-packed u32 (R<<24|G<<16|B<<8|A) -> BGRA byte quad.
void U32ToBgra(const std::vector<std::uint32_t>& src,
               std::vector<std::uint8_t>* out) {
    out->resize(src.size() * 4);
    for (std::size_t i = 0; i < src.size(); ++i) {
        const std::uint32_t v = src[i];
        (*out)[i * 4 + 0] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        (*out)[i * 4 + 1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        (*out)[i * 4 + 2] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
        (*out)[i * 4 + 3] = static_cast<std::uint8_t>(v & 0xFF);
    }
}

// Marching-ants dashed polyline (closed loop): draws `dash_px` runs
// offset by `phase_px` — two passes (fg/bg) give the classic ants.
void DrawAntsPolyline(Rendering::Renderer& renderer,
                      const std::vector<Layout::Point>& pts,
                      Color color, float dash_px, float phase_px,
                      float thickness) {
    if (pts.size() < 2) return;
    const float period = dash_px * 2.0f;
    double d = std::fmod(static_cast<double>(phase_px),
                         static_cast<double>(period));
    bool drawing = d < dash_px;
    float run_left = drawing ? static_cast<float>(dash_px - d)
                             : static_cast<float>(period - d);
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const Layout::Point a = pts[i];
        const Layout::Point b = pts[(i + 1) % pts.size()];
        const float seg_len = std::hypot(b.x - a.x, b.y - a.y);
        if (seg_len < 1e-4f) continue;
        const float ux = (b.x - a.x) / seg_len;
        const float uy = (b.y - a.y) / seg_len;
        float t = 0.0f;
        while (t < seg_len) {
            const float step = std::min(run_left, seg_len - t);
            if (drawing) {
                renderer.DrawLine(
                    {a.x + ux * t, a.y + uy * t},
                    {a.x + ux * (t + step), a.y + uy * (t + step)},
                    color, thickness);
            }
            t += step;
            run_left -= step;
            if (run_left <= 0.0f) {
                drawing = !drawing;
                run_left = dash_px;
            }
        }
    }
}

ph::IntRect DocRectFromCorners(float ax, float ay, float bx, float by) {
    const float x0 = std::min(ax, bx);
    const float y0 = std::min(ay, by);
    const float x1 = std::max(ax, bx);
    const float y1 = std::max(ay, by);
    return {static_cast<int>(std::floor(x0)),
            static_cast<int>(std::floor(y0)),
            static_cast<int>(std::ceil(x1 - x0)),
            static_cast<int>(std::ceil(y1 - y0))};
}

// Structural undo for layer removal (layer_ops.cc keeps its
// structural commands file-local; PhotoCommand is the public seam —
// same shape the legacy workspace used).
class RemoveLayerCommand final : public ph::PhotoCommand {
public:
    explicit RemoveLayerCommand(ph::LayerId layer) : layer_(layer) {}
    bool Apply(ph::Document& doc) override {
        ph::LayerNode* node = doc.FindLayer(layer_);
        if (node == nullptr || node == &doc.Root() ||
            node->Parent() == nullptr) {
            return false;
        }
        parent_ = node->Parent()->Id();
        index_ = static_cast<std::size_t>(
            doc.ChildIndex(*node->Parent(), layer_));
        node_ = doc.DetachLayer(layer_);
        return node_ != nullptr;
    }
    bool Revert(ph::Document& doc) override {
        ph::LayerNode* parent = doc.FindLayer(parent_);
        if (parent == nullptr || node_ == nullptr) return false;
        doc.InsertChildAt(parent, index_, std::move(node_));
        return true;
    }
    std::string Label() const override { return "Delete Layer"; }

private:
    ph::LayerId layer_;
    ph::LayerId parent_ = ph::kInvalidLayerId;
    std::size_t index_ = 0;
    std::unique_ptr<ph::LayerNode> node_;
};

#ifdef _WIN32
std::wstring FilterSpec(const wchar_t* name, const wchar_t* pattern) {
    std::wstring filter = name;
    filter.push_back(L'\0');
    filter += pattern;
    filter.push_back(L'\0');
    filter += L"All files";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    return filter;
}

std::wstring PickOpenFileW(HWND owner, const std::wstring& filter,
                           const wchar_t* title) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return file;
}

std::wstring PickSaveFileW(HWND owner, const std::wstring& filter,
                           const wchar_t* title, const wchar_t* extension,
                           const std::wstring& initial_name) {
    wchar_t file[MAX_PATH] = L"";
    if (!initial_name.empty()) {
        wcscpy_s(file, initial_name.c_str());
    }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = extension;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return {};
    return file;
}
#endif

// "New Photo Document" dialog body: W × H scrub fields the host reads
// back on confirm.
class NewDocFields final : public W::Widget {
public:
    NewDocFields() {
        auto mk = [](float v, const char* unit) {
            auto f = W::ScrubNumberField::Create();
            f->SetRange(1.0f, 16384.0f);
            f->SetStep(1.0f);
            f->SetAllowWheelScrub(false);
            f->SetValue(v);
            f->SetFormat("%.0f");
            f->SetUnit(unit);
            return f;
        };
        w_field_ = mk(1280.0f, "px");
        h_field_ = mk(720.0f, "px");
        w_label_ = std::make_shared<W::Label>("Width", true);
        h_label_ = std::make_shared<W::Label>("Height", true);
        AddChild(w_label_);
        AddChild(w_field_);
        AddChild(h_label_);
        AddChild(h_field_);
    }
    int Width() const {
        return std::clamp(static_cast<int>(w_field_->GetValue()), 1,
                          16384);
    }
    int Height() const {
        return std::clamp(static_cast<int>(h_field_->GetValue()), 1,
                          16384);
    }

    void Render(Rendering::Renderer&, const XER::UI::Theme::Tokens&)
        override {}
    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        w_label_->Layout({bounds.x, bounds.y + 6.0f, 56.0f, 20.0f});
        w_field_->Layout({bounds.x + 60.0f, bounds.y,
                          bounds.width - 60.0f, 30.0f});
        h_label_->Layout({bounds.x, bounds.y + 42.0f, 56.0f, 20.0f});
        h_field_->Layout({bounds.x + 60.0f, bounds.y + 36.0f,
                          bounds.width - 60.0f, 30.0f});
    }
    bool HandleEvent(const W::Event&) override { return false; }
    Layout::Size GetMinSize() const override { return {260.0f, 70.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {320.0f, 70.0f};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 70.0f};
    }

private:
    std::shared_ptr<W::Label> w_label_, h_label_;
    std::shared_ptr<W::ScrubNumberField> w_field_, h_field_;
};

// Foreground-color chip: paints the fg swatch, fires on_click.
class ColorChip final : public W::Widget {
public:
    ColorChip() { SetFocusable(true); }
    void SetColor(Color c) {
        color_ = c;
        Invalidate();
    }
    void SetOnClick(std::function<void()> cb) {
        on_click_ = std::move(cb);
    }
    void Render(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        const auto gb = GetBounds();
        const Layout::Rect b{gb.x + 1.0f, gb.y + 1.0f,
                             gb.width - 2.0f, gb.height - 2.0f};
        renderer.FillRect(b, color_, tokens.radius_small);
        renderer.StrokeRect(b, tokens.foreground, 1.0f,
                            tokens.radius_small);
    }
    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
    }
    bool HandleEvent(const W::Event& event) override {
        if (event.type == W::Event::Type::MouseDown &&
            event.mouse_button == W::MouseButton::Left) {
            if (on_click_) on_click_();
            return true;
        }
        return false;
    }
    Layout::Size GetMinSize() const override { return {28.0f, 24.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {32.0f, 26.0f};
    }
    Layout::Size GetMaxSize() const override { return {40.0f, 32.0f}; }

private:
    Color color_{0.12f, 0.31f, 0.86f, 1.0f};
    std::function<void()> on_click_;
};

// Per-row affordances drawn over the layers TreeView: eye + lock hit
// zones and a real mask thumbnail badge. Rows are derived from the
// SAME flatten the TreeView performs (DFS over TreeNode roots,
// descending only into expanded branches); the workspace pushes the
// flat id list so the two never drift apart.
class LayerChrome final : public W::Widget {
public:
    enum class Zone { None, Eye, Lock, Mask };

    // Providers pulled at paint/hit-test time — always current.
    std::function<std::vector<ph::LayerId>()> flat_ids;
    std::function<const ph::LayerNode*(ph::LayerId)> find_node;
    std::function<bool(ph::LayerId)> mask_editing;
    std::function<float()> scroll_y;     // tree list scroll offset
    std::function<void(ph::LayerId, Zone)> on_action;

    void InvalidateThumbs() {
        thumbs_.clear();
        Invalidate();
    }

    void Render(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        if (flat_ids == nullptr || find_node == nullptr) return;
        const auto b = GetBounds();
        const auto ids = flat_ids();
        const float scroll = scroll_y != nullptr ? scroll_y() : 0.0f;
        for (std::size_t i = 0; i < ids.size(); ++i) {
            const float ry =
                b.y + static_cast<float>(i) * kRowH - scroll;
            if (ry + kRowH < b.y || ry > b.y + b.height) continue;
            const ph::LayerNode* node = find_node(ids[i]);
            if (node == nullptr) continue;
            const Layout::Rect row{b.x, ry, b.width, kRowH};

            // Eye glyph at trailing -20..-4.
            const Layout::Rect eye = EyeRect(row);
            const Color ec = node->Visible()
                                 ? tokens.accent
                                 : WithAlpha(tokens.muted, 0.7f);
            const float ex = eye.x + eye.width * 0.5f;
            const float ey = eye.y + eye.height * 0.5f;
            renderer.StrokeRect({ex - 4.0f, ey - 3.0f, 8.0f, 6.0f}, ec,
                                1.0f, 3.0f);
            if (node->Visible()) {
                renderer.FillRect({ex - 1.5f, ey - 1.5f, 3.0f, 3.0f},
                                  ec, 1.5f);
            } else {
                renderer.DrawLine({ex - 4.0f, ey + 3.0f},
                                  {ex + 4.0f, ey - 3.0f}, ec, 1.0f);
            }

            // Lock glyph between eye and mask badge.
            const Layout::Rect lock = LockRect(row);
            const float lx = lock.x + lock.width * 0.5f;
            const float ly = lock.y + lock.height * 0.5f;
            const Color lc = node->Locked()
                                 ? tokens.accent
                                 : WithAlpha(tokens.muted, 0.45f);
            if (node->Locked()) {
                DrawIcon(renderer, "lock", 12, lc, lock);
            } else {
                DrawIcon(renderer, "lock_open", 12, lc, lock);
            }
            (void)lx;
            (void)ly;

            // Mask thumbnail badge when the node has a mask.
            if (node->HasMask()) {
                const Layout::Rect badge = MaskRect(row);
                const Thumb& t = ThumbFor(ids[i], *node);
                if (!t.bgra.empty()) {
                    renderer.DrawFrameBgra(t.bgra.data(), t.w, t.h,
                                           badge, 1.0f);
                }
                const bool editing =
                    mask_editing != nullptr && mask_editing(ids[i]);
                renderer.StrokeRect(
                    badge,
                    editing ? tokens.accent : tokens.border,
                    editing ? 1.5f : 1.0f, 0.0f);
            }
        }
    }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
    }

    bool HandleEvent(const W::Event& event) override {
        if (event.type != W::Event::Type::MouseDown ||
            event.mouse_button != W::MouseButton::Left ||
            flat_ids == nullptr) {
            return false;
        }
        const auto b = GetBounds();
        if (!b.Contains(event.position)) return false;
        const auto ids = flat_ids();
        const float scroll = scroll_y != nullptr ? scroll_y() : 0.0f;
        const int row = static_cast<int>(
            std::floor((event.position.y - b.y + scroll) / kRowH));
        if (row < 0 || row >= static_cast<int>(ids.size())) {
            return false;
        }
        const ph::LayerNode* node = find_node(ids[row]);
        if (node == nullptr) return false;
        const Layout::Rect rr{b.x, b.y + row * kRowH - scroll, b.width,
                              kRowH};
        Zone zone = Zone::None;
        if (EyeRect(rr).Contains(event.position)) {
            zone = Zone::Eye;
        } else if (LockRect(rr).Contains(event.position)) {
            zone = Zone::Lock;
        } else if (node->HasMask() &&
                   MaskRect(rr).Contains(event.position)) {
            zone = Zone::Mask;
        }
        if (zone == Zone::None || on_action == nullptr) return false;
        on_action(ids[row], zone);
        return true;
    }

    Layout::Size GetMinSize() const override { return {0.0f, 0.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {0.0f, 0.0f};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    static Layout::Rect EyeRect(const Layout::Rect& row) {
        return {row.Right() - 22.0f, row.y + 4.0f, 18.0f,
                row.height - 8.0f};
    }
    static Layout::Rect LockRect(const Layout::Rect& row) {
        return {row.Right() - 40.0f, row.y + 4.0f, 18.0f,
                row.height - 8.0f};
    }
    static Layout::Rect MaskRect(const Layout::Rect& row) {
        return {row.Right() - 62.0f, row.y + 4.0f, 20.0f,
                row.height - 8.0f};
    }

    struct Thumb {
        const ph::TiledDevice* src = nullptr;
        std::vector<std::uint8_t> bgra;
        int w = 0, h = 0;
    };

    // Grayscale coverage thumbnail, letterboxed into ~20x14. Read the
    // mask's painted EXTENT (not the whole canvas) so thumbs stay
    // cheap on big documents.
    const Thumb& ThumbFor(ph::LayerId id, const ph::LayerNode& node) {
        Thumb& t = thumbs_[id];
        const ph::TiledDevice* dev = node.Mask();
        if (t.src == dev && !t.bgra.empty()) return t;
        t.src = dev;
        t.bgra.clear();
        t.w = t.h = 0;
        if (dev == nullptr) return t;
        const ph::IntRect ext = dev->Extent();
        if (ext.IsEmpty()) {
            // Fresh white mask — show a filled badge.
            t.w = 20;
            t.h = 14;
            t.bgra.assign(20 * 14 * 4, 0);
            for (int i = 0; i < 20 * 14; ++i) {
                t.bgra[i * 4 + 0] = 220;
                t.bgra[i * 4 + 1] = 220;
                t.bgra[i * 4 + 2] = 220;
                t.bgra[i * 4 + 3] = 255;
            }
            return t;
        }
        const auto cov = dev->ReadRegionCoverage(ext);
        // Downsample stride-wise into the badge box.
        t.w = 20;
        t.h = std::clamp(
            static_cast<int>(std::lround(
                20.0f * ext.h / std::max(1, ext.w))),
            2, 14);
        t.bgra.assign(static_cast<std::size_t>(t.w) * t.h * 4, 0);
        for (int y = 0; y < t.h; ++y) {
            const int sy = ext.h * y / t.h;
            for (int x = 0; x < t.w; ++x) {
                const int sx = ext.w * x / t.w;
                const std::uint8_t v = cov[static_cast<std::size_t>(
                    sy * ext.w + sx)];
                const std::size_t o =
                    (static_cast<std::size_t>(y) * t.w + x) * 4;
                t.bgra[o + 0] = v;
                t.bgra[o + 1] = v;
                t.bgra[o + 2] = v;
                t.bgra[o + 3] = 255;
            }
        }
        return t;
    }

    std::unordered_map<ph::LayerId, Thumb> thumbs_;
};

// Layers-tab host: [toolbar | tree+chrome | selected-layer strip].
// Plain manual layout — the chrome overlay must sit on top of the
// tree's slot, so a LinearBox can't express it.
class LayersPanel final : public W::Widget {
public:
    void SetRows(std::shared_ptr<W::Widget> toolbar,
                 std::shared_ptr<W::Widget> tree,
                 std::shared_ptr<W::Widget> chrome,
                 std::shared_ptr<W::Widget> strip) {
        toolbar_ = std::move(toolbar);
        tree_ = std::move(tree);
        chrome_ = std::move(chrome);
        strip_ = std::move(strip);
        AddChild(toolbar_);
        AddChild(tree_);
        AddChild(chrome_);
        AddChild(strip_);
    }
    void Render(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
    }
    void Layout(const Layout::Rect& bounds) override {
        Layout(bounds, CachedLayoutTokens());
    }
    void Layout(const Layout::Rect& bounds,
                const XER::UI::Theme::Tokens& tokens) override {
        SetBounds(bounds);
        // The toolbar row count is content-driven — ask it instead of
        // hardcoding one row's height.
        const float toolbar_h = std::max(
            24.0f, toolbar_->GetPreferredSize().height);
        const float strip_h = 66.0f;
        toolbar_->Layout({bounds.x, bounds.y, bounds.width, toolbar_h},
                         tokens);
        const Layout::Rect tree_r{bounds.x, bounds.y + toolbar_h,
                                  bounds.width,
                                  bounds.height - toolbar_h - strip_h};
        tree_->Layout(tree_r, tokens);
        chrome_->Layout(tree_r, tokens);
        strip_->Layout(
            {bounds.x, bounds.y + bounds.height - strip_h,
             bounds.width, strip_h},
            tokens);
    }
    bool HandleEvent(const W::Event&) override { return false; }
    Layout::Size GetMinSize() const override { return {180.0f, 200.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {280.0f, 400.0f};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    std::shared_ptr<W::Widget> toolbar_;
    std::shared_ptr<W::Widget> tree_;
    std::shared_ptr<W::Widget> chrome_;
    std::shared_ptr<W::Widget> strip_;
};

// One labeled scrub-field row for an adjustment parameter.
struct AdjParamRow {
    std::shared_ptr<W::Label> label;
    std::shared_ptr<W::ScrubNumberField> field;
};

// Tool metadata (index-aligned with PhotoWorkspace::Tool).
struct ToolInfo {
    PhotoWorkspace::Tool tool;
    const char* label;
    const char* icon;    // pack icon name ("" -> text fallback)
    const char* tooltip;
    char key;            // TextInput shortcut (0 = none)
};

constexpr ToolInfo kToolInfos[] = {
    {PhotoWorkspace::Tool::Move, "Move", "photo.move",
     "Move layer pixels (V) — drag, release commits", 'v'},
    {PhotoWorkspace::Tool::Pan, "Pan", "photo.pan",
     "Hand — drag pans the canvas (H); middle-drag always pans", 'h'},
    {PhotoWorkspace::Tool::SelectRect, "Rect", "photo.select_rect",
     "Rect marquee (M) — Shift adds, Alt subtracts", 'm'},
    {PhotoWorkspace::Tool::SelectEllipse, "Oval",
     "photo.select_ellipse",
     "Ellipse marquee (O) — Shift adds, Alt subtracts", 'o'},
    {PhotoWorkspace::Tool::Lasso, "Lasso", "photo.lasso",
     "Freehand lasso (L) — drag a loop", 'l'},
    {PhotoWorkspace::Tool::PolyLasso, "Poly", "photo.poly_lasso",
     "Polygon lasso (P) — click vertices, Enter closes", 'p'},
    {PhotoWorkspace::Tool::MagicWand, "Wand", "photo.wand",
     "Magic wand (W) — click a color, tolerance in the bar", 'w'},
    {PhotoWorkspace::Tool::Brush, "Brush", "photo.brush",
     "Brush (B) — size/hardness/opacity in the bar", 'b'},
    {PhotoWorkspace::Tool::Eraser, "Erase", "photo.eraser",
     "Eraser (E) — dstOut strokes; on masks paints coverage 0", 'e'},
    {PhotoWorkspace::Tool::Fill, "Fill", "photo.fill",
     "Fill (F) — click fills the active layer with fg", 'f'},
    {PhotoWorkspace::Tool::Gradient, "Grad", "photo.gradient",
     "Gradient (G) — drag a line; fg fades to transparent", 'g'},
    {PhotoWorkspace::Tool::CloneStamp, "Clone", "photo.clone",
     "Clone stamp (S) — Alt+click picks the source", 's'},
    {PhotoWorkspace::Tool::Healing, "Heal", "photo.heal",
     "Healing brush (J) — Alt+click picks the source", 'j'},
    {PhotoWorkspace::Tool::Spot, "Spot", "photo.spot",
     "Spot remove (K) — paint over blemishes", 'k'},
    {PhotoWorkspace::Tool::Inpaint, "Fill~", "photo.inpaint",
     "Inpaint (N) — fills the active selection", 'n'},
    {PhotoWorkspace::Tool::Crop, "Crop", "photo.crop",
     "Crop (C) — drag a rect, Enter commits (rebuilds document)",
     'c'},
    {PhotoWorkspace::Tool::Eyedropper, "Pick", "photo.eyedropper",
     "Eyedropper (I) — click samples the composite into fg", 'i'},
};
constexpr int kToolCount =
    static_cast<int>(sizeof(kToolInfos) / sizeof(kToolInfos[0]));

}  // namespace

// ===========================================================================
// PhotoCanvasSurface
// ===========================================================================

PhotoCanvasSurface::PhotoCanvasSurface() {
    surface_ = std::make_shared<W::Surface>();
    surface_->SetPaintCallback(
        [this](Rendering::Renderer& renderer,
               const Layout::Rect& content,
               const XER::UI::Theme::Tokens& tokens) {
            Paint(renderer, content, tokens);
        });
    surface_->SetEventCallback(
        [this](const W::Event& event) { return OnSurfaceEvent(event); });
    AddChild(surface_);
}

PhotoCanvasSurface::~PhotoCanvasSurface() = default;

std::shared_ptr<PhotoCanvasSurface> PhotoCanvasSurface::Create() {
    return std::shared_ptr<PhotoCanvasSurface>(new PhotoCanvasSurface());
}

void PhotoCanvasSurface::SetHooks(PhotoCanvasHooks hooks) {
    hooks_ = std::move(hooks);
}

// ---- document-state pushes ----------------------------------------------

void PhotoCanvasSurface::SetComposite(std::vector<std::uint8_t> bgra,
                                      int width, int height) {
    const bool was_empty = composite_w_ <= 0;
    composite_ = std::move(bgra);
    composite_w_ = width;
    composite_h_ = height;
    if (width > 0 && was_empty) need_fit_ = true;
    Invalidate();
}

void PhotoCanvasSurface::SetStatusText(std::string text) {
    status_text_ = std::move(text);
    Invalidate();
}

void PhotoCanvasSurface::SetSelectionOutline(
    std::vector<ph::IntPoint> doc_points) {
    sel_outline_ = std::move(doc_points);
    Invalidate();
}

void PhotoCanvasSurface::SetLiveOutline(
    std::vector<ph::IntPoint> doc_points) {
    live_outline_ = std::move(doc_points);
    Invalidate();
}

void PhotoCanvasSurface::SetMaskOverlay(
    std::shared_ptr<const std::vector<std::uint8_t>> coverage,
    int width, int height) {
    mask_cov_ = std::move(coverage);
    mask_w_ = width;
    mask_h_ = height;
    Invalidate();
}

void PhotoCanvasSurface::SetMaskTint(Color color) {
    mask_tint_ = color;
    Invalidate();
}

void PhotoCanvasSurface::SetStrokePreview(std::vector<std::uint8_t> bgra,
                                          ph::IntRect doc_extent,
                                          float opacity) {
    stroke_bgra_ = std::move(bgra);
    stroke_ext_ = doc_extent;
    stroke_opacity_ = opacity;
    Invalidate();
}

void PhotoCanvasSurface::SetGradientLine(bool on, float ax, float ay,
                                         float bx, float by) {
    grad_line_on_ = on;
    grad_ax_ = ax;
    grad_ay_ = ay;
    grad_bx_ = bx;
    grad_by_ = by;
    Invalidate();
}

void PhotoCanvasSurface::SetSourceMarker(bool on,
                                         ph::IntPoint doc_point) {
    src_marker_on_ = on;
    src_marker_ = doc_point;
    Invalidate();
}

void PhotoCanvasSurface::SetBrushRing(float doc_diameter) {
    if (brush_ring_diameter_ == doc_diameter) return;
    brush_ring_diameter_ = doc_diameter;
    Invalidate();
}

void PhotoCanvasSurface::SetMovePreview(bool on,
                                        ph::IntRect layer_rect,
                                        float dx, float dy) {
    move_preview_on_ = on;
    move_rect_ = layer_rect;
    move_dx_ = dx;
    move_dy_ = dy;
    Invalidate();
}

void PhotoCanvasSurface::SetCropPreview(bool on,
                                        ph::IntRect doc_rect) {
    crop_on_ = on;
    crop_rect_ = doc_rect;
    Invalidate();
}

// ---- view state ----------------------------------------------------------

void PhotoCanvasSurface::SetZoom(float zoom) {
    const float next =
        zoom <= 0.0f ? 1.0f : std::clamp(zoom, kMinZoom, kMaxZoom);
    if (next == zoom_) return;
    zoom_ = next;
    if (zoom_ <= 1.0f) need_fit_ = true;   // re-center on fit
    if (on_zoom_changed_) on_zoom_changed_(zoom_);
    Invalidate();
}

void PhotoCanvasSurface::SetPanMode(bool on) { pan_mode_ = on; }

void PhotoCanvasSurface::SetOnZoomChanged(
    std::function<void(float)> cb) {
    on_zoom_changed_ = std::move(cb);
}

// ---- geometry ------------------------------------------------------------

float PhotoCanvasSurface::FitScale() const {
    const auto b = GetBounds();
    if (composite_w_ <= 0 || composite_h_ <= 0 || b.width <= 0.0f ||
        b.height <= 0.0f) {
        return 1.0f;
    }
    return std::min(b.width / composite_w_, b.height / composite_h_);
}

float PhotoCanvasSurface::Scale() const { return FitScale() * zoom_; }

Layout::Rect PhotoCanvasSurface::CanvasDest() const {
    if (composite_w_ <= 0 || composite_h_ <= 0) return {};
    const float s = Scale();
    return {pan_x_, pan_y_, composite_w_ * s, composite_h_ * s};
}

Layout::Point PhotoCanvasSurface::ClientToDoc(
    const Layout::Point& p) const {
    const Layout::Rect d = CanvasDest();
    const float s = Scale();
    if (s <= 1e-6f) return {0.0f, 0.0f};
    return {(p.x - d.x) / s, (p.y - d.y) / s};
}

Layout::Point PhotoCanvasSurface::DocToClient(float x, float y) const {
    const Layout::Rect d = CanvasDest();
    const float s = Scale();
    return {d.x + x * s, d.y + y * s};
}

void PhotoCanvasSurface::ZoomAt(Layout::Point center, float factor) {
    const float next =
        std::clamp(zoom_ * factor, kMinZoom, kMaxZoom);
    if (next == zoom_) return;
    const Layout::Rect before = CanvasDest();
    float ix = 0.5f, iy = 0.5f;
    if (before.width > 1e-6f && before.height > 1e-6f) {
        ix = std::clamp((center.x - before.x) / before.width, 0.0f,
                        1.0f);
        iy = std::clamp((center.y - before.y) / before.height, 0.0f,
                        1.0f);
    }
    zoom_ = next;
    const Layout::Rect after = CanvasDest();
    pan_x_ = center.x - ix * after.width;
    pan_y_ = center.y - iy * after.height;
    if (on_zoom_changed_) on_zoom_changed_(zoom_);
    Invalidate();
}

void PhotoCanvasSurface::EmitStatus(const std::string& text,
                                    bool is_error) {
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

// ---- checker cache --------------------------------------------------------

void PhotoCanvasSurface::EnsureChecker(int w, int h, int origin_x,
                                       int origin_y) {
    if (w <= 0 || h <= 0) {
        checker_bgra_.clear();
        checker_w_ = checker_h_ = -1;
        return;
    }
    // The checker is anchored to the canvas so pan moves it; the
    // pattern repeats every two 16px cells so the cache key is a
    // phase in [0,32).
    constexpr int kCell = 16;
    const int phase_x =
        ((origin_x % (kCell * 2)) + kCell * 2) % (kCell * 2);
    const int phase_y =
        ((origin_y % (kCell * 2)) + kCell * 2) % (kCell * 2);
    if (checker_w_ == w && checker_h_ == h && checker_ox_ == phase_x &&
        checker_oy_ == phase_y) {
        return;
    }
    checker_w_ = w;
    checker_h_ = h;
    checker_ox_ = phase_x;
    checker_oy_ = phase_y;
    checker_bgra_.resize(static_cast<std::size_t>(w) * h * 4);
    const std::uint8_t a = 205, b = 158;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int cx = (x + phase_x) / kCell;
            const int cy = (y + phase_y) / kCell;
            const std::uint8_t v = ((cx + cy) & 1) ? b : a;
            std::uint8_t* p =
                checker_bgra_.data() +
                (static_cast<std::size_t>(y) * w + x) * 4;
            p[0] = v;
            p[1] = v;
            p[2] = v;
            p[3] = 255;
        }
    }
}

// A8 coverage -> premultiplied tint + traced contour, keyed on the
// coverage buffer identity (monitor_surface parity).
void PhotoCanvasSurface::EnsureMaskOverlayBuilt(Color tint) {
    const void* key = mask_cov_.get();
    if (key == mask_built_for_ && built_tint_[0] == tint.r &&
        built_tint_[1] == tint.g && built_tint_[2] == tint.b &&
        built_tint_[3] == tint.a) {
        return;
    }
    mask_built_for_ = key;
    built_tint_[0] = tint.r;
    built_tint_[1] = tint.g;
    built_tint_[2] = tint.b;
    built_tint_[3] = tint.a;
    mask_bgra_.clear();
    mask_contour_.clear();
    if (mask_cov_ == nullptr || mask_w_ <= 0 || mask_h_ <= 0) return;
    const std::size_t count =
        static_cast<std::size_t>(mask_w_) * mask_h_;
    if (mask_cov_->size() < count) return;
    mask_bgra_.resize(count * 4);
    const float fill_alpha = tint.a;
    for (std::size_t i = 0; i < count; ++i) {
        const int a = static_cast<int>(
            static_cast<float>(mask_cov_->at(i)) * fill_alpha + 0.5f);
        std::uint8_t* px = mask_bgra_.data() + i * 4;
        px[0] = static_cast<std::uint8_t>(tint.b * 255.0f * a / 255.0f);
        px[1] = static_cast<std::uint8_t>(tint.g * 255.0f * a / 255.0f);
        px[2] = static_cast<std::uint8_t>(tint.r * 255.0f * a / 255.0f);
        px[3] = static_cast<std::uint8_t>(a);
    }
    for (const float tol : {1.5f, 4.0f, 10.0f}) {
        const auto traced =
            Lab::MaskToPolygon(*mask_cov_, mask_w_, mask_h_, tol);
        mask_contour_.clear();
        mask_contour_.reserve(traced.size());
        for (const auto& p : traced) {
            mask_contour_.push_back({p.x, p.y});
        }
        if (mask_contour_.size() <= 512) break;
    }
}

// ---- paint -----------------------------------------------------------------

void PhotoCanvasSurface::Paint(Rendering::Renderer& renderer,
                               const Layout::Rect& /*content*/,
                               const XER::UI::Theme::Tokens& tokens) {
    const auto b = GetBounds();
    renderer.FillRect(b, tokens.background, 0.0f);
    renderer.StrokeRect(b, tokens.border, 1.0f, 0.0f);

    if (composite_w_ <= 0 || composite_h_ <= 0 ||
        composite_.empty()) {
        Rendering::TextStyle style;
        style.color = tokens.muted;
        style.size = tokens.font_body;
        renderer.DrawText(
            status_text_,
            {b.x, b.y + b.height * 0.45f, b.width, 24.0f}, style);
        return;
    }

    const Layout::Rect dest = CanvasDest();
    renderer.PushClip(b);

    // Checkerboard under transparency, anchored to the canvas.
    const Layout::Rect clip = dest.Intersect(b);
    if (clip.width > 0.5f && clip.height > 0.5f) {
        const int ox =
            static_cast<int>(std::lround(clip.x - dest.x));
        const int oy =
            static_cast<int>(std::lround(clip.y - dest.y));
        const int cw = static_cast<int>(std::ceil(clip.width));
        const int ch = static_cast<int>(std::ceil(clip.height));
        EnsureChecker(cw, ch, ox, oy);
        if (!checker_bgra_.empty()) {
            renderer.DrawFrameBgra(checker_bgra_.data(), cw, ch, clip,
                                   1.0f);
        }
    }

    // Composite.
    renderer.DrawFrameBgra(composite_.data(), composite_w_,
                           composite_h_, dest, 1.0f);

    const float s = Scale();
    const auto to_client = [&](const ph::IntPoint& p) {
        return Layout::Point{dest.x + p.x * s, dest.y + p.y * s};
    };

    // Live stroke preview (scratch extent blit).
    if (!stroke_bgra_.empty() && !stroke_ext_.IsEmpty()) {
        const Layout::Rect sd{dest.x + stroke_ext_.x * s,
                              dest.y + stroke_ext_.y * s,
                              stroke_ext_.w * s, stroke_ext_.h * s};
        renderer.DrawFrameBgra(stroke_bgra_.data(), stroke_ext_.w,
                               stroke_ext_.h, sd, stroke_opacity_);
    }

    // Mask overlay: tint + traced boundary.
    if (mask_cov_ != nullptr && mask_w_ == composite_w_ &&
        mask_h_ == composite_h_) {
        EnsureMaskOverlayBuilt(mask_tint_);
        if (!mask_bgra_.empty()) {
            renderer.DrawFrameBgra(mask_bgra_.data(), mask_w_, mask_h_,
                                   dest, 1.0f);
        }
        if (mask_contour_.size() >= 2) {
            const Color edge = WithAlpha(tokens.accent, 0.9f);
            for (std::size_t i = 0; i < mask_contour_.size(); ++i) {
                const auto& a = mask_contour_[i];
                const auto& c =
                    mask_contour_[(i + 1) % mask_contour_.size()];
                renderer.DrawLine(to_client(a), to_client(c), edge,
                                  1.0f);
            }
        }
    }

    // Gradient line preview.
    if (grad_line_on_) {
        const Layout::Point ga = DocToClient(grad_ax_, grad_ay_);
        const Layout::Point gb = DocToClient(grad_bx_, grad_by_);
        renderer.DrawLine(ga, gb, tokens.accent, 2.0f);
        renderer.StrokeRect(
            {ga.x - 3.0f, ga.y - 3.0f, 6.0f, 6.0f}, tokens.accent,
            1.0f, 3.0f);
    }

    // Crop dim + frame.
    if (crop_on_ && !crop_rect_.IsEmpty()) {
        const Color dim{0.0f, 0.0f, 0.0f, 0.45f};
        const Layout::Rect cr_raw{
            dest.x + crop_rect_.x * s, dest.y + crop_rect_.y * s,
            crop_rect_.w * s, crop_rect_.h * s};
        const Layout::Rect cr = cr_raw.Intersect(dest).Intersect(b);
        // Four dim bands around the crop rect.
        const Layout::Rect bands[4] = {
            {dest.x, dest.y, dest.width, cr.y - dest.y},
            {dest.x, cr.Bottom(), dest.width, dest.Bottom() - cr.Bottom()},
            {dest.x, cr.y, cr.x - dest.x, cr.height},
            {cr.Right(), cr.y, dest.Right() - cr.Right(), cr.height}};
        for (const auto& band : bands) {
            if (band.width > 0.5f && band.height > 0.5f) {
                renderer.FillRect(band, dim, 0.0f);
            }
        }
        renderer.StrokeRect(cr, tokens.accent, 1.5f, 0.0f);
    }

    // Move preview: translated layer-extent frame.
    if (move_preview_on_ && !move_rect_.IsEmpty()) {
        const Layout::Rect mr{
            dest.x + (move_rect_.x + move_dx_) * s,
            dest.y + (move_rect_.y + move_dy_) * s,
            move_rect_.w * s, move_rect_.h * s};
        renderer.StrokeRect(mr, tokens.accent, 1.5f, 0.0f);
    }

    // Marching ants: live marquee polygon wins over the committed
    // outline while a gesture is in flight.
    const auto& poly =
        live_outline_.size() >= 2 ? live_outline_ : sel_outline_;
    if (poly.size() >= 2) {
        std::vector<Layout::Point> ants;
        ants.reserve(poly.size());
        for (const auto& p : poly) ants.push_back(to_client(p));
        const float phase = std::fmod(
            static_cast<float>(ant_phase_) * 40.0f, 8.0f);
        DrawAntsPolyline(renderer, ants, tokens.foreground, 4.0f,
                         phase, 1.0f);
        DrawAntsPolyline(renderer, ants, tokens.background, 4.0f,
                         phase + 4.0f, 1.0f);
    }

    // Clone/heal source marker.
    if (src_marker_on_) {
        const Layout::Point sc = to_client(src_marker_);
        renderer.DrawLine({sc.x - 6.0f, sc.y}, {sc.x + 6.0f, sc.y},
                          tokens.accent, 1.5f);
        renderer.DrawLine({sc.x, sc.y - 6.0f}, {sc.x, sc.y + 6.0f},
                          tokens.accent, 1.5f);
    }

    // Canvas hairline.
    renderer.StrokeRect(dest, WithAlpha(tokens.foreground, 0.3f),
                        1.0f, 0.0f);
    renderer.PopClip();

    // Brush ring (outside the canvas clip so it stays crisp at edges).
    if (cursor_in_ && brush_ring_diameter_ > 0.0f) {
        const float d =
            std::max(4.0f, brush_ring_diameter_ * Scale());
        const Layout::Rect ring{cursor_pos_.x - d * 0.5f,
                                cursor_pos_.y - d * 0.5f, d, d};
        renderer.StrokeRect(ring, tokens.foreground, 1.0f, d * 0.5f);
        renderer.StrokeRect(ring, WithAlpha(tokens.accent, 0.7f), 0.5f,
                            d * 0.5f);
    }
}

void PhotoCanvasSurface::Render(Rendering::Renderer& renderer,
                                const XER::UI::Theme::Tokens& tokens) {
    // The kit Surface child does the painting (its paint callback
    // calls Paint()); this widget only reserves its bounds.
    (void)renderer;
    (void)tokens;
}

void PhotoCanvasSurface::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    surface_->Layout(bounds);
    if (need_fit_ && composite_w_ > 0 && composite_h_ > 0 &&
        bounds.width > 0.0f && bounds.height > 0.0f) {
        need_fit_ = false;
        const float s = Scale();
        pan_x_ = bounds.x + (bounds.width - composite_w_ * s) * 0.5f;
        pan_y_ = bounds.y + (bounds.height - composite_h_ * s) * 0.5f;
    }
}

// ---- events -----------------------------------------------------------------

bool PhotoCanvasSurface::OnSurfaceEvent(const W::Event& event) {
    using Type = W::Event::Type;
    using Btn = W::MouseButton;
    const auto p = event.position;

    switch (event.type) {
        case Type::MouseWheel: {
            if (!GetBounds().Contains(p) || composite_w_ <= 0) {
                return false;
            }
            ZoomAt(p, event.wheel_delta > 0 ? 1.15f : 1.0f / 1.15f);
            return true;
        }

        case Type::MouseDown: {
            if (event.mouse_button == Btn::Right) {
                if (GetBounds().Contains(p) &&
                    hooks_.on_right_down != nullptr) {
                    hooks_.on_right_down(p);
                    return true;
                }
                return false;
            }
            if (event.mouse_button != Btn::Left &&
                event.mouse_button != Btn::Middle) {
                return false;
            }
            if (hooks_.on_activate) hooks_.on_activate();
            if (!GetBounds().Contains(p)) return false;
            if (event.mouse_button == Btn::Middle || pan_mode_) {
                panning_ = true;
                pan_last_ = p;
                return true;
            }
            const Layout::Point d = ClientToDoc(p);
            gesture_down_ = true;
            if (hooks_.on_canvas_down != nullptr) {
                hooks_.on_canvas_down(d.x, d.y, event.is_shift,
                                      event.is_alt);
            }
            return true;
        }

        case Type::MouseMove: {
            const bool in = GetBounds().Contains(p);
            if (in != cursor_in_) {
                cursor_in_ = in;
                Invalidate();
            }
            cursor_pos_ = p;
            if (panning_) {
                pan_x_ += p.x - pan_last_.x;
                pan_y_ += p.y - pan_last_.y;
                pan_last_ = p;
                Invalidate();
                return true;
            }
            if (gesture_down_) {
                const Layout::Point d = ClientToDoc(p);
                if (hooks_.on_canvas_drag != nullptr) {
                    hooks_.on_canvas_drag(d.x, d.y);
                }
                return true;
            }
            // Brush-ring tracking still needs a repaint.
            if (brush_ring_diameter_ > 0.0f) Invalidate();
            return in;
        }

        case Type::MouseUp: {
            if (panning_) {
                panning_ = false;
                return true;
            }
            if (gesture_down_) {
                gesture_down_ = false;
                const Layout::Point d = ClientToDoc(p);
                if (hooks_.on_canvas_up != nullptr) {
                    hooks_.on_canvas_up(d.x, d.y);
                }
                return true;
            }
            return false;
        }

        default:
            // Keys/text/focus route to the workspace (the kit Surface
            // holds focus after canvas presses).
            if (hooks_.on_event != nullptr) {
                return hooks_.on_event(event);
            }
            return false;
    }
}

bool PhotoCanvasSurface::HandleEvent(const W::Event&) { return false; }

void PhotoCanvasSurface::Tick(double dt_seconds) {
    if (!sel_outline_.empty() || !live_outline_.empty()) {
        ant_phase_ += dt_seconds;
        Invalidate();
    }
    surface_->Tick(dt_seconds);
}

// ===========================================================================
// PhotoWorkspace
// ===========================================================================

PhotoWorkspace::PhotoWorkspace(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
    brush_params_.color = ph::Pixel::RGBA(30, 80, 220, 255);
    BuildChildren();
}

PhotoWorkspace::~PhotoWorkspace() = default;

std::shared_ptr<PhotoWorkspace> PhotoWorkspace::Create(Hooks hooks) {
    return std::shared_ptr<PhotoWorkspace>(
        new PhotoWorkspace(std::move(hooks)));
}

// ---- construction -----------------------------------------------------------

void PhotoWorkspace::BuildChildren() {
    InstallButtonIconResolver();

    // ---- canvas ----
    canvas_ = PhotoCanvasSurface::Create();
    PhotoCanvasHooks ch;
    ch.on_canvas_down =
        [this](float x, float y, bool shift, bool alt) {
            OnCanvasDown(x, y, shift, alt);
        };
    ch.on_canvas_drag = [this](float x, float y) { OnCanvasDrag(x, y); };
    ch.on_canvas_up = [this](float x, float y) { OnCanvasUp(x, y); };
    ch.on_right_down = [this](Layout::Point p) { OnCanvasRightDown(p); };
    ch.on_activate = [] {};
    ch.on_status = [this](const std::string& t, bool e) {
        SetStatus(t, e);
    };
    ch.on_event = [this](const W::Event& ev) { return HandleEvent(ev); };
    canvas_->SetHooks(std::move(ch));
    canvas_->SetOnZoomChanged([this](float z) {
        if (zoom_label_ != nullptr) {
            zoom_label_->SetText(std::to_string(
                                     static_cast<int>(z * 100.0f)) +
                                 "%");
        }
    });
    AddChild(canvas_);

    // ---- top bar ----
    top_bar_ = LinearBox::Create(LinearBox::Direction::Horizontal);
    top_bar_->SetSpacing(4.0f);
    top_bar_->SetPadding({4.0f, 4.0f, 4.0f, 4.0f});
    auto small = [](const char* text, std::function<void()> fn,
                    const char* tip = nullptr) {
        auto b = std::make_shared<W::Button>();
        b->SetText(text);
        b->SetSize(W::ButtonSize::Small);
        b->SetStyle(W::ButtonStyle::Ghost);
        if (tip != nullptr) b->SetTooltip(tip);
        b->SetOnClick(std::move(fn));
        return b;
    };
    // Icon-only toolbar button: no text (the strip is dense — the
    // tooltip carries the full name), Small ghost chrome, preferred
    // width so the layout never squeezes the glyph.
    auto icon_btn = [](const char* icon, std::function<void()> fn,
                       const char* tip) {
        auto b = std::make_shared<W::Button>();
        b->SetIcon(icon);
        b->SetSize(W::ButtonSize::Small);
        b->SetStyle(W::ButtonStyle::Ghost);
        b->SetTooltip(tip);
        b->SetOnClick(std::move(fn));
        return b;
    };
    new_btn_ = icon_btn("add", [this] { NewDocumentDialog(); },
                        "New document (Ctrl+N)");
    open_btn_ = icon_btn("file_open", [this] { OpenDialog(); },
                         "Open .xerphoto / image (Ctrl+O)");
    save_btn_ = icon_btn("save", [this] { Save(false); },
                         "Save .xerphoto (Ctrl+S)");
    save_btn_->SetMenu({
        W::ContextMenu::Item{"Save", [this] { Save(false); }, true,
                             false, false, "Ctrl+S"},
        W::ContextMenu::Item{"Save As…",
                             [this] { Save(true); }, true,
                             false, false, "Ctrl+Shift+S"},
        W::ContextMenu::Item{"Export PNG…",
                             [this] { ExportPng(); }, true, true,
                             false, ""},
    });
    undo_btn_ = icon_btn("undo", [this] { Undo(); }, "Undo (Ctrl+Z)");
    redo_btn_ = icon_btn("redo", [this] { Redo(); }, "Redo (Ctrl+Y)");

    // Preferred-width slots: icon buttons measure ~28px (glyph +
    // padding, +chevron for the menu button) — fixed guesses used to
    // squeeze them under their honest minimum.
    top_bar_->Add(new_btn_, {});
    top_bar_->Add(open_btn_, {});
    top_bar_->Add(save_btn_, {});
    top_bar_->Add(undo_btn_, {});
    top_bar_->Add(redo_btn_, {});

    auto field_label = [this](const char* text) {
        auto l = std::make_shared<W::Label>(text, true);
        // Preferred-width slot: the label keeps its measured extent
        // instead of wrapping mid-word inside a guessed 30px box.
        top_bar_->Add(l, {});
        return l;
    };
    auto opt_field = [this](float value, float lo, float hi,
                            float step, const char* fmt,
                            std::function<void(float)> on_commit) {
        auto f = W::ScrubNumberField::Create();
        f->SetRange(lo, hi);
        f->SetStep(step);
        f->SetFormat(fmt);
        f->SetValue(value);
        f->SetOnValueChange(on_commit);
        f->SetOnValueCommitted(on_commit);
        top_bar_->Add(f, {76.0f, 0.0f});
        return f;
    };
    size_label_ = field_label("Size");
    size_field_ =
        opt_field(32.0f, 1.0f, 400.0f, 1.0f, "%.0f",
                  [this](float v) {
                      brush_params_.diameter_px = v;
                      canvas_->SetBrushRing(
                          (tool_ == Tool::Brush || tool_ == Tool::Eraser ||
                           tool_ == Tool::CloneStamp ||
                           tool_ == Tool::Healing || tool_ == Tool::Spot)
                              ? v
                              : 0.0f);
                  });
    hard_label_ = field_label("Hard");
    hard_field_ =
        opt_field(0.8f, 0.0f, 1.0f, 0.01f, "%.2f",
                  [this](float v) { brush_params_.hardness = v; });
    opacity_label_ = field_label("Op");
    opacity_field_ =
        opt_field(1.0f, 0.0f, 1.0f, 0.01f, "%.2f",
                  [this](float v) { brush_params_.opacity = v; });
    tol_label_ = field_label("Tol");
    tol_field_ = opt_field(32.0f, 0.0f, 255.0f, 1.0f, "%.0f",
                           [this](float v) { wand_tol_ = v; });
    feather_label_ = field_label("Fthr");
    feather_field_ = opt_field(0.0f, 0.0f, 64.0f, 0.5f, "%.1f",
                               [this](float v) { sel_feather_ = v; });
    invert_sel_btn_ = small("Inv", [this] { InvertSelection(); },
                            "Invert selection");
    deselect_btn_ = small("None", [this] { Deselect(); },
                          "Deselect (Ctrl+D)");
    top_bar_->Add(invert_sel_btn_, {});
    top_bar_->Add(deselect_btn_, {});

    // Flexible spacer, then fg chip + zoom readout.
    auto spacer = std::make_shared<W::Label>("");
    top_bar_->Add(spacer, {0.0f, 1.0f});
    auto chip = std::make_shared<ColorChip>();
    chip->SetColor(ToColor(brush_params_.color));
    chip->SetTooltip("Foreground color — click to pick");
    chip->SetOnClick([this, chip] {
        color_picker_ = std::make_shared<W::ColorPicker>();
        color_picker_->SetColor(ToColor(brush_params_.color));
        color_picker_->SetOnColorChange([this, chip](Color c) {
            SetForegroundColor(ToPixel(c));
            chip->SetColor(ToColor(brush_params_.color));
        });
        color_picker_->SetOnColorCommitted(
            [this, chip](Color c) {
                SetForegroundColor(ToPixel(c));
                chip->SetColor(ToColor(brush_params_.color));
            });
        dialog_ = W::Dialog::Create("Foreground Color", color_picker_,
                                    "Done", "");
        dialog_->SetPanelWidth(320.0f);
        dialog_->SetPanelHeight(420.0f);
        dialog_->SetOnClosed([this] {
            color_picker_.reset();
            HideOverlay();
        });
        ShowOverlay(dialog_);
    });
    fg_btn_ = chip;   // stored in the Button slot as a Widget
    top_bar_->Add(chip, {34.0f, 0.0f});
    zoom_label_ = std::make_shared<W::Label>("100%", true);
    top_bar_->Add(zoom_label_, {52.0f, 0.0f});
    AddChild(top_bar_);

    // ---- tools column ----
    // Icon-only checkable strip (no text labels — the tool name lives
    // in the tooltip) inside a narrow ScrollArea so short windows
    // scroll instead of squeezing the buttons. The checked latch gives
    // the selected-tool highlight.
    tools_col_ = LinearBox::Create(LinearBox::Direction::Vertical);
    tools_col_->SetSpacing(2.0f);
    tools_col_->SetPadding({4.0f, 4.0f, 4.0f, 4.0f});
    tool_btns_.reserve(kToolCount);
    for (int i = 0; i < kToolCount; ++i) {
        const ToolInfo& info = kToolInfos[i];
        auto b = std::make_shared<W::Button>();
        b->SetSize(W::ButtonSize::Small);
        b->SetStyle(W::ButtonStyle::Ghost);
        b->SetCheckable(true);
        if (info.icon != nullptr && info.icon[0] != '\0') {
            b->SetIcon(info.icon);
        }
        b->SetTooltip(info.tooltip);
        b->SetOnClick([this, t = info.tool] { SetTool(t); });
        tool_btns_.push_back(b);
        tools_col_->Add(b, {30.0f, 0.0f});
    }
    tools_scroll_ = std::make_shared<W::ScrollArea>(tools_col_);
    AddChild(tools_scroll_);

    // ---- right dock ----
    right_dock_ = DockTabGroup::Create();
    AddChild(right_dock_);

    // ---- layers panel ----
    auto layers = std::make_shared<LayersPanel>();
    layers_panel_ = layers;

    // Two rows of five — ten Small buttons can't share one 304px row
    // at their honest 56px minimum, so the strip wraps instead of
    // squeezing every label mid-word.
    auto toolbar = LinearBox::Create(LinearBox::Direction::Vertical);
    toolbar->SetSpacing(2.0f);
    toolbar->SetPadding({3.0f, 3.0f, 3.0f, 3.0f});
    auto trow = [&] {
        auto r = LinearBox::Create(LinearBox::Direction::Horizontal);
        r->SetSpacing(2.0f);
        toolbar->Add(r, {0.0f, 0.0f});
        return r;
    };
    auto row1 = trow();
    auto row2 = trow();
    auto tbtn = [&](const std::shared_ptr<LinearBox>& row,
                    const char* text, const char* icon,
                    std::function<void()> fn, const char* tip) {
        auto b = std::make_shared<W::Button>();
        b->SetText(text);
        if (icon != nullptr) b->SetIcon(icon);
        b->SetSize(W::ButtonSize::Small);
        b->SetStyle(W::ButtonStyle::Ghost);
        b->SetTooltip(tip);
        b->SetOnClick(std::move(fn));
        row->Add(b, {0.0f, 1.0f});
        return b;
    };
    add_layer_btn_ = tbtn(row1, "+Lyr", "add",
                          [this] { AddLayer(false); },
                          "Add raster layer");
    (void)tbtn(row1, "+Grp", "create_new_folder",
               [this] { AddLayer(true); }, "Add group");
    add_adj_btn_ = std::make_shared<W::Button>();
    add_adj_btn_->SetText("+Adj");
    add_adj_btn_->SetSize(W::ButtonSize::Small);
    add_adj_btn_->SetStyle(W::ButtonStyle::Ghost);
    add_adj_btn_->SetTooltip("Add adjustment layer");
    {
        std::vector<W::ContextMenu::Item> items;
        for (const auto& op : ph::AdjustmentOpRegistry()) {
            items.push_back(W::ContextMenu::Item{
                op.label,
                [this, name = op.name] { AddAdjustmentLayer(name); },
                true, false, false, ""});
        }
        add_adj_btn_->SetMenu(std::move(items));
    }
    row1->Add(add_adj_btn_, {0.0f, 1.0f});
    dup_btn_ = tbtn(row1, "Dup", nullptr,
                    [this] { DuplicateActiveLayer(); },
                    "Duplicate active layer");
    del_btn_ = tbtn(row1, "Del", "delete_forever",
                    [this] { DeleteActiveLayer(); },
                    "Delete active layer (Delete)");
    up_btn_ = tbtn(row2, "Up", "edit_arrow_up",
                   [this] { MoveActiveLayer(+1); }, "Move layer up");
    down_btn_ = tbtn(row2, "Dn", nullptr,
                     [this] { MoveActiveLayer(-1); }, "Move layer down");
    merge_btn_ = tbtn(row2, "Mrg", nullptr,
                      [this] { MergeActiveLayer(); }, "Merge down");
    flatten_btn_ = tbtn(row2, "Flt", "layers",
                        [this] { FlattenImage(); }, "Flatten image");
    mask_btn_ = tbtn(row2, "Mask", nullptr,
                     [this] { ToggleActiveLayerMask(); },
                     "Attach / detach layer mask");

    layers_tree_ = W::TreeView::Create();
    layers_tree_->SetRowHeight(kRowH);
    layers_tree_->SetOnSelectionChanged([this] {
        const std::string id = layers_tree_->GetSelectedId();
        if (id.empty()) return;
        SelectLayer(static_cast<ph::LayerId>(
            std::strtoull(id.c_str(), nullptr, 10)));
    });

    auto chrome = std::make_shared<LayerChrome>();
    chrome->flat_ids = [this] {
        std::vector<ph::LayerId> ids;
        FlattenRows(&ids);
        return ids;
    };
    chrome->find_node = [this](ph::LayerId id) -> const ph::LayerNode* {
        return doc_ != nullptr ? doc_->FindLayer(id) : nullptr;
    };
    chrome->mask_editing = [this](ph::LayerId id) {
        return mask_editing_ && id == active_layer_;
    };
    chrome->scroll_y = [this] {
        return layers_tree_->GetList() != nullptr
                   ? layers_tree_->GetList()->GetScrollY()
                   : 0.0f;
    };
    chrome->on_action = [this](ph::LayerId id, LayerChrome::Zone z) {
        const ph::LayerNode* node =
            doc_ != nullptr ? doc_->FindLayer(id) : nullptr;
        if (node == nullptr) return;
        if (z == LayerChrome::Zone::Mask) {
            if (id != active_layer_) {
                SelectLayer(id);
            } else {
                SetMaskEditing(!mask_editing_);
            }
            return;
        }
        ph::SetLayerAttributesCommand::Attributes a{
            node->Name(), node->Visible(), node->Locked(),
            node->Opacity(), node->BlendMode(), node->Adjustment()};
        if (z == LayerChrome::Zone::Eye) a.visible = !a.visible;
        if (z == LayerChrome::Zone::Lock) a.locked = !a.locked;
        ApplyLayerAttributes(
            id, a,
            z == LayerChrome::Zone::Eye ? "Toggle Visibility"
                                        : "Toggle Lock");
    };
    layer_chrome_ = chrome;

    auto strip = LinearBox::Create(LinearBox::Direction::Vertical);
    strip->SetSpacing(2.0f);
    strip->SetPadding({4.0f, 4.0f, 4.0f, 4.0f});
    auto attr_row1 = LinearBox::Create(LinearBox::Direction::Horizontal);
    attr_row1->SetSpacing(4.0f);
    eye_btn_ = std::make_shared<W::Button>();
    eye_btn_->SetIcon("visibility");
    eye_btn_->SetSize(W::ButtonSize::Small);
    eye_btn_->SetStyle(W::ButtonStyle::Ghost);
    eye_btn_->SetCheckable(true);
    eye_btn_->SetTooltip("Layer visibility");
    eye_btn_->SetOnClick([this] {
        ph::LayerNode* n = ActiveLayer();
        if (n == nullptr) return;
        ph::SetLayerAttributesCommand::Attributes a{
            n->Name(), !n->Visible(), n->Locked(), n->Opacity(),
            n->BlendMode(), n->Adjustment()};
        ApplyLayerAttributes(n->Id(), a, "Toggle Visibility");
    });
    lock_btn_ = std::make_shared<W::Button>();
    lock_btn_->SetIcon("lock_open");
    lock_btn_->SetSize(W::ButtonSize::Small);
    lock_btn_->SetStyle(W::ButtonStyle::Ghost);
    lock_btn_->SetCheckable(true);
    lock_btn_->SetTooltip("Lock layer against pixel ops");
    lock_btn_->SetOnClick([this] {
        ph::LayerNode* n = ActiveLayer();
        if (n == nullptr) return;
        ph::SetLayerAttributesCommand::Attributes a{
            n->Name(), n->Visible(), !n->Locked(), n->Opacity(),
            n->BlendMode(), n->Adjustment()};
        ApplyLayerAttributes(n->Id(), a, "Toggle Lock");
    });
    name_input_ = std::make_shared<W::TextInput>("");
    name_input_->SetPlaceholder("Layer name");
    name_input_->SetOnSubmit([this] {
        ph::LayerNode* n = ActiveLayer();
        if (n == nullptr) return;
        const std::string next = name_input_->GetValue();
        if (next.empty() || next == n->Name()) return;
        ph::SetLayerAttributesCommand::Attributes a{
            next, n->Visible(), n->Locked(), n->Opacity(),
            n->BlendMode(), n->Adjustment()};
        ApplyLayerAttributes(n->Id(), a, "Rename Layer");
    });
    attr_row1->Add(eye_btn_, {28.0f, 0.0f});
    attr_row1->Add(lock_btn_, {28.0f, 0.0f});
    attr_row1->Add(name_input_, {0.0f, 1.0f});
    strip->Add(attr_row1, {28.0f, 0.0f});

    auto attr_row2 = LinearBox::Create(LinearBox::Direction::Horizontal);
    attr_row2->SetSpacing(4.0f);
    layer_opacity_field_ = W::ScrubNumberField::Create();
    layer_opacity_field_->SetRange(0.0f, 1.0f);
    layer_opacity_field_->SetStep(0.01f);
    layer_opacity_field_->SetFormat("%.2f");
    layer_opacity_field_->SetTooltip("Layer opacity");
    layer_opacity_field_->SetOnValueChange([this](float v) {
        ph::LayerNode* n = ActiveLayer();
        if (n == nullptr) return;
        if (gesture_opacity_ < 0.0f) gesture_opacity_ = n->Opacity();
        n->SetOpacity(v);           // live preview (commit restores)
        OnDocumentEdited();
    });
    layer_opacity_field_->SetOnValueCommitted([this](float v) {
        ph::LayerNode* n = ActiveLayer();
        if (n == nullptr || hist_ == nullptr) return;
        const float prev = gesture_opacity_ >= 0.0f
                               ? gesture_opacity_
                               : n->Opacity();
        gesture_opacity_ = -1.0f;
        n->SetOpacity(prev);   // restore so the command captures prev
        ph::SetLayerAttributesCommand::Attributes a{
            n->Name(), n->Visible(), n->Locked(), v, n->BlendMode(),
            n->Adjustment()};
        ApplyLayerAttributes(n->Id(), a, "Layer Opacity");
    });
    blend_select_ = std::make_shared<W::Select>(
        std::vector<std::string>{"srcOver", "multiply", "screen",
                                 "overlay", "darken", "lighten",
                                 "softLight", "hardLight", "difference",
                                 "exclusion", "hue", "saturation",
                                 "color", "luminosity", "plus",
                                 "srcIn", "dstOut", "clear"},
        0);
    blend_select_->SetTooltip("Layer blend mode");
    blend_select_->SetOnChanged([this](int /*index*/) {
        if (suppress_adj_sync_) return;
        ph::LayerNode* n = ActiveLayer();
        if (n == nullptr) return;
        ph::SetLayerAttributesCommand::Attributes a{
            n->Name(), n->Visible(), n->Locked(), n->Opacity(),
            blend_select_->GetSelectedText(), n->Adjustment()};
        ApplyLayerAttributes(n->Id(), a, "Blend Mode");
    });
    attr_row2->Add(layer_opacity_field_, {0.0f, 1.0f});
    attr_row2->Add(blend_select_, {104.0f, 0.0f});
    strip->Add(attr_row2, {28.0f, 0.0f});

    layers->SetRows(toolbar, layers_tree_, chrome, strip);

    // ---- adjustments panel ----
    adjust_panel_ = LinearBox::Create(LinearBox::Direction::Vertical);
    adjust_panel_->SetSpacing(4.0f);
    adjust_panel_->SetPadding({4.0f, 4.0f, 4.0f, 4.0f});
    RebuildAdjustmentCards();
    adjust_scroll_ =
        std::make_shared<W::ScrollArea>(adjust_panel_);

    right_dock_->AddTab("Layers", "Layer tree + mask + attributes",
                        layers_panel_);
    right_dock_->AddTab("Adjust", "Adjustment ops (collapsible cards)",
                        adjust_scroll_);

    // ---- eyedropper overlay ----
    eyedropper_ = std::make_shared<W::Eyedropper>();
    eyedropper_->SetSampleSource([this](float x, float y) -> Color {
        const Layout::Point d = canvas_->ClientToDoc({x, y});
        const int dx = static_cast<int>(std::floor(d.x));
        const int dy = static_cast<int>(std::floor(d.y));
        if (doc_ == nullptr || dx < 0 || dy < 0 ||
            dx >= doc_->CanvasWidth() || dy >= doc_->CanvasHeight()) {
            return {0, 0, 0, 0};
        }
        const auto flat = doc_->FlattenU32();
        const std::uint32_t v =
            flat[static_cast<std::size_t>(dy) * doc_->CanvasWidth() +
                 dx];
        return Color{((v >> 24) & 0xFF) / 255.0f,
                     ((v >> 16) & 0xFF) / 255.0f,
                     ((v >> 8) & 0xFF) / 255.0f, 1.0f};
    });
    eyedropper_->SetOnPick([this](Color c) {
        ph::Pixel p = ToPixel(c);
        p.a = 255;
        SetForegroundColor(p);
        if (auto* chip = dynamic_cast<ColorChip*>(fg_btn_.get())) {
            chip->SetColor(ToColor(brush_params_.color));
        }
        SetStatus("Picked fg color");
    });
    eyedropper_->SetOnCancel([this] {
        SetStatus("Eyedropper canceled");
    });
    // Starts hidden + inactive: it spans the canvas bounds, so a
    // visible-but-inactive loupe would swallow every canvas pointer
    // event (the tree delivers to the deepest hit target only).
    eyedropper_->SetVisible(false);
    eyedropper_->SetActive(false);
    AddChild(eyedropper_);

    SyncToolButtons();
    SyncTopBar();
}

// ---- status / chrome sync ------------------------------------------------

void PhotoWorkspace::SetStatus(const std::string& text, bool is_error) {
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void PhotoWorkspace::SyncToolButtons() {
    for (int i = 0; i < kToolCount; ++i) {
        tool_btns_[i]->SetChecked(kToolInfos[i].tool == tool_);
    }
}

int PhotoWorkspace::IconOnlyToolButtonCount() const {
    int n = 0;
    for (const auto& b : tool_btns_) {
        if (b != nullptr && b->GetText().empty() &&
            !b->GetIcon().empty() && !b->GetTooltip().empty()) {
            ++n;
        }
    }
    return n;
}

void PhotoWorkspace::SyncTopBar() {
    const bool brushy = tool_ == Tool::Brush || tool_ == Tool::Eraser;
    const bool retouchy = tool_ == Tool::CloneStamp ||
                          tool_ == Tool::Healing || tool_ == Tool::Spot;
    const bool selecty = tool_ == Tool::SelectRect ||
                         tool_ == Tool::SelectEllipse ||
                         tool_ == Tool::Lasso ||
                         tool_ == Tool::PolyLasso ||
                         tool_ == Tool::MagicWand;
    const auto vis = [](const std::shared_ptr<W::Widget>& w, bool on) {
        if (w != nullptr) w->SetVisible(on);
    };
    vis(size_label_, brushy || retouchy);
    vis(size_field_, brushy || retouchy);
    vis(hard_label_, brushy);
    vis(hard_field_, brushy);
    vis(opacity_label_, brushy || tool_ == Tool::Fill ||
                            tool_ == Tool::Gradient);
    vis(opacity_field_, brushy || tool_ == Tool::Fill ||
                            tool_ == Tool::Gradient);
    vis(tol_label_, tool_ == Tool::MagicWand);
    vis(tol_field_, tool_ == Tool::MagicWand);
    vis(feather_label_, selecty && tool_ != Tool::MagicWand);
    vis(feather_field_, selecty && tool_ != Tool::MagicWand);
    vis(invert_sel_btn_, selecty);
    vis(deselect_btn_, selecty);
    vis(fg_btn_, !selecty && tool_ != Tool::Eyedropper &&
                     tool_ != Tool::Crop);
    if (undo_btn_ != nullptr) undo_btn_->SetEnabled(CanUndo());
    if (redo_btn_ != nullptr) redo_btn_->SetEnabled(CanRedo());
    canvas_->SetPanMode(tool_ == Tool::Pan);
    canvas_->SetBrushRing(
        (brushy || retouchy) ? brush_params_.diameter_px : 0.0f);
    if (eyedropper_ != nullptr) {
        // The loupe overlays the whole canvas rect — hide it while
        // inactive or it wins hit-testing and starves the canvas.
        const bool on = tool_ == Tool::Eyedropper;
        eyedropper_->SetVisible(on);
        eyedropper_->SetActive(on);
    }
}

// ---- document plumbing -----------------------------------------------------

void PhotoWorkspace::SetDocument(std::unique_ptr<ph::Document> doc,
                                 std::string display_name) {
    doc_ = std::move(doc);
    hist_.reset();
    if (doc_ != nullptr) {
        hist_ = std::make_unique<ph::PhotoHistory>(*doc_);
    }
    doc_name_ = std::move(display_name);
    doc_dirty_ = false;
    active_layer_ = FallbackLayerId();
    stroking_ = false;
    stroking_mask_ = false;
    gradient_drag_ = false;
    retouch_drag_ = false;
    marquee_drag_ = false;
    poly_active_ = false;
    poly_pts_.clear();
    lasso_pts_.clear();
    clone_src_set_ = false;
    mask_editing_ = false;
    move_drag_ = false;
    crop_drag_ = false;
    crop_rect_ = {};
    canvas_->SetCropPreview(false, {});
    canvas_->SetMovePreview(false, {}, 0.0f, 0.0f);
    canvas_->SetGradientLine(false, 0, 0, 0, 0);
    canvas_->SetSourceMarker(false, {});
    canvas_->SetStrokePreview({}, {}, 1.0f);
    canvas_->SetMaskOverlay(nullptr, 0, 0);
    pre_gesture_spec_.reset();
    EnsureSelectionDevice();
    RefreshSelectionState();
    Reflatten();
    RebuildLayerTree();
    SyncLayerControls();
    SyncAdjustmentBinding();
    SyncTopBar();
    RequestLayout();
    Invalidate();
}

void PhotoWorkspace::EnsureDocument() {
    if (doc_ != nullptr) return;
    SetDocument(std::make_unique<ph::Document>(1280, 720), "Untitled");
    if (doc_ != nullptr && doc_->Root().Children().empty()) {
        AddLayerUndoable(ph::LayerKind::kRaster, "Layer 1",
                         &active_layer_);
        RebuildLayerTree();
        SyncLayerControls();
        Invalidate();
    }
}

ph::LayerId PhotoWorkspace::FallbackLayerId() const {
    if (doc_ == nullptr) return ph::kInvalidLayerId;
    const auto* node = &doc_->Root();
    while (!node->Children().empty()) {
        node = node->Children().back().get();
    }
    return node != &doc_->Root() ? node->Id() : ph::kInvalidLayerId;
}

ph::LayerNode* PhotoWorkspace::ActiveLayer() {
    if (doc_ == nullptr) return nullptr;
    return doc_->FindLayer(active_layer_);
}

void PhotoWorkspace::SelectLayer(ph::LayerId id) {
    if (id == active_layer_) return;
    active_layer_ = id;
    if (mask_editing_) {
        const ph::LayerNode* n =
            doc_ != nullptr ? doc_->FindLayer(id) : nullptr;
        if (n == nullptr || n->Mask() == nullptr) {
            mask_editing_ = false;
            canvas_->SetMaskOverlay(nullptr, 0, 0);
        } else {
            RebuildMaskOverlay();
        }
    }
    SyncLayerControls();
    SyncAdjustmentBinding();
    if (layers_tree_ != nullptr && layer_chrome_ != nullptr) {
        layers_tree_->SetSelectedId(std::to_string(id));
        layer_chrome_->Invalidate();
    }
    Invalidate();
}

void PhotoWorkspace::OnDocumentEdited() {
    doc_dirty_ = true;
    Reflatten();
    RebuildLayerTree();
    SyncLayerControls();
    if (layer_chrome_ != nullptr) {
        layer_chrome_->Invalidate();
    }
    Invalidate();
}

void PhotoWorkspace::RefreshCanvasComposite() {
    if (doc_ == nullptr) {
        canvas_->SetComposite({}, 0, 0);
        return;
    }
    const image::ImageBuffer flat = doc_->Flatten();
    if (!flat.IsValid() ||
        flat.Format() != image::PixelFormat::RGBA8) {
        canvas_->SetComposite({}, 0, 0);
        return;
    }
    const int w = flat.Width();
    const int h = flat.Height();
    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(w) * h * 4);
    const std::uint8_t* s = flat.Data();
    std::uint8_t* d = bgra.data();
    const std::size_t px = static_cast<std::size_t>(w) * h;
    for (std::size_t i = 0; i < px; ++i) {
        d[i * 4 + 0] = s[i * 4 + 2];  // B
        d[i * 4 + 1] = s[i * 4 + 1];  // G
        d[i * 4 + 2] = s[i * 4 + 0];  // R
        d[i * 4 + 3] = s[i * 4 + 3];  // A
    }
    canvas_->SetComposite(std::move(bgra), w, h);
}

void PhotoWorkspace::Reflatten() {
    RefreshCanvasComposite();
    if (mask_editing_) RebuildMaskOverlay();
    RefreshSelectionState();
    canvas_->Invalidate();
}

// ---- file ops ---------------------------------------------------------------

void PhotoWorkspace::NewDocument(int width, int height) {
    doc_path_.clear();
    SetDocument(std::make_unique<ph::Document>(
                    std::clamp(width, 1, 16384),
                    std::clamp(height, 1, 16384)),
                "Untitled");
    AddLayerUndoable(ph::LayerKind::kRaster, "Layer 1",
                     &active_layer_);
    RebuildLayerTree();
    SyncLayerControls();
    doc_dirty_ = false;
    SetStatus("New document " + std::to_string(width) + "x" +
              std::to_string(height));
}

void PhotoWorkspace::NewDocumentDialog() {
    auto fields = std::make_shared<NewDocFields>();
    auto dlg = W::Dialog::Create("New Photo Document", fields,
                                 "Create", "Cancel");
    dlg->SetPanelWidth(360.0f);
    std::weak_ptr<NewDocFields> weak_fields = fields;
    std::weak_ptr<W::Dialog> weak_dlg = dlg;
    dlg->SetOnClosed([this, weak_dlg, weak_fields] {
        auto d = weak_dlg.lock();
        auto f = weak_fields.lock();
        HideOverlay();
        if (d == nullptr || f == nullptr) return;
        if (!d->WasConfirmed()) return;
        NewDocument(f->Width(), f->Height());
    });
    dialog_ = dlg;
    ShowOverlay(dlg);
}

void PhotoWorkspace::OpenDialog() {
#ifdef _WIN32
    const std::wstring picked = PickOpenFileW(
        nullptr,
        FilterSpec(L"Photo documents and images",
                   L"*.xerphoto;*.png;*.jpg;*.jpeg;*.jpe;*.bmp;"
                   L"*.webp;*.tif;*.tiff;*.gif;*.avif;*.psd;*.ora"),
        L"Open photo document or image");
    if (picked.empty()) return;
    OpenPath(XER::Helper::Path::ToUtf8(
        std::filesystem::path(picked)));
#else
    SetStatus("File dialogs are Windows-only", true);
#endif
}

bool PhotoWorkspace::OpenPath(const std::string& utf8_path) {
    const std::string path =
        XER::Helper::Path::NormalizeUtf8Path(utf8_path);
    std::string err;
    std::unique_ptr<ph::Document> doc;
    const std::string lower = [&] {
        std::string s = path;
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        return s;
    }();
    const bool native = lower.size() >= 9 &&
                        lower.compare(lower.size() - 9, 9,
                                      ".xerphoto") == 0;
    doc = native ? ph::LoadDocument(path, &err)
                 : ph::Document::OpenPng(path, &err);
    if (doc == nullptr) {
        SetStatus("Open failed: " + (err.empty() ? path : err), true);
        return false;
    }
    SetDocument(std::move(doc), Basename(path));
    if (native) {
        doc_path_ = path;
    } else {
        doc_path_.clear();
        doc_dirty_ = true;
    }
    SetStatus("Opened " + Basename(path));
    return true;
}

bool PhotoWorkspace::Save(bool choose_path) {
    if (doc_ == nullptr) {
        SetStatus("No document to save", true);
        return false;
    }
    std::string path = doc_path_;
    if (choose_path || path.empty()) {
#ifdef _WIN32
        const std::wstring initial =
            XER::Helper::Path::FromUtf8(
                doc_name_.empty() ? "Untitled.xerphoto"
                                  : doc_name_ + ".xerphoto")
                .wstring();
        const std::wstring picked = PickSaveFileW(
            nullptr, FilterSpec(L"XER Photo Document", L"*.xerphoto"),
            L"Save photo document", L"xerphoto", initial);
        if (picked.empty()) return false;
        path = XER::Helper::Path::ToUtf8(std::filesystem::path(picked));
#else
        SetStatus("Save dialogs are Windows-only", true);
        return false;
#endif
    }
    std::string err;
    if (!ph::SaveDocument(*doc_, path, &err)) {
        SetStatus("Save failed: " + err, true);
        return false;
    }
    doc_path_ = path;
    doc_name_ = Basename(path);
    doc_dirty_ = false;
    SetStatus("Saved " + Basename(path));
    Invalidate();
    return true;
}

bool PhotoWorkspace::ExportPng() {
    if (doc_ == nullptr) {
        SetStatus("No document to export", true);
        return false;
    }
#ifdef _WIN32
    const std::wstring picked = PickSaveFileW(
        nullptr, FilterSpec(L"PNG image", L"*.png"), L"Export PNG",
        L"png",
        XER::Helper::Path::FromUtf8(
            doc_name_.empty() ? "Untitled.png" : doc_name_ + ".png")
            .wstring());
    if (picked.empty()) return false;
    const std::string path =
        XER::Helper::Path::ToUtf8(std::filesystem::path(picked));
    std::string err;
    if (!doc_->SavePng(path, &err)) {
        SetStatus("Export failed: " + err, true);
        return false;
    }
    SetStatus("Exported " + Basename(path));
    return true;
#else
    SetStatus("Export dialogs are Windows-only", true);
    return false;
#endif
}

// ---- undo -------------------------------------------------------------------

void PhotoWorkspace::Undo() {
    if (hist_ == nullptr || !hist_->Undo()) {
        SetStatus("Nothing to undo");
        return;
    }
    if (doc_ != nullptr && doc_->FindLayer(active_layer_) == nullptr) {
        active_layer_ = FallbackLayerId();
    }
    doc_dirty_ = true;
    Reflatten();
    RebuildLayerTree();
    SyncLayerControls();
    SyncAdjustmentBinding();
    SyncTopBar();
    Invalidate();
}

void PhotoWorkspace::Redo() {
    if (hist_ == nullptr || !hist_->Redo()) {
        SetStatus("Nothing to redo");
        return;
    }
    if (doc_ != nullptr && doc_->FindLayer(active_layer_) == nullptr) {
        active_layer_ = FallbackLayerId();
    }
    doc_dirty_ = true;
    Reflatten();
    RebuildLayerTree();
    SyncLayerControls();
    SyncAdjustmentBinding();
    SyncTopBar();
    Invalidate();
}

bool PhotoWorkspace::CanUndo() const {
    return hist_ != nullptr && hist_->CanUndo();
}
bool PhotoWorkspace::CanRedo() const {
    return hist_ != nullptr && hist_->CanRedo();
}
std::string PhotoWorkspace::UndoLabel() const {
    return hist_ != nullptr ? hist_->UndoLabel() : std::string();
}
std::string PhotoWorkspace::RedoLabel() const {
    return hist_ != nullptr ? hist_->RedoLabel() : std::string();
}

// ---- layers -----------------------------------------------------------------

bool PhotoWorkspace::AddLayerUndoable(ph::LayerKind kind,
                                      const std::string& name,
                                      ph::LayerId* out_id) {
    if (doc_ == nullptr || hist_ == nullptr) return false;
    ph::LayerNode& made =
        doc_->AddLayer(kind, name, &doc_->Root());
    const ph::LayerId id = made.Id();
    auto node = doc_->DetachLayer(id);
    if (node == nullptr) return false;
    if (!hist_->Execute(std::make_unique<ph::AddLayerCommand>(
            doc_->Root().Id(), std::move(node)))) {
        return false;
    }
    if (out_id != nullptr) *out_id = id;
    return true;
}

void PhotoWorkspace::AddLayer(bool group) {
    EnsureDocument();
    if (doc_ == nullptr) return;
    int n = 1;
    std::function<void(const ph::LayerNode&)> count =
        [&](const ph::LayerNode& node) {
            for (const auto& c : node.Children()) {
                ++n;
                count(*c);
            }
        };
    count(doc_->Root());
    const std::string name =
        (group ? "Group " : "Layer ") + std::to_string(n);
    ph::LayerId id = ph::kInvalidLayerId;
    if (!AddLayerUndoable(
            group ? ph::LayerKind::kGroup : ph::LayerKind::kRaster,
            name, &id)) {
        SetStatus("Add layer failed", true);
        return;
    }
    active_layer_ = id;
    OnDocumentEdited();
    SetStatus("Added " + name);
}

void PhotoWorkspace::DeleteActiveLayer() {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || hist_ == nullptr || node == nullptr) {
        SetStatus("No layer selected");
        return;
    }
    const std::string name = node->Name();
    if (!hist_->Execute(
            std::make_unique<RemoveLayerCommand>(node->Id()))) {
        SetStatus("Delete failed", true);
        return;
    }
    active_layer_ = FallbackLayerId();
    OnDocumentEdited();
    SetStatus("Deleted " + name);
}

void PhotoWorkspace::DuplicateActiveLayer() {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || node == nullptr) {
        SetStatus("No layer selected");
        return;
    }
    const std::string name = node->Name();
    const ph::LayerId copy =
        ph::DuplicateLayer(*doc_, node->Id(), hist_.get());
    if (copy == ph::kInvalidLayerId) {
        SetStatus("Duplicate failed", true);
        return;
    }
    active_layer_ = copy;
    OnDocumentEdited();
    SetStatus("Duplicated " + name);
}

void PhotoWorkspace::MoveActiveLayer(int direction) {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || node == nullptr || node->Parent() == nullptr) {
        SetStatus("No layer selected");
        return;
    }
    ph::LayerNode* parent = node->Parent();
    const int index = doc_->ChildIndex(*parent, node->Id());
    const int count = static_cast<int>(parent->Children().size());
    const int next = std::clamp(index + direction, 0, count - 1);
    if (next == index) {
        SetStatus(direction > 0 ? "Already topmost" : "Already bottom");
        return;
    }
    if (!ph::MoveLayer(*doc_, node->Id(), parent->Id(),
                       static_cast<std::size_t>(next), hist_.get())) {
        SetStatus("Move failed", true);
        return;
    }
    OnDocumentEdited();
}

void PhotoWorkspace::MergeActiveLayer() {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || node == nullptr || node->Parent() == nullptr) {
        SetStatus("No layer selected");
        return;
    }
    ph::LayerNode* parent = node->Parent();
    const int index = doc_->ChildIndex(*parent, node->Id());
    ph::LayerId below = ph::kInvalidLayerId;
    if (index > 0) {
        below = parent->Children()[static_cast<std::size_t>(index - 1)]
                    ->Id();
    }
    if (!ph::MergeDown(*doc_, node->Id(), hist_.get())) {
        SetStatus("Merge failed — need a raster sibling below", true);
        return;
    }
    active_layer_ = below;
    OnDocumentEdited();
    SetStatus("Merged down");
}

void PhotoWorkspace::FlattenImage() {
    if (doc_ == nullptr) return;
    if (!ph::FlattenImage(*doc_, hist_.get())) {
        SetStatus("Flatten failed", true);
        return;
    }
    active_layer_ = FallbackLayerId();
    OnDocumentEdited();
    SetStatus("Flattened");
}

void PhotoWorkspace::ApplyLayerAttributes(
    ph::LayerId id,
    const ph::SetLayerAttributesCommand::Attributes& next,
    const std::string& label) {
    if (doc_ == nullptr || hist_ == nullptr) return;
    if (!hist_->Execute(
            std::make_unique<ph::SetLayerAttributesCommand>(id, next,
                                                            label))) {
        SetStatus(label + " failed", true);
        return;
    }
    OnDocumentEdited();
}

// ---- tools ------------------------------------------------------------------

void PhotoWorkspace::SetTool(Tool tool) {
    tool_ = tool;
    SyncToolButtons();
    SyncTopBar();
    for (const auto& info : kToolInfos) {
        if (info.tool == tool_) {
            SetStatus(info.tooltip);
            break;
        }
    }
    Invalidate();
}

void PhotoWorkspace::SetForegroundColor(ph::Pixel color) {
    brush_params_.color = color;
    Invalidate();
}

ph::BrushParams PhotoWorkspace::CurrentBrushParams() const {
    ph::BrushParams p = brush_params_;
    p.eraser = (tool_ == Tool::Eraser);
    return p;
}

// ---- stroke / fill / gradient -------------------------------------------------

void PhotoWorkspace::BeginStroke(const Layout::Point& doc) {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || node == nullptr) {
        SetStatus("No layer selected — add one in the Layers panel");
        return;
    }
    const bool paint_mask = mask_editing_ && node->Mask() != nullptr;
    if (!paint_mask && (node->Kind() != ph::LayerKind::kRaster ||
                        node->Device() == nullptr)) {
        SetStatus("Select a raster layer to paint", true);
        return;
    }
    if (node->Locked()) {
        SetStatus("Layer is locked", true);
        return;
    }
    scratch_ = std::make_unique<ph::TiledDevice>(
        doc_->CanvasWidth(), doc_->CanvasHeight());
    ph::BrushParams params = CurrentBrushParams();
    if (paint_mask) {
        params.color = ph::Pixel::RGBA(255, 255, 255, 255);
    }
    engine_ = std::make_unique<ph::BrushEngine>(params);
    engine_->BeginStroke(*scratch_);
    stroking_ = true;
    stroking_mask_ = paint_mask;
    engine_->LineTo({doc.x, doc.y, 1.0f});
    PushStrokePreview();
    Invalidate();
}

void PhotoWorkspace::ContinueStroke(const Layout::Point& doc) {
    if (!stroking_ || engine_ == nullptr) return;
    engine_->LineTo({doc.x, doc.y, 1.0f});
    PushStrokePreview();
}

void PhotoWorkspace::EndStroke() {
    if (!stroking_ || engine_ == nullptr || scratch_ == nullptr) {
        stroking_ = false;
        return;
    }
    stroking_ = false;
    const bool mask_stroke = stroking_mask_;
    stroking_mask_ = false;
    const ph::IntRect dirty = engine_->EndStroke();
    canvas_->SetStrokePreview({}, {}, 1.0f);
    bool ok = false;
    if (!dirty.IsEmpty() && doc_ != nullptr) {
        if (mask_stroke) {
            ph::LayerNode* node = ActiveLayer();
            if (node != nullptr && node->Mask() != nullptr) {
                const std::uint8_t value = MaskPaintValue();
                ok = engine_->CompositeStrokeOnMask(
                    *node->Mask(), *scratch_, dirty, value,
                    hist_.get(), ClipMask());
            }
        } else {
            ok = engine_->CompositeStroke(
                *doc_, active_layer_, *scratch_, dirty, hist_.get(),
                ClipMask());
        }
    }
    engine_.reset();
    scratch_.reset();
    if (!ok) {
        SetStatus("Stroke made no change");
        Invalidate();
        return;
    }
    OnDocumentEdited();
}

void PhotoWorkspace::PushStrokePreview() {
    if (scratch_ == nullptr) return;
    const ph::IntRect ext = scratch_->Extent();
    if (ext.IsEmpty()) {
        canvas_->SetStrokePreview({}, {}, 1.0f);
        return;
    }
    const auto u32 = scratch_->ReadRegionU32(ext);
    std::vector<std::uint8_t> bgra;
    U32ToBgra(u32, &bgra);
    canvas_->SetStrokePreview(
        std::move(bgra), ext,
        tool_ == Tool::Eraser ? 0.5f : 1.0f);
}

void PhotoWorkspace::ApplyFillAt(const Layout::Point&) {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || node == nullptr) {
        SetStatus("No layer selected — add one in the Layers panel");
        return;
    }
    if (node->Locked() || node->Device() == nullptr) {
        SetStatus("Select an unlocked raster layer to fill", true);
        return;
    }
    if (!ph::FillLayer(*doc_, node->Id(), brush_params_.color,
                       hist_.get(), ClipMask())) {
        SetStatus("Fill failed", true);
        return;
    }
    OnDocumentEdited();
    SetStatus("Filled " + node->Name());
}

void PhotoWorkspace::FinishGradient() {
    if (!gradient_drag_ || doc_ == nullptr) {
        gradient_drag_ = false;
        return;
    }
    gradient_drag_ = false;
    canvas_->SetGradientLine(false, 0, 0, 0, 0);
    ph::LayerNode* node = ActiveLayer();
    if (node == nullptr || node->Locked() || node->Device() == nullptr) {
        SetStatus("Select an unlocked raster layer", true);
        Invalidate();
        return;
    }
    if (std::abs(grad_ax_ - grad_bx_) + std::abs(grad_ay_ - grad_by_) <
        2.0f) {
        return;   // degenerate drag — treat as a click
    }
    const ph::Pixel c = brush_params_.color;
    const ph::Pixel transparent = ph::Pixel::RGBA(c.r, c.g, c.b, 0);
    const XER::Bridge::Skia::GradientStops stops = {
        {0.0f, c.PackedU32()},
        {1.0f, transparent.PackedU32()},
    };
    if (!ph::FillLinearGradient(*doc_, node->Id(), grad_ax_, grad_ay_,
                                grad_bx_, grad_by_, stops, hist_.get(),
                                ClipMask())) {
        SetStatus("Gradient failed", true);
        Invalidate();
        return;
    }
    OnDocumentEdited();
    SetStatus("Gradient applied to " + node->Name());
}

// ---- selection -----------------------------------------------------------------

void PhotoWorkspace::EnsureSelectionDevice() {
    if (doc_ == nullptr) {
        selection_.reset();
        return;
    }
    if (selection_ == nullptr ||
        selection_->Width() != doc_->CanvasWidth() ||
        selection_->Height() != doc_->CanvasHeight()) {
        selection_ = std::make_unique<ph::TiledDevice>(
            doc_->CanvasWidth(), doc_->CanvasHeight(),
            image::PixelFormat::Gray8, ph::Pixel{0, 0, 0, 0});
        sel_outline_.clear();
        selection_nonempty_ = false;
    }
}

const ph::TiledDevice* PhotoWorkspace::ClipMask() const {
    return selection_nonempty_ ? selection_.get() : nullptr;
}

void PhotoWorkspace::RefreshSelectionState() {
    selection_nonempty_ =
        selection_ != nullptr && !ph::SelectionIsEmpty(*selection_);
    sel_outline_ = selection_nonempty_
                       ? ph::SelectionOutline(*selection_)
                       : std::vector<ph::IntPoint>{};
    canvas_->SetSelectionOutline(sel_outline_);
}

void PhotoWorkspace::RunSelectionOp(
    const std::string& label,
    const std::function<void(ph::TiledDevice&)>& fn) {
    EnsureSelectionDevice();
    if (selection_ == nullptr || hist_ == nullptr) return;
    hist_->ExecuteDeviceOp(*selection_, label, fn);
    RefreshSelectionState();
    Invalidate();
}

void PhotoWorkspace::SelectAll() {
    EnsureDocument();
    RunSelectionOp("Select All",
                   [](ph::TiledDevice& s) { ph::SelectAll(s); });
}

void PhotoWorkspace::Deselect() {
    RunSelectionOp("Deselect",
                   [](ph::TiledDevice& s) { ph::SelectNone(s); });
}

void PhotoWorkspace::InvertSelection() {
    if (!HasSelection()) {
        SetStatus("Nothing selected", true);
        return;
    }
    RunSelectionOp("Invert Selection",
                   [](ph::TiledDevice& s) { ph::InvertSelection(s); });
}

void PhotoWorkspace::FeatherSelectionPx(float radius_px) {
    if (radius_px <= 0.0f) return;
    RunSelectionOp("Feather Selection",
                   [radius_px](ph::TiledDevice& s) {
                       ph::FeatherSelection(s, radius_px);
                   });
}

void PhotoWorkspace::ApplyRefineEdge(
    const ph::RefineEdgeParams& params) {
    if (!selection_nonempty_) {
        SetStatus("No active selection to refine", true);
        return;
    }
    RunSelectionOp("Refine Edge", [params](ph::TiledDevice& s) {
        ph::RefineSelectionEdge(s, params);
    });
    SetStatus("Refine Edge applied");
}

ph::SelectionCombine PhotoWorkspace::PendingCombine() const {
    return marq_combine_;
}

void PhotoWorkspace::CommitMarquee() {
    if (!marquee_drag_) return;
    marquee_drag_ = false;
    const ph::SelectionCombine mode = marq_combine_;
    const float feather = sel_feather_;
    if (tool_ == Tool::Lasso) {
        auto pts = lasso_pts_;
        lasso_pts_.clear();
        canvas_->SetLiveOutline({});
        RunSelectionOp("Lasso", [pts, mode, feather](
                                    ph::TiledDevice& s) {
            ph::SelectPolygon(s, pts, feather, mode);
        });
        return;
    }
    const ph::IntRect r =
        DocRectFromCorners(marq_ax_, marq_ay_, marq_bx_, marq_by_);
    canvas_->SetLiveOutline({});
    if (r.w < 1 || r.h < 1) return;
    if (tool_ == Tool::SelectEllipse) {
        RunSelectionOp(
            "Ellipse Select", [r, mode, feather](ph::TiledDevice& s) {
                ph::SelectEllipse(s, r, mode);
                if (feather > 0.0f) ph::FeatherSelection(s, feather);
            });
    } else {
        RunSelectionOp(
            "Rect Select", [r, mode, feather](ph::TiledDevice& s) {
                ph::SelectRect(s, r, mode);
                if (feather > 0.0f) ph::FeatherSelection(s, feather);
            });
    }
}

std::vector<ph::IntPoint> PhotoWorkspace::LiveMarqueePolygon() const {
    std::vector<ph::IntPoint> out;
    if (poly_active_ && poly_pts_.size() >= 2) return poly_pts_;
    if (!marquee_drag_) return out;
    if (tool_ == Tool::Lasso) return lasso_pts_;
    const ph::IntRect r =
        DocRectFromCorners(marq_ax_, marq_ay_, marq_bx_, marq_by_);
    if (r.w < 1 || r.h < 1) return out;
    if (tool_ == Tool::SelectEllipse) {
        const float cx = r.x + r.w * 0.5f;
        const float cy = r.y + r.h * 0.5f;
        for (int i = 0; i < 32; ++i) {
            const float t = 2.0f * 3.14159265f *
                            static_cast<float>(i) / 32.0f;
            out.push_back(
                {static_cast<int>(
                     std::lround(cx + std::cos(t) * r.w * 0.5f)),
                 static_cast<int>(std::lround(
                     cy + std::sin(t) * r.h * 0.5f))});
        }
    } else {
        out = {{r.x, r.y},
               {r.x + r.w, r.y},
               {r.x + r.w, r.y + r.h},
               {r.x, r.y + r.h}};
    }
    return out;
}

void PhotoWorkspace::CommitPolyLasso() {
    if (!poly_active_) return;
    poly_active_ = false;
    auto pts = poly_pts_;
    poly_pts_.clear();
    canvas_->SetLiveOutline({});
    const ph::SelectionCombine mode = marq_combine_;
    const float feather = sel_feather_;
    if (pts.size() < 3) return;
    RunSelectionOp("Polygon Select",
                   [pts, mode, feather](ph::TiledDevice& s) {
                       ph::SelectPolygon(s, pts, feather, mode);
                   });
}

void PhotoWorkspace::CancelSelectionGesture() {
    marquee_drag_ = false;
    poly_active_ = false;
    poly_pts_.clear();
    lasso_pts_.clear();
    canvas_->SetLiveOutline({});
}

// ---- masks -------------------------------------------------------------------

void PhotoWorkspace::ToggleActiveLayerMask() {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || node == nullptr) {
        SetStatus("No layer selected");
        return;
    }
    if (node->HasMask()) {
        if (ph::DetachLayerMask(*doc_, node->Id(), hist_.get())) {
            mask_editing_ = false;
            canvas_->SetMaskOverlay(nullptr, 0, 0);
            OnDocumentEdited();
            SetStatus("Removed mask from " + node->Name());
        } else {
            SetStatus("Could not remove mask", true);
        }
    } else {
        if (ph::AttachLayerMask(*doc_, node->Id(), hist_.get())) {
            OnDocumentEdited();
            SetStatus("Attached mask to " + node->Name() +
                      " — click its badge to paint it");
        } else {
            SetStatus("Could not attach mask", true);
        }
    }
}

void PhotoWorkspace::SetMaskEditing(bool on) {
    if (on) {
        ph::LayerNode* node = ActiveLayer();
        if (node == nullptr || node->Mask() == nullptr) {
            SetStatus("Active layer has no mask — attach one first",
                      true);
            return;
        }
        if (tool_ != Tool::Brush && tool_ != Tool::Eraser) {
            SetTool(Tool::Brush);
        }
    }
    if (mask_editing_ == on) return;
    mask_editing_ = on;
    if (!on) {
        canvas_->SetMaskOverlay(nullptr, 0, 0);
    } else {
        RebuildMaskOverlay();
    }
    if (layer_chrome_ != nullptr) layer_chrome_->Invalidate();
    Invalidate();
}

std::uint8_t PhotoWorkspace::MaskPaintValue() const {
    if (tool_ == Tool::Eraser) return 0;
    const ph::Pixel c = brush_params_.color;
    // Rec.601 luma — paints the fg swatch's gray into the mask.
    return static_cast<std::uint8_t>(
        std::clamp((77 * c.r + 150 * c.g + 29 * c.b) >> 8, 0, 255));
}

void PhotoWorkspace::RebuildMaskOverlay() {
    ph::LayerNode* node = ActiveLayer();
    if (!mask_editing_ || doc_ == nullptr || node == nullptr ||
        node->Mask() == nullptr) {
        canvas_->SetMaskOverlay(nullptr, 0, 0);
        return;
    }
    const int w = doc_->CanvasWidth();
    const int h = doc_->CanvasHeight();
    const std::vector<std::uint8_t> cov =
        node->Mask()->ReadRegionCoverage({0, 0, w, h});
    auto hidden = std::make_shared<std::vector<std::uint8_t>>(
        cov.size());
    for (std::size_t i = 0; i < cov.size(); ++i) {
        (*hidden)[i] = static_cast<std::uint8_t>(255 - cov[i]);
    }
    canvas_->SetMaskTint({1.0f, 0.18f, 0.15f, 0.42f});
    canvas_->SetMaskOverlay(std::move(hidden), w, h);
}

// ---- adjustment layers -----------------------------------------------------------

void PhotoWorkspace::AddAdjustmentLayer(const std::string& op) {
    EnsureDocument();
    if (doc_ == nullptr || hist_ == nullptr) return;
    const ph::AdjustmentOpInfo* info = ph::FindAdjustmentOp(op);
    if (info == nullptr) {
        SetStatus("Unknown adjustment op: " + op, true);
        return;
    }
    ph::LayerNode& made = doc_->AddLayer(
        ph::LayerKind::kAdjustment, info->label, &doc_->Root());
    made.SetAdjustment(ph::DefaultAdjustmentSpec(info->name));
    const ph::LayerId id = made.Id();
    auto owned = doc_->DetachLayer(id);
    if (owned == nullptr ||
        !hist_->Execute(std::make_unique<ph::AddLayerCommand>(
            doc_->Root().Id(), std::move(owned),
            "Add Adjustment Layer"))) {
        SetStatus("Add adjustment layer failed", true);
        return;
    }
    active_layer_ = id;
    OnDocumentEdited();
    SyncAdjustmentBinding();
    SetStatus("Added " + info->label);
}

bool PhotoWorkspace::SetActiveAdjustment(
    const ph::AdjustmentSpec& spec) {
    ph::LayerNode* n = ActiveLayer();
    if (n == nullptr || hist_ == nullptr) return false;
    ph::SetLayerAttributesCommand::Attributes a{
        n->Name(), n->Visible(), n->Locked(), n->Opacity(),
        n->BlendMode(), spec};
    ApplyLayerAttributes(n->Id(), a, "Adjustment Params");
    return true;
}

// Builds one collapsible Card per registry op; each card's content is
// a LinearBox of param rows bound to the active adjustment layer.
void PhotoWorkspace::RebuildAdjustmentCards() {
    adj_cards_.clear();
    adj_fields_.clear();
    adjust_panel_->Clear();
    const auto& ops = ph::AdjustmentOpRegistry();
    for (std::size_t oi = 0; oi < ops.size(); ++oi) {
        const auto& info = ops[oi];
        auto card = std::make_shared<W::Card>();
        card->SetTitle(info.label);
        card->SetCollapsible(true);
        card->SetCollapsed(true);
        card->SetDensity(W::Card::Density::Compact);

        auto col = LinearBox::Create(LinearBox::Direction::Vertical);
        col->SetSpacing(2.0f);
        std::vector<std::shared_ptr<W::ScrubNumberField>> fields;
        for (std::size_t pi = 0; pi < info.defaults.size(); ++pi) {
            const auto& def = info.defaults[pi];
            auto row =
                LinearBox::Create(LinearBox::Direction::Horizontal);
            row->SetSpacing(4.0f);
            auto label = std::make_shared<W::Label>(def.name, true);
            row->Add(label, {0.0f, 1.0f});
            auto field = W::ScrubNumberField::Create();
            const double lo = pi < info.ranges.size()
                                  ? info.ranges[pi].first
                                  : -1000.0;
            const double hi = pi < info.ranges.size()
                                  ? info.ranges[pi].second
                                  : 1000.0;
            field->SetRange(static_cast<float>(lo),
                            static_cast<float>(hi));
            field->SetStep(static_cast<float>(
                std::max(0.001, (hi - lo) / 200.0)));
            field->SetFormat("%.2f");
            field->SetResetValue(static_cast<float>(def.value));
            field->SetValue(static_cast<float>(def.value));
            const int opi = static_cast<int>(oi);
            const int ppi = static_cast<int>(pi);
            field->SetOnValueChange([this, opi, ppi](float v) {
                ApplyAdjustmentLive(opi, ppi, v);
            });
            field->SetOnValueCommitted(
                [this, opi, ppi](float /*v*/) {
                    CommitAdjustmentParam(opi, ppi);
                });
            row->Add(field, {110.0f, 0.0f});
            col->Add(row, {28.0f, 0.0f});
            fields.push_back(field);
        }
        // "Add layer" affordance when this op isn't the active one —
        // editing a field on a non-active card also adds the layer.
        auto add = std::make_shared<W::Button>();
        add->SetText("Add " + info.label);
        add->SetSize(W::ButtonSize::Small);
        add->SetStyle(W::ButtonStyle::Secondary);
        add->SetOnClick(
            [this, name = info.name] { AddAdjustmentLayer(name); });
        col->Add(add, {28.0f, 0.0f});
        card->SetContent(col);
        adj_cards_.push_back(card);
        adj_fields_.push_back(std::move(fields));
        adjust_panel_->Add(card, {0.0f, 0.0f});
    }
    SyncAdjustmentBinding();
}

void PhotoWorkspace::SyncAdjustmentBinding() {
    if (suppress_adj_sync_) return;
    adj_bound_op_ = -1;
    const ph::LayerNode* n = ActiveLayer();
    if (n != nullptr && n->Kind() == ph::LayerKind::kAdjustment) {
        const auto& ops = ph::AdjustmentOpRegistry();
        for (std::size_t i = 0; i < ops.size(); ++i) {
            if (ops[i].name == n->Adjustment().op) {
                adj_bound_op_ = static_cast<int>(i);
                break;
            }
        }
    }
    for (std::size_t i = 0; i < adj_cards_.size(); ++i) {
        const bool bound = static_cast<int>(i) == adj_bound_op_;
        adj_cards_[i]->SetCollapsed(!bound);
        adj_cards_[i]->SetSelected(bound);
        if (bound) {
            const auto& spec = n->Adjustment();
            const auto& info = ph::AdjustmentOpRegistry()[i];
            for (std::size_t pi = 0; pi < adj_fields_[i].size(); ++pi) {
                const std::string& pname = info.defaults[pi].name;
                double v = info.defaults[pi].value;
                for (const auto& p : spec.params) {
                    if (p.name == pname) {
                        v = p.value;
                        break;
                    }
                }
                adj_fields_[i][pi]->SetValue(
                    static_cast<float>(v));
            }
        }
    }
}

void PhotoWorkspace::ApplyAdjustmentLive(int op_index,
                                         int param_index,
                                         double value) {
    if (op_index != adj_bound_op_) return;
    ph::LayerNode* n = ActiveLayer();
    if (n == nullptr || n->Kind() != ph::LayerKind::kAdjustment) {
        return;   // card not bound — ignore live edits
    }
    if (pre_gesture_spec_ == nullptr) {
        pre_gesture_spec_ =
            std::make_unique<ph::AdjustmentSpec>(n->Adjustment());
    }
    const auto* info = ph::FindAdjustmentOp(n->Adjustment().op);
    if (info == nullptr ||
        param_index >= static_cast<int>(info->defaults.size())) {
        return;
    }
    ph::AdjustmentSpec spec = n->Adjustment();
    const std::string& pname = info->defaults[param_index].name;
    bool found = false;
    for (auto& p : spec.params) {
        if (p.name == pname) {
            p.value = value;
            found = true;
            break;
        }
    }
    if (!found) spec.params.push_back({pname, value});
    n->SetAdjustment(spec);   // live preview — commit restores
    Reflatten();
}

void PhotoWorkspace::CommitAdjustmentParam(int op_index,
                                           int /*param_index*/) {
    if (op_index != adj_bound_op_) return;
    ph::LayerNode* n = ActiveLayer();
    if (n == nullptr || n->Kind() != ph::LayerKind::kAdjustment) {
        return;
    }
    ph::AdjustmentSpec final_spec = n->Adjustment();
    if (pre_gesture_spec_ != nullptr) {
        // Restore the pre-gesture spec so the command's prev is the
        // value the user started scrubbing from.
        n->SetAdjustment(*pre_gesture_spec_);
        pre_gesture_spec_.reset();
    }
    ph::SetLayerAttributesCommand::Attributes a{
        n->Name(), n->Visible(), n->Locked(), n->Opacity(),
        n->BlendMode(), final_spec};
    ApplyLayerAttributes(n->Id(), a, "Adjustment Params");
    SyncAdjustmentBinding();
}

// ---- retouch ----------------------------------------------------------------

int PhotoWorkspace::RetouchRadius() const {
    return std::max(
        2, static_cast<int>(brush_params_.diameter_px * 0.5f));
}

void PhotoWorkspace::RetouchDab(const Layout::Point& doc_pt) {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || hist_ == nullptr || node == nullptr ||
        node->Kind() != ph::LayerKind::kRaster ||
        node->Device() == nullptr || node->Locked()) {
        return;
    }
    const ph::IntPoint dst{static_cast<int>(std::lround(doc_pt.x)),
                           static_cast<int>(std::lround(doc_pt.y))};
    const int r = RetouchRadius();
    const ph::TiledDevice* clip = ClipMask();
    bool ok = false;
    if (tool_ == Tool::CloneStamp || tool_ == Tool::Healing) {
        const ph::IntPoint src{
            clone_src_.x + (dst.x - retouch_origin_.x),
            clone_src_.y + (dst.y - retouch_origin_.y)};
        ok = tool_ == Tool::CloneStamp
                 ? ph::CloneStampAt(*doc_, node->Id(), src, dst, r,
                                    hist_.get(), clip)
                 : ph::HealingBrushAt(*doc_, node->Id(), src, dst, r,
                                      hist_.get(), clip);
    } else if (tool_ == Tool::Spot) {
        ok = ph::SpotRemoveAt(*doc_, node->Id(), dst, r, hist_.get(),
                              clip);
    }
    if (ok) OnDocumentEdited();
}

void PhotoWorkspace::ApplyInpaintOverSelection() {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || hist_ == nullptr || node == nullptr ||
        node->Kind() != ph::LayerKind::kRaster ||
        node->Device() == nullptr) {
        SetStatus("Select a raster layer to inpaint", true);
        return;
    }
    if (node->Locked()) {
        SetStatus("Layer is locked", true);
        return;
    }
    if (!HasSelection()) {
        SetStatus("Make a selection first, then Inpaint", true);
        return;
    }
    if (ph::InpaintMasked(*doc_, node->Id(), *selection_,
                          hist_.get())) {
        OnDocumentEdited();
        SetStatus("Inpainted selection on " + node->Name());
    } else {
        SetStatus("Inpaint made no change", true);
    }
}

// ---- layers panel sync -----------------------------------------------------------

void PhotoWorkspace::FlattenRows(
    std::vector<ph::LayerId>* out) const {
    if (out == nullptr || doc_ == nullptr) return;
    const std::set<std::string> expanded =
        layers_tree_ != nullptr ? layers_tree_->GetExpanded()
                                : std::set<std::string>{};
    std::function<void(const ph::LayerNode&)> walk =
        [&](const ph::LayerNode& parent) {
            // Display order = topmost first (children are stored
            // bottom-to-top).
            for (auto it = parent.Children().rbegin();
                 it != parent.Children().rend(); ++it) {
                const ph::LayerNode& node = **it;
                out->push_back(node.Id());
                if (node.Kind() == ph::LayerKind::kGroup &&
                    expanded.count(std::to_string(node.Id()))) {
                    walk(node);
                }
            }
        };
    walk(doc_->Root());
}

void PhotoWorkspace::RebuildLayerTree() {
    if (layers_tree_ == nullptr || doc_ == nullptr) return;
    const std::string sel = std::to_string(active_layer_);
    std::set<std::string> expanded = layers_tree_->GetExpanded();
    std::set<std::string> groups;
    // Local shim so the recursion can return TreeNode by value.
    struct BuildInto {
        static void Go(const ph::LayerNode& node,
                       std::set<std::string>* groups,
                       W::TreeNode* out) {
            out->id = std::to_string(node.Id());
            out->label = node.Name();
            // A branch with zero children is LAZY in TreeView terms
            // (expanding shows a Loading row forever) — only mark
            // groups that actually have children; empty groups show
            // no chevron.
            out->is_branch =
                node.Kind() == ph::LayerKind::kGroup &&
                !node.Children().empty();
            if (out->is_branch) {
                groups->insert(out->id);
                for (auto it = node.Children().rbegin();
                     it != node.Children().rend(); ++it) {
                    W::TreeNode child;
                    Go(**it, groups, &child);
                    out->children.push_back(std::move(child));
                }
            }
        }
    };
    std::vector<W::TreeNode> roots;
    for (auto it = doc_->Root().Children().rbegin();
         it != doc_->Root().Children().rend(); ++it) {
        W::TreeNode n;
        BuildInto::Go(**it, &groups, &n);
        roots.push_back(std::move(n));
    }
    // New groups start expanded; existing expansion state wins.
    expanded.insert(groups.begin(), groups.end());
    layers_tree_->SetItems(std::move(roots));
    layers_tree_->SetExpanded(std::move(expanded));
    layers_tree_->SetSelectedId(sel);
    if (layer_chrome_ != nullptr) layer_chrome_->Invalidate();
}

void PhotoWorkspace::SyncLayerControls() {
    suppress_adj_sync_ = true;
    ph::LayerNode* n = ActiveLayer();
    const bool have = n != nullptr;
    if (name_input_ != nullptr) {
        name_input_->SetValue(have ? n->Name() : "");
        name_input_->SetEnabled(have);
    }
    if (eye_btn_ != nullptr) {
        eye_btn_->SetChecked(have && n->Visible());
        eye_btn_->SetEnabled(have);
    }
    if (lock_btn_ != nullptr) {
        lock_btn_->SetChecked(have && n->Locked());
        lock_btn_->SetEnabled(have);
        lock_btn_->SetIcon(have && n->Locked() ? "lock" : "lock_open");
    }
    if (layer_opacity_field_ != nullptr) {
        layer_opacity_field_->SetValue(have ? n->Opacity() : 1.0f);
        layer_opacity_field_->SetEnabled(have);
    }
    if (blend_select_ != nullptr) {
        const std::string mode = have ? n->BlendMode() : "srcOver";
        // The Select's option list is a fixed curated set — unknown
        // modes leave the index at srcOver.
        static const char* const kModes[] = {
            "srcOver",    "multiply",  "screen",   "overlay",
            "darken",     "lighten",   "softLight", "hardLight",
            "difference", "exclusion", "hue",      "saturation",
            "color",      "luminosity", "plus",    "srcIn",
            "dstOut",     "clear"};
        int idx = 0;
        for (int i = 0;
             i < static_cast<int>(sizeof(kModes) / sizeof(kModes[0]));
             ++i) {
            if (mode == kModes[i]) {
                idx = i;
                break;
            }
        }
        blend_select_->SetSelectedIndex(idx);
        blend_select_->SetEnabled(have);
    }
    suppress_adj_sync_ = false;
}

// ---- crop -------------------------------------------------------------------

void PhotoWorkspace::CommitCrop() {
    if (doc_ == nullptr) return;
    ph::IntRect r = crop_rect_;
    crop_drag_ = false;
    canvas_->SetCropPreview(false, {});
    // Clamp to canvas; empty = cancel.
    r = ph::IntRect::Intersect(
        r, {0, 0, doc_->CanvasWidth(), doc_->CanvasHeight()});
    if (r.IsEmpty()) {
        SetStatus("Crop canceled — empty rect");
        return;
    }
    // The photo model has no canvas-resize op, so the crop rebuilds
    // the document: every layer's pixels are read inside the crop
    // rect and written at the new origin; structure, attributes,
    // adjustment specs and masks ride along. Fresh history — the old
    // undo stack can't follow a document swap.
    auto next = std::make_unique<ph::Document>(
        r.w, r.h, doc_->WorkingSpace());
    const ph::IntRect src{r.x, r.y, r.w, r.h};
    std::function<void(ph::LayerNode&, const ph::LayerNode&)> copy =
        [&](ph::LayerNode& dst_parent, const ph::LayerNode& src_node) {
            ph::LayerNode& dn =
                &dst_parent == &next->Root()
                    ? next->AddLayer(src_node.Kind(), src_node.Name())
                    : next->AddLayer(src_node.Kind(), src_node.Name(),
                                     &dst_parent);
            dn.SetVisible(src_node.Visible());
            dn.SetLocked(src_node.Locked());
            dn.SetOpacity(src_node.Opacity());
            dn.SetBlendMode(src_node.BlendMode());
            if (src_node.Kind() == ph::LayerKind::kAdjustment) {
                dn.SetAdjustment(src_node.Adjustment());
            }
            if (src_node.Device() != nullptr) {
                const auto px = src_node.Device()->ReadRegion(src);
                dn.Device()->WriteRegion({0, 0, r.w, r.h}, px.data());
            }
            if (src_node.Mask() != nullptr) {
                auto mask = std::make_unique<ph::TiledDevice>(
                    r.w, r.h, image::PixelFormat::Gray8,
                    ph::Pixel{0, 0, 0, 0});
                const auto cov =
                    src_node.Mask()->ReadRegionCoverage(src);
                mask->WriteRegionCoverage({0, 0, r.w, r.h},
                                          cov.data());
                dn.SetMask(std::move(mask));
            }
            for (const auto& child : src_node.Children()) {
                copy(dn, *child);
            }
        };
    for (const auto& child : doc_->Root().Children()) {
        copy(next->Root(), *child);
    }
    // Selection clips to the new bounds.
    selection_.reset();
    SetDocument(std::move(next), doc_name_);
    doc_dirty_ = true;
    SetStatus("Cropped to " + std::to_string(r.w) + "x" +
              std::to_string(r.h) +
              " (new document — undo history reset)");
}

// ---- canvas gesture handlers --------------------------------------------------

void PhotoWorkspace::OnCanvasDown(float dx, float dy, bool shift,
                                  bool alt) {
    EnsureDocument();
    const ph::SelectionCombine combine =
        shift ? ph::SelectionCombine::kAdd
              : alt ? ph::SelectionCombine::kSubtract
                    : ph::SelectionCombine::kReplace;
    switch (tool_) {
        case Tool::Brush:
        case Tool::Eraser:
            BeginStroke({dx, dy});
            break;
        case Tool::Fill:
            ApplyFillAt({});
            break;
        case Tool::Gradient:
            gradient_drag_ = true;
            grad_ax_ = grad_bx_ = dx;
            grad_ay_ = grad_by_ = dy;
            canvas_->SetGradientLine(true, dx, dy, dx, dy);
            break;
        case Tool::SelectRect:
        case Tool::SelectEllipse:
        case Tool::Lasso:
            marq_combine_ = combine;
            marquee_drag_ = true;
            marq_ax_ = marq_bx_ = dx;
            marq_ay_ = marq_by_ = dy;
            lasso_pts_.clear();
            if (tool_ == Tool::Lasso) {
                lasso_pts_.push_back(
                    {static_cast<int>(std::lround(dx)),
                     static_cast<int>(std::lround(dy))});
                canvas_->SetLiveOutline(lasso_pts_);
            }
            break;
        case Tool::PolyLasso: {
            marq_combine_ = combine;
            const ph::IntPoint pt{
                static_cast<int>(std::lround(dx)),
                static_cast<int>(std::lround(dy))};
            if (!poly_active_) {
                poly_active_ = true;
                poly_pts_.clear();
            } else if (poly_pts_.size() >= 3) {
                // Click near the start vertex closes the loop —
                // measured in client px (10 DIP at current zoom).
                const float s = canvas_->Scale();
                const Layout::Point c0 =
                    canvas_->DocToClient(
                        static_cast<float>(poly_pts_.front().x),
                        static_cast<float>(poly_pts_.front().y));
                const Layout::Point c =
                    canvas_->DocToClient(dx, dy);
                if (s > 0.0f &&
                    std::hypot(c.x - c0.x, c.y - c0.y) <= 10.0f) {
                    CommitPolyLasso();
                    break;
                }
            }
            poly_pts_.push_back(pt);
            canvas_->SetLiveOutline(poly_pts_);
            break;
        }
        case Tool::MagicWand: {
            if (doc_ == nullptr) break;
            marq_combine_ = combine;
            const int sx = static_cast<int>(std::floor(dx));
            const int sy = static_cast<int>(std::floor(dy));
            if (sx < 0 || sy < 0 || sx >= doc_->CanvasWidth() ||
                sy >= doc_->CanvasHeight()) {
                break;
            }
            const auto flat = doc_->FlattenU32();
            const ph::SelectionCombine mode = marq_combine_;
            const int tol = static_cast<int>(std::lround(wand_tol_));
            const int w = doc_->CanvasWidth();
            const int h = doc_->CanvasHeight();
            const float feather = sel_feather_;
            RunSelectionOp(
                "Magic Wand",
                [flat, w, h, sx, sy, tol, mode,
                 feather](ph::TiledDevice& s) {
                    ph::SelectColorRange(s, flat, w, h, sx, sy, tol,
                                         mode);
                    if (feather > 0.0f) {
                        ph::FeatherSelection(s, feather);
                    }
                });
            break;
        }
        case Tool::CloneStamp:
        case Tool::Healing: {
            const ph::IntPoint pt{
                static_cast<int>(std::lround(dx)),
                static_cast<int>(std::lround(dy))};
            if (alt) {
                clone_src_ = pt;
                clone_src_set_ = true;
                canvas_->SetSourceMarker(true, pt);
                SetStatus("Source picked — drag to paint");
                break;
            }
            if (!clone_src_set_) {
                SetStatus("Alt+click to pick a source first", true);
                break;
            }
            retouch_drag_ = true;
            retouch_origin_ = pt;
            if (hist_ != nullptr) {
                hist_->BeginMacro(tool_ == Tool::CloneStamp
                                      ? "Clone Stamp"
                                      : "Healing Brush");
            }
            RetouchDab({dx, dy});
            break;
        }
        case Tool::Spot: {
            retouch_drag_ = true;
            retouch_origin_ = {static_cast<int>(std::lround(dx)),
                               static_cast<int>(std::lround(dy))};
            if (hist_ != nullptr) hist_->BeginMacro("Spot Remove");
            RetouchDab({dx, dy});
            break;
        }
        case Tool::Inpaint:
            ApplyInpaintOverSelection();
            break;
        case Tool::Move: {
            ph::LayerNode* node = ActiveLayer();
            if (node == nullptr || node->Device() == nullptr) {
                SetStatus("Select a raster layer to move");
                break;
            }
            if (node->Locked()) {
                SetStatus("Layer is locked", true);
                break;
            }
            move_drag_ = true;
            move_ax_ = dx;
            move_ay_ = dy;
            move_dx_ = move_dy_ = 0.0f;
            move_extent_ = node->Device()->Extent();
            canvas_->SetMovePreview(true, move_extent_, 0.0f, 0.0f);
            break;
        }
        case Tool::Crop:
            crop_drag_ = true;
            crop_rect_ = {static_cast<int>(std::floor(dx)),
                          static_cast<int>(std::floor(dy)), 0, 0};
            canvas_->SetCropPreview(true, crop_rect_);
            break;
        default:
            break;
    }
}

void PhotoWorkspace::OnCanvasDrag(float dx, float dy) {
    if (stroking_) {
        ContinueStroke({dx, dy});
        return;
    }
    if (gradient_drag_) {
        grad_bx_ = dx;
        grad_by_ = dy;
        canvas_->SetGradientLine(true, grad_ax_, grad_ay_, dx, dy);
        return;
    }
    if (marquee_drag_) {
        marq_bx_ = dx;
        marq_by_ = dy;
        if (tool_ == Tool::Lasso) {
            lasso_pts_.push_back(
                {static_cast<int>(std::lround(dx)),
                 static_cast<int>(std::lround(dy))});
        }
        canvas_->SetLiveOutline(LiveMarqueePolygon());
        return;
    }
    if (retouch_drag_) {
        RetouchDab({dx, dy});
        return;
    }
    if (move_drag_) {
        move_dx_ = dx - move_ax_;
        move_dy_ = dy - move_ay_;
        canvas_->SetMovePreview(true, move_extent_, move_dx_,
                                move_dy_);
        return;
    }
    if (crop_drag_) {
        crop_rect_ = DocRectFromCorners(
            static_cast<float>(crop_rect_.x),
            static_cast<float>(crop_rect_.y), dx, dy);
        canvas_->SetCropPreview(true, crop_rect_);
        return;
    }
}

void PhotoWorkspace::OnCanvasUp(float dx, float dy) {
    if (stroking_) {
        EndStroke();
        return;
    }
    if (gradient_drag_) {
        grad_bx_ = dx;
        grad_by_ = dy;
        FinishGradient();
        return;
    }
    if (marquee_drag_) {
        marq_bx_ = dx;
        marq_by_ = dy;
        if (tool_ == Tool::Lasso && lasso_pts_.size() >= 2) {
            lasso_pts_.push_back(
                {static_cast<int>(std::lround(dx)),
                 static_cast<int>(std::lround(dy))});
        }
        CommitMarquee();
        return;
    }
    if (retouch_drag_) {
        retouch_drag_ = false;
        if (hist_ != nullptr) hist_->EndMacro();
        return;
    }
    if (move_drag_) {
        move_drag_ = false;
        canvas_->SetMovePreview(false, {}, 0.0f, 0.0f);
        const int mdx = static_cast<int>(std::lround(move_dx_));
        const int mdy = static_cast<int>(std::lround(move_dy_));
        if (mdx != 0 || mdy != 0) {
            if (ph::MoveLayerPixels(*doc_, active_layer_, mdx, mdy,
                                    hist_.get())) {
                OnDocumentEdited();
                SetStatus("Moved layer " +
                          std::to_string(mdx) + "," +
                          std::to_string(mdy));
            } else {
                SetStatus("Move failed", true);
            }
        }
        return;
    }
    if (crop_drag_) {
        crop_drag_ = false;
        // Keep the crop preview up — Enter commits, Escape cancels.
        SetStatus("Crop rect set — Enter commits, Escape cancels");
        return;
    }
}

void PhotoWorkspace::OnCanvasRightDown(Layout::Point client) {
    // Marking-menu tool switching: radial PieMenu centered on the
    // press point.
    std::vector<W::PieMenu::Item> items;
    items.reserve(kToolCount);
    for (const auto& info : kToolInfos) {
        W::PieMenu::Item item;
        item.label = info.label;
        if (info.icon != nullptr) item.icon = info.icon;
        item.action = [this, t = info.tool] { SetTool(t); };
        items.push_back(std::move(item));
    }
    pie_menu_ = W::PieMenu::Create(std::move(items));
    pie_menu_->SetCenter(client);
    pie_menu_->SetRadius(96.0f);
    pie_menu_->SetOnSelect([this](int index) {
        if (index >= 0 && index < kToolCount) {
            SetTool(kToolInfos[index].tool);
        }
    });
    pie_menu_->SetOnCancel([this] { pie_menu_.reset(); });
    ShowOverlay(pie_menu_);
}

// ---- layout / render / events ---------------------------------------------------

void PhotoWorkspace::Render(Rendering::Renderer& renderer,
                            const XER::UI::Theme::Tokens& tokens) {
    const auto b = GetBounds();
    renderer.FillRect(b, tokens.background, 0.0f);
    // Top-bar surface + separators (children paint their own fills;
    // this just provides the chrome bands).
    renderer.FillRect({b.x, b.y, b.width, kTopBarH}, tokens.panel,
                      0.0f);
    renderer.DrawLine({b.x, b.y + kTopBarH},
                      {b.x + b.width, b.y + kTopBarH}, tokens.border,
                      1.0f);
    renderer.FillRect({b.x, b.y + kTopBarH, kToolColW,
                       b.height - kTopBarH},
                      tokens.panel, 0.0f);
    renderer.DrawLine({b.x + kToolColW, b.y + kTopBarH},
                      {b.x + kToolColW, b.y + b.height}, tokens.border,
                      1.0f);
    const float dock_x = b.x + b.width - kRightDockW;
    renderer.FillRect({dock_x, b.y + kTopBarH, kRightDockW,
                       b.height - kTopBarH},
                      tokens.panel, 0.0f);
    renderer.DrawLine({dock_x, b.y + kTopBarH},
                      {dock_x, b.y + b.height}, tokens.border, 1.0f);
}

void PhotoWorkspace::Layout(const Layout::Rect& bounds) {
    Layout(bounds, CachedLayoutTokens());
}

void PhotoWorkspace::Layout(const Layout::Rect& bounds,
                            const XER::UI::Theme::Tokens& tokens) {
    SetBounds(bounds);
    top_bar_->Layout({bounds.x, bounds.y, bounds.width, kTopBarH},
                     tokens);
    const float body_y = bounds.y + kTopBarH;
    const float body_h = std::max(0.0f, bounds.height - kTopBarH);
    tools_scroll_->Layout({bounds.x, body_y, kToolColW, body_h},
                          tokens);
    const float dock_x = bounds.x + bounds.width - kRightDockW;
    right_dock_->Layout({dock_x, body_y, kRightDockW, body_h}, tokens);
    const Layout::Rect canvas_r{
        bounds.x + kToolColW, body_y,
        std::max(0.0f, dock_x - bounds.x - kToolColW), body_h};
    canvas_->Layout(canvas_r, tokens);
    if (eyedropper_ != nullptr) {
        eyedropper_->Layout(canvas_r, tokens);
    }
}

bool PhotoWorkspace::HandleEvent(const W::Event& event) {
    using W::Event;
    using W::KeyCode;

    switch (event.type) {
        case Event::Type::KeyDown: {
            if (event.key == KeyCode::Delete && !event.is_ctrl &&
                !event.is_alt) {
                DeleteActiveLayer();
                return true;
            }
            if (event.key == KeyCode::Escape) {
                if (stroking_ || gradient_drag_ || marquee_drag_ ||
                    poly_active_ || retouch_drag_ || move_drag_ ||
                    crop_drag_ || !crop_rect_.IsEmpty()) {
                    stroking_ = false;
                    stroking_mask_ = false;
                    gradient_drag_ = false;
                    retouch_drag_ = false;
                    move_drag_ = false;
                    crop_drag_ = false;
                    if (hist_ != nullptr) hist_->EndMacro();
                    CancelSelectionGesture();
                    engine_.reset();
                    scratch_.reset();
                    crop_rect_ = {};
                    canvas_->SetStrokePreview({}, {}, 1.0f);
                    canvas_->SetGradientLine(false, 0, 0, 0, 0);
                    canvas_->SetMovePreview(false, {}, 0.0f, 0.0f);
                    canvas_->SetCropPreview(false, {});
                    Invalidate();
                }
                return true;
            }
            if (event.key == KeyCode::Enter) {
                if (poly_active_) {
                    CommitPolyLasso();
                    return true;
                }
                if (!crop_rect_.IsEmpty() && tool_ == Tool::Crop) {
                    CommitCrop();
                    return true;
                }
                return false;
            }
            if (event.is_ctrl) {
                switch (event.key) {
                    case KeyCode::Z:
                        if (event.is_shift) Redo(); else Undo();
                        return true;
                    case KeyCode::Y:
                        Redo();
                        return true;
                    case KeyCode::S:
                        Save(event.is_shift);
                        return true;
                    case KeyCode::O:
                        OpenDialog();
                        return true;
                    case KeyCode::N:
                        NewDocumentDialog();
                        return true;
                    case KeyCode::A:
                        SelectAll();
                        return true;
                    case KeyCode::D:
                        Deselect();
                        return true;
                    default:
                        return false;
                }
            }
            return false;
        }

        case Event::Type::TextInput: {
            if (event.is_ctrl || event.is_alt ||
                event.text.size() != 1) {
                return false;
            }
            const char c =
                static_cast<char>(std::tolower(
                    static_cast<unsigned char>(event.text.front())));
            for (const auto& info : kToolInfos) {
                if (info.key != 0 && info.key == c) {
                    SetTool(info.tool);
                    return true;
                }
            }
            if (c == '[') {
                brush_params_.diameter_px = std::max(
                    1.0f, brush_params_.diameter_px / 1.2f);
                if (size_field_ != nullptr) {
                    size_field_->SetValue(
                        brush_params_.diameter_px);
                }
                SyncTopBar();
                return true;
            }
            if (c == ']') {
                brush_params_.diameter_px = std::min(
                    400.0f, brush_params_.diameter_px * 1.2f);
                if (size_field_ != nullptr) {
                    size_field_->SetValue(
                        brush_params_.diameter_px);
                }
                SyncTopBar();
                return true;
            }
            return false;
        }

        default:
            return false;
    }
}

void PhotoWorkspace::Tick(double dt_seconds) {
    canvas_->Tick(dt_seconds);
}

// ===========================================================================
// Headless smoke — registered from main.cc as "photo_workspace".
// ===========================================================================

namespace {

// Null renderer that counts DrawFrameBgra calls — enough to assert
// "the composite painted" without pixel comparison.
class SmokeRenderer final : public Rendering::Renderer {
public:
    int frame_calls = 0;

    void BeginFrame(Layout::Rect) override {}
    void EndFrame() override {}
    void FillRect(Layout::Rect, Color, float) override {}
    void StrokeRect(Layout::Rect, Color, float, float) override {}
    void DrawLine(Layout::Point, Layout::Point, Color,
                  float) override {}
    void DrawText(std::string_view, Layout::Rect,
                  Rendering::TextStyle) override {}
    Layout::Size MeasureText(std::string_view text,
                             Rendering::TextStyle) const override {
        return {static_cast<float>(text.size()) * 7.0f, 14.0f};
    }
    bool DrawFrameBgra(const void*, int, int, Layout::Rect,
                       float) override {
        ++frame_calls;
        return true;
    }
};

W::Event SmokeMouse(W::Event::Type type, float x, float y) {
    W::Event ev{};
    ev.type = type;
    ev.position = {x, y};
    if (type != W::Event::Type::MouseMove) {
        ev.mouse_button = W::MouseButton::Left;
    }
    return ev;
}

[[maybe_unused]] W::Event SmokeKey(W::KeyCode key, bool ctrl = false,
                                   bool shift = false) {
    W::Event ev{};
    ev.type = W::Event::Type::KeyDown;
    ev.key = key;
    ev.is_ctrl = ctrl;
    ev.is_shift = shift;
    return ev;
}

}  // namespace

std::string Ui2PhotoWorkspaceSmoke() {
    auto ws = PhotoWorkspace::Create({});
    W::WidgetTree tree;
    tree.SetRoot(ws);
    const auto tokens = XER::VideoEditor::Ui2::Theme::DarkTokens();
    const Layout::Rect bounds{0.0f, 0.0f, 1200.0f, 800.0f};
    tree.Layout(bounds, tokens);

    ws->EnsureDocument();
    tree.Layout(bounds, tokens);   // settle after the doc push
    if (!ws->HasDocument() || ws->Doc() == nullptr) {
        return "EnsureDocument made no document";
    }

    // Layout contract: the workspace min covers the icon tool strip,
    // a usable canvas, and the right dock — the shell's split floor
    // leans on this.
    if (ws->GetMinSize().width <
        kToolColW + kRightDockW + 160.0f) {
        return "workspace min width below strip+dock+canvas";
    }
    // Tool-strip contract: every strip button is icon-only — a
    // regression that re-arms text labels is the "Mo ve" wrap bug.
    if (ws->IconOnlyToolButtonCount() != kToolCount) {
        return "tool strip is not fully icon-only";
    }
    if (ws->Doc()->Root().Children().size() != 1) {
        return "fresh document should have one starter layer";
    }

    // Canvas paint: the composite must reach DrawFrameBgra.
    {
        SmokeRenderer r;
        tree.Render(r, tokens);
        if (r.frame_calls == 0) {
            return "canvas composite did not paint";
        }
    }

    const auto down = [](Layout::Point p) {
        return SmokeMouse(W::Event::Type::MouseDown, p.x, p.y);
    };
    const auto move = [](Layout::Point p) {
        return SmokeMouse(W::Event::Type::MouseMove, p.x, p.y);
    };
    const auto up = [](Layout::Point p) {
        return SmokeMouse(W::Event::Type::MouseUp, p.x, p.y);
    };

    // ---- rect marquee -> A8 selection ---------------------------------
    ws->SetTool(PhotoWorkspace::Tool::SelectRect);
    const Layout::Point a = ws->Canvas()->DocToClient(60.0f, 60.0f);
    const Layout::Point bpt =
        ws->Canvas()->DocToClient(260.0f, 220.0f);
    tree.HandleEvent(down(a));
    tree.HandleEvent(move(bpt));
    tree.HandleEvent(up(bpt));
    if (!ws->HasSelection()) {
        return "rect marquee produced no selection";
    }

    // ---- layer mask: attach -> mask-editing eraser stroke -------------
    ws->ToggleActiveLayerMask();
    ph::LayerNode* node =
        ws->Doc()->FindLayer(ws->ActiveLayerId());
    if (node == nullptr || node->Mask() == nullptr) {
        return "mask attach failed";
    }
    ws->SetMaskEditing(true);
    if (!ws->MaskEditing()) return "mask editing did not engage";
    ws->SetTool(PhotoWorkspace::Tool::Eraser);
    const Layout::Point m0 =
        ws->Canvas()->DocToClient(80.0f, 80.0f);
    const Layout::Point m1 =
        ws->Canvas()->DocToClient(220.0f, 200.0f);
    tree.HandleEvent(down(m0));
    tree.HandleEvent(move(m1));
    tree.HandleEvent(up(m1));
    if (node->Mask()->ReadCoverage(140, 140) >= 255) {
        return "mask paint stroke did not cut coverage";
    }
    ws->SetMaskEditing(false);
    ws->SetTool(PhotoWorkspace::Tool::Brush);
    ws->Deselect();

    // ---- brush stroke -> composite changes, undo restores -------------
    const auto flat0 = ws->Doc()->FlattenU32();
    const Layout::Point c0 =
        ws->Canvas()->DocToClient(500.0f, 400.0f);
    const Layout::Point c1 =
        ws->Canvas()->DocToClient(600.0f, 480.0f);
    tree.HandleEvent(down(c0));
    tree.HandleEvent(move(c1));
    tree.HandleEvent(up(c1));
    if (ws->Doc()->FlattenU32() == flat0) {
        return "brush stroke did not paint";
    }
    ws->Undo();
    if (ws->Doc()->FlattenU32() != flat0) {
        return "brush stroke undo did not restore";
    }
    ws->Redo();
    if (ws->Doc()->FlattenU32() == flat0) {
        return "brush stroke redo did not repaint";
    }
    ws->Undo();

    // ---- layer add/remove + undo ---------------------------------------
    const std::size_t n0 = ws->Doc()->Root().Children().size();
    ws->AddLayer(false);
    if (ws->Doc()->Root().Children().size() != n0 + 1) {
        return "add layer failed";
    }
    ws->DeleteActiveLayer();
    if (ws->Doc()->Root().Children().size() != n0) {
        return "delete layer failed";
    }
    ws->Undo();   // restores the deleted layer
    if (ws->Doc()->Root().Children().size() != n0 + 1) {
        return "delete undo failed";
    }
    ws->Undo();   // removes the added layer
    if (ws->Doc()->Root().Children().size() != n0) {
        return "add undo failed";
    }

    // ---- adjustment param commit -> undo -------------------------------
    // The composite is transparent again after the stroke undo — an
    // adjustment over fully-transparent pixels is a no-op, so paint
    // visible content first (kept; the undo below must restore it).
    ws->SetTool(PhotoWorkspace::Tool::Brush);
    {
        const Layout::Point p0 =
            ws->Canvas()->DocToClient(700.0f, 500.0f);
        const Layout::Point p1 =
            ws->Canvas()->DocToClient(800.0f, 560.0f);
        tree.HandleEvent(down(p0));
        tree.HandleEvent(move(p1));
        tree.HandleEvent(up(p1));
    }
    ws->AddAdjustmentLayer("brightness_contrast");
    ph::LayerNode* adj =
        ws->Doc()->FindLayer(ws->ActiveLayerId());
    if (adj == nullptr ||
        adj->Kind() != ph::LayerKind::kAdjustment) {
        return "adjustment layer missing";
    }
    {
        const auto before = ws->Doc()->FlattenU32();
        ph::AdjustmentSpec spec = adj->Adjustment();
        for (auto& p : spec.params) {
            if (p.name == "brightness") p.value = 80.0;
        }
        if (!ws->SetActiveAdjustment(spec)) {
            return "adjustment commit failed";
        }
        if (ws->Doc()->FlattenU32() == before) {
            return "adjustment did not change the composite";
        }
        ws->Undo();
        if (ws->Doc()->FlattenU32() != before) {
            return "adjustment undo did not restore";
        }
        ws->Undo();   // undo the adjustment-layer add
    }

    // ---- .xerphoto v2 round trip ---------------------------------------
    {
        const auto path =
            (std::filesystem::temp_directory_path() /
             "xer2_photo_workspace_smoke.xerphoto")
                .generic_string();
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::string err;
        if (!ph::SaveDocument(*ws->Doc(), path, &err)) {
            return "xerphoto save failed: " + err;
        }
        const std::size_t layers =
            ws->Doc()->Root().Children().size();
        if (!ws->OpenPath(path)) {
            return "xerphoto load failed";
        }
        if (ws->Doc() == nullptr ||
            ws->Doc()->Root().Children().size() != layers) {
            return "xerphoto round trip lost layers";
        }
        std::filesystem::remove(path, ec);
    }

    // ---- canvas zoom/pan view state --------------------------------------
    const float s0 = ws->Canvas()->Scale();
    ws->Canvas()->SetZoom(2.0f);
    if (ws->Canvas()->Scale() <= s0) {
        return "zoom did not change scale";
    }
    ws->Canvas()->SetZoom(0.0f);   // reset to fit

    tree.SetRoot(nullptr);
    return {};
}

}  // namespace XER::VideoEditor::Ui2
