// photo_workspace.cc — see photo_workspace.h.
#include "photo_workspace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
#include "photo/adjust_ops.h"
#include "photo/layer_ops.h"
#include "photo/mask_ops.h"
#include "photo/retouch.h"
#include "photo/xerphoto.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/dialog.h"
#include "widgets/label.h"
#include "widgets/select.h"
#include "widgets/slider.h"
#include "widgets/text_input.h"
#include "wrapper/skia/skia_engine_bridge.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
namespace Widgets = XER::UI::Widgets;
namespace ph = XER::VideoEditor::Photo;
using XER::UI::Theme::Color;

constexpr float kTopBarH = 40.0f;
constexpr float kToolStripW = 40.0f;
constexpr float kPad = 10.0f;
constexpr float kBtnH = 28.0f;

std::string Basename(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

Color ToColor(ph::Pixel p) {
    return {p.r / 255.0f, p.g / 255.0f, p.b / 255.0f, p.a / 255.0f};
}

// Bridge-packed u32 (R<<24|G<<16|B<<8|A) -> BGRA byte quad.
void U32ToBgra(const std::vector<std::uint32_t>& src,
               std::vector<std::uint8_t>* out) {
    out->resize(src.size() * 4);
    for (std::size_t i = 0; i < src.size(); ++i) {
        const std::uint32_t v = src[i];
        (*out)[i * 4 + 0] = static_cast<std::uint8_t>((v >> 8) & 0xFF);   // B
        (*out)[i * 4 + 1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);  // G
        (*out)[i * 4 + 2] = static_cast<std::uint8_t>((v >> 24) & 0xFF);  // R
        (*out)[i * 4 + 3] = static_cast<std::uint8_t>(v & 0xFF);          // A
    }
}

// Preset swatch palette (fg color targets). Kept deliberately small —
// the hex input covers arbitrary colors.
const ph::Pixel kSwatches[] = {
    ph::Pixel::RGBA(0, 0, 0),       ph::Pixel::RGBA(255, 255, 255),
    ph::Pixel::RGBA(120, 120, 120), ph::Pixel::RGBA(230, 60, 60),
    ph::Pixel::RGBA(240, 150, 40),  ph::Pixel::RGBA(240, 220, 60),
    ph::Pixel::RGBA(60, 190, 90),   ph::Pixel::RGBA(40, 190, 200),
    ph::Pixel::RGBA(60, 110, 230),  ph::Pixel::RGBA(150, 80, 220),
};
constexpr int kSwatchCount =
    static_cast<int>(sizeof(kSwatches) / sizeof(kSwatches[0]));

// Lenient "#RRGGBB"/"RRGGBB" (+ optional 2-digit alpha) parser.
bool ParseHexColor(const std::string& text, ph::Pixel* out) {
    std::string s = text;
    while (!s.empty() && (s.front() == '#' ||
                          std::isspace(static_cast<unsigned char>(
                              s.front())))) {
        s.erase(s.begin());
    }
    if (s.size() != 6 && s.size() != 8) return false;
    const auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    int v[8];
    for (std::size_t i = 0; i < s.size(); ++i) {
        v[i] = digit(s[i]);
        if (v[i] < 0) return false;
    }
    const auto u8 = [&](int hi, int lo) {
        return static_cast<std::uint8_t>(v[hi] * 16 + v[lo]);
    };
    const std::uint8_t a = s.size() == 8 ? u8(6, 7) : 255;
    if (out != nullptr) {
        *out = ph::Pixel::RGBA(u8(0, 1), u8(2, 3), u8(4, 5), a);
    }
    return true;
}

std::string HexOf(ph::Pixel p) {
    char buffer[10];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", p.r, p.g,
                  p.b);
    return buffer;
}

// Structural undo for layer removal (layer_ops.cc has no Remove op —
// its structural commands are file-local; PhotoCommand is the public
// seam). Holds the detached subtree and the re-insert coordinates.
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
        index_ = static_cast<size_t>(
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
    size_t index_ = 0;
    std::unique_ptr<ph::LayerNode> node_;
};

// "New Photo Document" dialog content: W × H text fields the host
// reads back on confirm.
class NewDocFields final : public Widgets::Widget {
public:
    NewDocFields() {
        w_input_ = std::make_shared<Widgets::TextInput>("1280");
        h_input_ = std::make_shared<Widgets::TextInput>("720");
        AddChild(w_input_);
        AddChild(h_input_);
    }
    int Width() const { return Parse(w_input_->GetValue(), 1280); }
    int Height() const { return Parse(h_input_->GetValue(), 720); }

    void Render(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        const auto b = GetBounds();
        Rendering::TextStyle label;
        label.color = tokens.muted;
        label.size = tokens.font_small;
        label.weight = 500;
        renderer.DrawText("Width",
                          {b.x, b.y + 10.0f, 60.0f, 16.0f}, label);
        renderer.DrawText("Height",
                          {b.x, b.y + 46.0f, 60.0f, 16.0f}, label);
    }
    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
        w_input_->Layout({bounds.x + 66.0f, bounds.y + 4.0f,
                          bounds.width - 66.0f, 26.0f});
        h_input_->Layout({bounds.x + 66.0f, bounds.y + 40.0f,
                          bounds.width - 66.0f, 26.0f});
    }
    bool HandleEvent(const Widgets::Event&) override { return false; }
    Layout::Size GetMinSize() const override { return {260.0f, 70.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {320.0f, 70.0f};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 70.0f};
    }

private:
    static int Parse(const std::string& text, int fallback) {
        const long v = std::strtol(text.c_str(), nullptr, 10);
        return v > 0 ? static_cast<int>(std::min<long>(v, 16384))
                     : fallback;
    }
    std::shared_ptr<Widgets::TextInput> w_input_;
    std::shared_ptr<Widgets::TextInput> h_input_;
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

}  // namespace

// ---------------------------------------------------------------------------
// Construction / children
// ---------------------------------------------------------------------------

PhotoWorkspaceWidget::PhotoWorkspaceWidget(void* parent_hwnd,
                                           Hooks hooks)
    : hooks_(std::move(hooks)), parent_hwnd_(parent_hwnd) {
    SetFocusable(true);
    SetClipsChildren(true);
}

void PhotoWorkspaceWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace W = XER::UI::Widgets;

    const auto make_btn = [this](const char* text, W::ButtonStyle style,
                                 std::function<void()> action,
                                 const std::string& tooltip) {
        auto b = std::make_shared<W::Button>(text, style);
        b->SetOnClick(std::move(action));
        if (!tooltip.empty()) b->SetTooltip(tooltip);
        AddChild(b);
        return b;
    };

    // ---- top bar: file ops + undo + tool options ----
    exit_btn_ = make_btn("< Video", W::ButtonStyle::Ghost,
                         [this] {
                             if (hooks_.on_exit) hooks_.on_exit();
                         },
                         "Back to the video workspace");
    new_btn_ = make_btn("New", W::ButtonStyle::Secondary,
                        [this] { NewDocumentDialog(); },
                        "New document (Ctrl+N)");
    open_btn_ = make_btn("Open", W::ButtonStyle::Secondary,
                         [this] { OpenDialog(); },
                         "Open .xerphoto or an image (Ctrl+O)");
    save_btn_ = make_btn("Save", W::ButtonStyle::Primary,
                         [this] { Save(false); },
                         "Save .xerphoto (Ctrl+S)");
    export_btn_ = make_btn("PNG", W::ButtonStyle::Secondary,
                           [this] { ExportPng(); },
                           "Export flattened PNG");
    undo_btn_ = make_btn("Undo", W::ButtonStyle::Ghost,
                         [this] { Undo(); }, "Ctrl+Z");
    redo_btn_ = make_btn("Redo", W::ButtonStyle::Ghost,
                         [this] { Redo(); }, "Ctrl+Y");

    const auto make_opt = [this](const char* text, float min, float max,
                                 float value,
                                 std::function<void(float)> on_change,
                                 const std::string& tooltip) {
        auto label = std::make_shared<W::Label>(text, true);
        auto slider = std::make_shared<W::Slider>(min, max, value);
        slider->SetTooltip(tooltip);
        slider->SetOnValueChanged(std::move(on_change));
        AddChild(label);
        AddChild(slider);
        return std::pair{label, slider};
    };
    auto size = make_opt("Size", 1.0f, 400.0f, 32.0f,
                         [this](float v) {
                             brush_params_.diameter_px = v;
                             Invalidate();
                         },
                         "Brush diameter (px) — [ and ] adjust");
    size_label_ = size.first;
    size_slider_ = size.second;
    auto hard = make_opt("Hard", 0.0f, 1.0f, 0.8f,
                         [this](float v) {
                             brush_params_.hardness = v;
                             Invalidate();
                         },
                         "Brush edge hardness");
    hardness_label_ = hard.first;
    hardness_slider_ = hard.second;
    auto op = make_opt("Opacity", 0.0f, 1.0f, 1.0f,
                       [this](float v) {
                           brush_params_.opacity = v;
                       },
                       "Stroke opacity");
    opacity_label_ = op.first;
    opacity_slider_ = op.second;
    auto tol = make_opt("Tol", 0.0f, 255.0f, 32.0f,
                        [this](float v) { wand_tol_ = v; },
                        "Magic-wand color tolerance");
    tol_label_ = tol.first;
    tol_slider_ = tol.second;
    auto fthr = make_opt("Fthr", 0.0f, 40.0f, 0.0f,
                         [this](float v) { sel_feather_ = v; },
                         "Marquee feather (px, applied on commit)");
    feather_label_ = fthr.first;
    feather_slider_ = fthr.second;
    invert_sel_btn_ =
        make_btn("Inv", W::ButtonStyle::Ghost,
                 [this] { InvertSelection(); },
                 "Invert selection");
    deselect_btn_ =
        make_btn("None", W::ButtonStyle::Ghost,
                 [this] { Deselect(); },
                 "Deselect (Ctrl+D)");
    refine_btn_ =
        make_btn("Refine", W::ButtonStyle::Ghost,
                 [this] { ToggleRefineEdgePanel(); },
                 "Refine Edge — radius/smooth/feather/contrast/shift "
                 "the selection edge (Select menu)");

    // Refine-Edge panel (modeless card over the viewport — see
    // RefinePanelRect/SyncRefinePanelVisibility). Slider params map
    // onto Photo::RefineEdgeParams; ApplyRefineEdge commits them.
    auto rf_radius = make_opt(
        "Radius", 0.0f, 40.0f, 0.0f,
        [this](float v) { refine_params_.radius_px = v; },
        "Refine band width (px) — pre-softens the edge zone");
    rf_radius_label_ = rf_radius.first;
    rf_radius_slider_ = rf_radius.second;
    auto rf_smooth = make_opt(
        "Smooth", 0.0f, 100.0f, 0.0f,
        [this](float v) { refine_params_.smooth = v / 100.0f; },
        "De-jag strength (%) — relaxes stepped marquee/lasso edges");
    rf_smooth_label_ = rf_smooth.first;
    rf_smooth_slider_ = rf_smooth.second;
    auto rf_feather = make_opt(
        "Feather", 0.0f, 50.0f, 0.0f,
        [this](float v) { refine_params_.feather_px = v; },
        "Edge softening (px)");
    rf_feather_label_ = rf_feather.first;
    rf_feather_slider_ = rf_feather.second;
    auto rf_contrast = make_opt(
        "Contrast", -100.0f, 100.0f, 0.0f,
        [this](float v) { refine_params_.contrast = v / 100.0f; },
        "Edge-ramp steepening about 50% (+ hard / - flat)");
    rf_contrast_label_ = rf_contrast.first;
    rf_contrast_slider_ = rf_contrast.second;
    auto rf_shift = make_opt(
        "Shift", -50.0f, 50.0f, 0.0f,
        [this](float v) { refine_params_.shift_px = v; },
        "Shift Edge (px) — + grows, - contracts the selection");
    rf_shift_label_ = rf_shift.first;
    rf_shift_slider_ = rf_shift.second;
    rf_apply_btn_ =
        make_btn("Apply", W::ButtonStyle::Primary,
                 [this] { ApplyRefineEdge(); },
                 "Apply to the active selection (undoable)");
    rf_close_btn_ =
        make_btn("Close", W::ButtonStyle::Secondary,
                 [this] { ToggleRefineEdgePanel(); },
                 "Close the Refine Edge panel");
    SyncRefinePanelVisibility();

    color_input_ = std::make_shared<W::TextInput>(
        HexOf(brush_params_.color));
    color_input_->SetPlaceholder("#RRGGBB");
    color_input_->SetTooltip("Foreground color, #RRGGBB");
    color_input_->SetOnSubmit([this] {
        SetFgColorFromHex(color_input_->GetValue());
    });
    color_input_->SetOnValueChanged([this](const std::string& v) {
        if (suppress_option_sync_) return;
        SetFgColorFromHex(v);
    });
    AddChild(color_input_);
    zoom_label_ = std::make_shared<W::Label>("", true);
    AddChild(zoom_label_);

    // ---- tool strip ----
    brush_btn_ = make_btn("B", W::ButtonStyle::Primary,
                          [this] { SetTool(Tool::Brush); },
                          "Brush (B)");
    eraser_btn_ = make_btn("E", W::ButtonStyle::Ghost,
                           [this] { SetTool(Tool::Eraser); },
                           "Eraser (E)");
    fill_btn_ = make_btn("F", W::ButtonStyle::Ghost,
                         [this] { SetTool(Tool::Fill); },
                         "Fill layer (F)");
    gradient_btn_ = make_btn("G", W::ButtonStyle::Ghost,
                             [this] { SetTool(Tool::Gradient); },
                             "Gradient — drag on canvas (G)");
    pan_btn_ = make_btn("H", W::ButtonStyle::Ghost,
                        [this] { SetTool(Tool::Pan); },
                        "Pan / hand (H, or middle-drag)");
    sel_rect_btn_ = make_btn("M", W::ButtonStyle::Ghost,
                             [this] { SetTool(Tool::SelectRect); },
                             "Rect marquee (M) — Shift adds, Alt subtracts");
    sel_ellipse_btn_ =
        make_btn("O", W::ButtonStyle::Ghost,
                 [this] { SetTool(Tool::SelectEllipse); },
                 "Ellipse marquee (O)");
    lasso_btn_ = make_btn("L", W::ButtonStyle::Ghost,
                          [this] { SetTool(Tool::Lasso); },
                          "Freehand lasso (L) — drag a closed loop");
    poly_lasso_btn_ =
        make_btn("P", W::ButtonStyle::Ghost,
                 [this] { SetTool(Tool::PolyLasso); },
                 "Polygon lasso (P) — click vertices, Enter/near-start to close");
    wand_btn_ = make_btn("W", W::ButtonStyle::Ghost,
                         [this] { SetTool(Tool::MagicWand); },
                         "Magic wand (W) — click a color to select");
    clone_btn_ =
        make_btn("S", W::ButtonStyle::Ghost,
                 [this] { SetTool(Tool::CloneStamp); },
                 "Clone stamp (S) — Alt+click picks the source");
    heal_btn_ = make_btn("J", W::ButtonStyle::Ghost,
                         [this] { SetTool(Tool::Healing); },
                         "Healing brush (J) — Alt+click picks the source");
    spot_btn_ = make_btn("K", W::ButtonStyle::Ghost,
                         [this] { SetTool(Tool::Spot); },
                         "Spot remove (K) — drag over blemishes");
    inpaint_btn_ =
        make_btn("I", W::ButtonStyle::Ghost,
                 [this] { SetTool(Tool::Inpaint); },
                 "Inpaint (I) — click to fill the active selection");

    // ---- layers panel controls ----
    add_layer_btn_ = make_btn("+Lyr", W::ButtonStyle::Secondary,
                              [this] { AddLayer(false); },
                              "New raster layer");
    add_group_btn_ = make_btn("+Grp", W::ButtonStyle::Secondary,
                              [this] { AddLayer(true); },
                              "New group (isolated composite)");
    add_adj_btn_ = make_btn("+Adj", W::ButtonStyle::Secondary,
                            [this] { AddAdjustmentLayer("hsl"); },
                            "New adjustment layer (Hue/Saturation)");
    mask_btn_ = make_btn("Mask", W::ButtonStyle::Ghost,
                         [this] { ToggleActiveLayerMask(); },
                         "Attach/detach an A8 mask on the active layer");
    dup_btn_ = make_btn("Dup", W::ButtonStyle::Ghost,
                        [this] { DuplicateActiveLayer(); },
                        "Duplicate layer");
    del_btn_ = make_btn("Del", W::ButtonStyle::Ghost,
                        [this] { DeleteActiveLayer(); },
                        "Delete layer (Del)");
    up_btn_ = make_btn("Up", W::ButtonStyle::Ghost,
                       [this] { MoveActiveLayer(1); },
                       "Move up (toward top)");
    down_btn_ = make_btn("Dn", W::ButtonStyle::Ghost,
                         [this] { MoveActiveLayer(-1); },
                         "Move down");
    merge_btn_ = make_btn("Merge", W::ButtonStyle::Ghost,
                          [this] { MergeActiveLayer(); },
                          "Merge down onto sibling below");
    flatten_btn_ = make_btn("Flat", W::ButtonStyle::Ghost,
                            [this] { FlattenImage(); },
                            "Flatten image");

    name_input_ = std::make_shared<W::TextInput>();
    name_input_->SetPlaceholder("layer name");
    name_input_->SetTooltip("Active layer name — Enter commits");
    name_input_->SetOnSubmit([this] {
        if (ph::LayerNode* n = ActiveLayer()) {
            ph::SetLayerAttributesCommand::Attributes a{
                name_input_->GetValue(), n->Visible(), n->Locked(),
                n->Opacity(), n->BlendMode(), n->Adjustment()};
            ApplyLayerAttributes(n->Id(), a, "Rename Layer");
        }
    });
    AddChild(name_input_);

    layer_opacity_slider_ =
        std::make_shared<W::Slider>(0.0f, 1.0f, 1.0f);
    layer_opacity_slider_->SetTooltip("Layer opacity");
    layer_opacity_slider_->SetOnValueChanged([this](float v) {
        if (suppress_option_sync_) return;
        if (ph::LayerNode* n = ActiveLayer()) {
            ph::SetLayerAttributesCommand::Attributes a{
                n->Name(), n->Visible(), n->Locked(), v,
                n->BlendMode(), n->Adjustment()};
            ApplyLayerAttributes(n->Id(), a, "Layer Opacity");
        }
    });
    AddChild(layer_opacity_slider_);

    blend_select_ = std::make_shared<W::Select>(
        XER::Bridge::Skia::BlendModeNames(), 0);
    blend_select_->SetTooltip("Layer blend mode");
    blend_select_->SetOnChanged([this](int index) {
        if (suppress_option_sync_) return;
        ph::LayerNode* n = ActiveLayer();
        const auto& names = XER::Bridge::Skia::BlendModeNames();
        if (n != nullptr && index >= 0 &&
            index < static_cast<int>(names.size())) {
            ph::SetLayerAttributesCommand::Attributes a{
                n->Name(), n->Visible(), n->Locked(), n->Opacity(),
                names[static_cast<size_t>(index)], n->Adjustment()};
            ApplyLayerAttributes(n->Id(), a, "Layer Blend Mode");
        }
    });
    AddChild(blend_select_);

    // ---- adjustment-layer editor (shown when the active layer is
    // an adjustment node): op Select + params "k=v;k=v" field.
    adj_op_select_ = std::make_shared<W::Select>(
        ph::AdjustmentOpLabels(), 0);
    adj_op_select_->SetTooltip("Adjustment type");
    adj_op_select_->SetOnChanged([this](int index) {
        if (suppress_option_sync_) return;
        ph::LayerNode* n = ActiveLayer();
        if (n == nullptr ||
            n->Kind() != ph::LayerKind::kAdjustment) {
            return;
        }
        const auto& ops = ph::AdjustmentOpRegistry();
        if (index < 0 || index >= static_cast<int>(ops.size())) {
            return;
        }
        const std::string& op_name =
            ops[static_cast<size_t>(index)].name;
        if (op_name == n->Adjustment().op) return;
        // Switching ops resets params to that op's defaults.
        ph::AdjustmentSpec spec = ph::DefaultAdjustmentSpec(op_name);
        SetActiveAdjustment(spec);
    });
    AddChild(adj_op_select_);
    adj_params_input_ = std::make_shared<W::TextInput>();
    adj_params_input_->SetPlaceholder("name=value;name=value");
    adj_params_input_->SetTooltip(
        "Adjustment params — Enter commits (undoable)");
    adj_params_input_->SetOnSubmit([this] {
        ph::LayerNode* n = ActiveLayer();
        if (n == nullptr ||
            n->Kind() != ph::LayerKind::kAdjustment) {
            return;
        }
        ph::AdjustmentSpec spec = n->Adjustment();
        if (ph::AdjustmentParamsFromText(
                adj_params_input_->GetValue(), &spec)) {
            SetActiveAdjustment(spec);
        } else {
            SetStatus("No params parsed — use name=value;...");
        }
    });
    AddChild(adj_params_input_);

    hint_label_ = std::make_shared<W::Label>("", true);
    AddChild(hint_label_);

    // Sync the tool styles + hint text for the default tool.
    SetTool(Tool::Brush);
}

// ---------------------------------------------------------------------------
// Document lifecycle
// ---------------------------------------------------------------------------

void PhotoWorkspaceWidget::SetDocument(
    std::unique_ptr<Photo::Document> doc, std::string display_name) {
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
    mask_overlay_bgra_.clear();
    mask_ovl_w_ = mask_ovl_h_ = 0;
    ant_phase_ = 0.0;
    EnsureSelectionDevice();
    RefreshSelectionState();
    need_fit_ = true;
    Reflatten();
    RebuildLayerRows();
    SyncLayerControls();
    RequestLayout();
    Invalidate();
}

void PhotoWorkspaceWidget::EnsureDocument() {
    if (doc_ != nullptr) return;
    SetDocument(std::make_unique<ph::Document>(1280, 720), "Untitled");
    if (doc_ != nullptr && doc_->Root().Children().empty()) {
        // A fresh canvas gets one transparent raster layer so the
        // user can paint immediately.
        AddLayerUndoable(ph::LayerKind::kRaster, "Layer 1",
                         &active_layer_);
        RebuildLayerRows();
        SyncLayerControls();
        Invalidate();
    }
}

Photo::LayerId PhotoWorkspaceWidget::FallbackLayerId() const {
    if (doc_ == nullptr) return ph::kInvalidLayerId;
    // Topmost leaf of the display order (reverse DFS of children).
    const auto* node = &doc_->Root();
    while (!node->Children().empty()) {
        node = node->Children().back().get();
    }
    return node != &doc_->Root() ? node->Id() : ph::kInvalidLayerId;
}

Photo::LayerNode* PhotoWorkspaceWidget::ActiveLayer() {
    if (doc_ == nullptr) return nullptr;
    return doc_->FindLayer(active_layer_);
}

void PhotoWorkspaceWidget::SelectLayer(Photo::LayerId id) {
    if (id == active_layer_) return;
    active_layer_ = id;
    // Mask editing is bound to the active layer's mask — drop out of
    // it when the newly selected node has none.
    if (mask_editing_) {
        const ph::LayerNode* n =
            doc_ != nullptr ? doc_->FindLayer(id) : nullptr;
        if (n == nullptr || n->Mask() == nullptr) {
            mask_editing_ = false;
            mask_overlay_bgra_.clear();
            mask_ovl_w_ = mask_ovl_h_ = 0;
        } else {
            RebuildMaskOverlay();
        }
    }
    SyncLayerControls();
    Invalidate();
}

void PhotoWorkspaceWidget::OnDocumentEdited() {
    doc_dirty_ = true;
    Reflatten();
    RebuildLayerRows();
    SyncLayerControls();
    Invalidate();
}

void PhotoWorkspaceWidget::Reflatten() {
    doc_bgra_.clear();
    doc_bgra_w_ = doc_bgra_h_ = 0;
    if (doc_ == nullptr) {
        Invalidate();
        return;
    }
    const image::ImageBuffer flat = doc_->Flatten();
    if (flat.IsValid() && flat.Format() == image::PixelFormat::RGBA8) {
        const int w = flat.Width();
        const int h = flat.Height();
        doc_bgra_.resize(static_cast<size_t>(w) * h * 4);
        const std::uint8_t* s = flat.Data();
        std::uint8_t* d = doc_bgra_.data();
        const std::size_t px = static_cast<size_t>(w) * h;
        for (std::size_t i = 0; i < px; ++i) {
            d[i * 4 + 0] = s[i * 4 + 2];  // B
            d[i * 4 + 1] = s[i * 4 + 1];  // G
            d[i * 4 + 2] = s[i * 4 + 0];  // R
            d[i * 4 + 3] = s[i * 4 + 3];  // A
        }
        doc_bgra_w_ = w;
        doc_bgra_h_ = h;
    }
    if (mask_editing_) RebuildMaskOverlay();
    RefreshSelectionState();
    Invalidate();
}

// ---------------------------------------------------------------------------
// File ops
// ---------------------------------------------------------------------------

void PhotoWorkspaceWidget::NewDocumentDialog() {
    auto fields = std::make_shared<NewDocFields>();
    auto dlg = Widgets::Dialog::Create("New Photo Document", fields,
                                       "Create", "Cancel");
    dlg->SetPanelWidth(360.0f);
    std::weak_ptr<NewDocFields> weak_fields = fields;
    std::weak_ptr<Widgets::Dialog> weak_dlg = dlg;
    // Dialog already confirms on Enter — no extra submit wiring needed.
    dlg->SetOnClosed([this, weak_dlg, weak_fields] {
        auto d = weak_dlg.lock();
        auto f = weak_fields.lock();
        if (d == nullptr || f == nullptr || !d->WasConfirmed()) return;
        const int w = std::clamp(f->Width(), 1, 16384);
        const int h = std::clamp(f->Height(), 1, 16384);
        doc_path_.clear();
        SetDocument(std::make_unique<ph::Document>(w, h), "Untitled");
        AddLayerUndoable(ph::LayerKind::kRaster, "Layer 1",
                         &active_layer_);
        RebuildLayerRows();
        SyncLayerControls();
        doc_dirty_ = false;
        SetStatus("New document " + std::to_string(w) + "x" +
                  std::to_string(h));
    });
    ShowOverlay(dlg);
}

void PhotoWorkspaceWidget::OpenDialog() {
#ifdef _WIN32
    const std::wstring picked = PickOpenFileW(
        static_cast<HWND>(parent_hwnd_),
        FilterSpec(L"Photo documents and images",
                   L"*.xerphoto;*.png;*.jpg;*.jpeg;*.jpe;*.bmp;"
                   L"*.webp;*.tif;*.tiff;*.gif;*.avif;*.psd;*.ora"),
        L"Open photo document or image");
    if (picked.empty()) return;
    OpenPath(XER::Helper::Path::ToUtf8(picked));
#else
    SetStatus("File dialogs are Windows-only", true);
#endif
}

bool PhotoWorkspaceWidget::OpenPath(const std::string& utf8_path) {
    const std::string path =
        XER::Helper::Path::NormalizeUtf8Path(utf8_path);
    std::string err;
    std::unique_ptr<ph::Document> doc;
    // .xerphoto by extension; everything else through the magic-detect
    // image loader (Document::OpenPng's name notwithstanding).
    const std::string lower = [&] {
        std::string s = path;
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        return s;
    }();
    if (lower.size() >= 9 &&
        lower.compare(lower.size() - 9, 9, ".xerphoto") == 0) {
        doc = ph::LoadDocument(path, &err);
    } else {
        doc = ph::Document::OpenPng(path, &err);
    }
    if (doc == nullptr) {
        SetStatus("Open failed: " + (err.empty() ? path : err), true);
        return false;
    }
    SetDocument(std::move(doc), Basename(path));
    if (lower.size() >= 9 &&
        lower.compare(lower.size() - 9, 9, ".xerphoto") == 0) {
        doc_path_ = path;
    } else {
        // An imported image is a new document — it has no .xerphoto
        // path until the user saves one.
        doc_path_.clear();
        doc_dirty_ = true;
    }
    SetStatus("Opened " + Basename(path));
    return true;
}

bool PhotoWorkspaceWidget::Save(bool choose_path) {
    if (doc_ == nullptr) {
        SetStatus("No document to save", true);
        return false;
    }
    std::string path = doc_path_;
    if (choose_path || path.empty()) {
#ifdef _WIN32
        std::wstring initial =
            XER::Helper::Path::FromUtf8(
                doc_name_.empty() ? "Untitled.xerphoto"
                                  : doc_name_ + ".xerphoto")
                .wstring();
        const std::wstring picked = PickSaveFileW(
            static_cast<HWND>(parent_hwnd_),
            FilterSpec(L"XER Photo Document", L"*.xerphoto"),
            L"Save photo document", L"xerphoto", initial);
        if (picked.empty()) return false;
        path = XER::Helper::Path::ToUtf8(picked);
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

bool PhotoWorkspaceWidget::ExportPng() {
    if (doc_ == nullptr) {
        SetStatus("No document to export", true);
        return false;
    }
#ifdef _WIN32
    const std::wstring picked = PickSaveFileW(
        static_cast<HWND>(parent_hwnd_),
        FilterSpec(L"PNG image", L"*.png"), L"Export PNG", L"png",
        XER::Helper::Path::FromUtf8(
            doc_name_.empty() ? "Untitled.png" : doc_name_ + ".png")
            .wstring());
    if (picked.empty()) return false;
    const std::string path = XER::Helper::Path::ToUtf8(picked);
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

// ---------------------------------------------------------------------------
// Undo
// ---------------------------------------------------------------------------

void PhotoWorkspaceWidget::Undo() {
    if (hist_ == nullptr || !hist_->Undo()) {
        SetStatus("Nothing to undo");
        return;
    }
    if (doc_ != nullptr && doc_->FindLayer(active_layer_) == nullptr) {
        active_layer_ = FallbackLayerId();
    }
    doc_dirty_ = true;
    Reflatten();
    RebuildLayerRows();
    SyncLayerControls();
    Invalidate();
}

void PhotoWorkspaceWidget::Redo() {
    if (hist_ == nullptr || !hist_->Redo()) {
        SetStatus("Nothing to redo");
        return;
    }
    if (doc_ != nullptr && doc_->FindLayer(active_layer_) == nullptr) {
        active_layer_ = FallbackLayerId();
    }
    doc_dirty_ = true;
    Reflatten();
    RebuildLayerRows();
    SyncLayerControls();
    Invalidate();
}

bool PhotoWorkspaceWidget::CanUndo() const {
    return hist_ != nullptr && hist_->CanUndo();
}
bool PhotoWorkspaceWidget::CanRedo() const {
    return hist_ != nullptr && hist_->CanRedo();
}
std::string PhotoWorkspaceWidget::UndoLabel() const {
    return hist_ != nullptr ? hist_->UndoLabel() : std::string();
}
std::string PhotoWorkspaceWidget::RedoLabel() const {
    return hist_ != nullptr ? hist_->RedoLabel() : std::string();
}

// ---------------------------------------------------------------------------
// Layer ops
// ---------------------------------------------------------------------------

bool PhotoWorkspaceWidget::AddLayerUndoable(Photo::LayerKind kind,
                                            const std::string& name,
                                            Photo::LayerId* out_id) {
    if (doc_ == nullptr || hist_ == nullptr) return false;
    // The public tree API only creates attached nodes; AddLayerCommand
    // wants a detached one — create + detach, then Execute re-adopts
    // it at Apply (top of the root stack).
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

void PhotoWorkspaceWidget::AddLayer(bool group) {
    EnsureDocument();
    if (doc_ == nullptr) return;
    int n = 1;
    for (size_t i = 0; i < rows_view_.size(); ++i) ++n;
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

void PhotoWorkspaceWidget::DeleteActiveLayer() {
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

void PhotoWorkspaceWidget::DuplicateActiveLayer() {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || node == nullptr) {
        SetStatus("No layer selected");
        return;
    }
    const ph::LayerId copy =
        ph::DuplicateLayer(*doc_, node->Id(), hist_.get());
    if (copy == ph::kInvalidLayerId) {
        SetStatus("Duplicate failed", true);
        return;
    }
    active_layer_ = copy;
    OnDocumentEdited();
    SetStatus("Duplicated " + node->Name());
}

void PhotoWorkspaceWidget::MoveActiveLayer(int direction) {
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
                       static_cast<size_t>(next), hist_.get())) {
        SetStatus("Move failed", true);
        return;
    }
    OnDocumentEdited();
}

void PhotoWorkspaceWidget::MergeActiveLayer() {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || node == nullptr || node->Parent() == nullptr) {
        SetStatus("No layer selected");
        return;
    }
    // The merge target (sibling below) becomes the new selection.
    ph::LayerNode* parent = node->Parent();
    const int index = doc_->ChildIndex(*parent, node->Id());
    ph::LayerId below = ph::kInvalidLayerId;
    if (index > 0) {
        below = parent->Children()[static_cast<size_t>(index - 1)]->Id();
    }
    if (!ph::MergeDown(*doc_, node->Id(), hist_.get())) {
        SetStatus("Merge failed — need a raster sibling below", true);
        return;
    }
    active_layer_ = below;
    OnDocumentEdited();
    SetStatus("Merged down");
}

void PhotoWorkspaceWidget::FlattenImage() {
    if (doc_ == nullptr) return;
    if (!ph::FlattenImage(*doc_, hist_.get())) {
        SetStatus("Flatten failed", true);
        return;
    }
    active_layer_ = FallbackLayerId();
    OnDocumentEdited();
    SetStatus("Flattened");
}

void PhotoWorkspaceWidget::ApplyLayerAttributes(
    Photo::LayerId id,
    const Photo::SetLayerAttributesCommand::Attributes& next,
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

void PhotoWorkspaceWidget::SetTool(Tool tool) {
    tool_ = tool;
    const auto style_of = [this](Tool t) {
        return tool_ == t ? Widgets::ButtonStyle::Primary
                          : Widgets::ButtonStyle::Ghost;
    };
    if (children_attached_) {
        brush_btn_->SetStyle(style_of(Tool::Brush));
        eraser_btn_->SetStyle(style_of(Tool::Eraser));
        fill_btn_->SetStyle(style_of(Tool::Fill));
        gradient_btn_->SetStyle(style_of(Tool::Gradient));
        pan_btn_->SetStyle(style_of(Tool::Pan));
        sel_rect_btn_->SetStyle(style_of(Tool::SelectRect));
        sel_ellipse_btn_->SetStyle(style_of(Tool::SelectEllipse));
        lasso_btn_->SetStyle(style_of(Tool::Lasso));
        poly_lasso_btn_->SetStyle(style_of(Tool::PolyLasso));
        wand_btn_->SetStyle(style_of(Tool::MagicWand));
        clone_btn_->SetStyle(style_of(Tool::CloneStamp));
        heal_btn_->SetStyle(style_of(Tool::Healing));
        spot_btn_->SetStyle(style_of(Tool::Spot));
        inpaint_btn_->SetStyle(style_of(Tool::Inpaint));
        const bool brushy = tool_ == Tool::Brush || tool_ == Tool::Eraser;
        const bool retouchy = tool_ == Tool::CloneStamp ||
                              tool_ == Tool::Healing ||
                              tool_ == Tool::Spot;
        const bool selecty = tool_ == Tool::SelectRect ||
                             tool_ == Tool::SelectEllipse ||
                             tool_ == Tool::Lasso ||
                             tool_ == Tool::PolyLasso ||
                             tool_ == Tool::MagicWand;
        size_label_->SetVisible(brushy || retouchy);
        size_slider_->SetVisible(brushy || retouchy);
        hardness_label_->SetVisible(tool_ == Tool::Brush);
        hardness_slider_->SetVisible(tool_ == Tool::Brush);
        opacity_label_->SetVisible(brushy || tool_ == Tool::Fill ||
                                   tool_ == Tool::Gradient);
        opacity_slider_->SetVisible(brushy || tool_ == Tool::Fill ||
                                    tool_ == Tool::Gradient);
        tol_label_->SetVisible(tool_ == Tool::MagicWand);
        tol_slider_->SetVisible(tool_ == Tool::MagicWand);
        feather_label_->SetVisible(selecty &&
                                   tool_ != Tool::MagicWand);
        feather_slider_->SetVisible(selecty &&
                                    tool_ != Tool::MagicWand);
        invert_sel_btn_->SetVisible(selecty);
        deselect_btn_->SetVisible(selecty);
        refine_btn_->SetVisible(selecty);
        static const char* const kHints[] = {
            "Brush — drag to paint on the active layer",
            "Eraser — drag to erase on the active layer",
            "Fill — click to fill the active layer with the color",
            "Gradient — drag a line on the canvas",
            "Pan — drag the canvas (middle-drag works with any tool)",
            "Rect marquee — drag; Shift adds, Alt subtracts",
            "Ellipse marquee — drag; Shift adds, Alt subtracts",
            "Lasso — drag a freehand loop to close a selection",
            "Polygon — click vertices; Enter or near-start closes",
            "Magic wand — click a color; Shift adds, Alt subtracts",
            "Clone stamp — Alt+click picks source, then drag",
            "Healing — Alt+click picks source, then drag",
            "Spot remove — drag over blemishes",
            "Inpaint — click to fill the active selection",
        };
        hint_label_->SetText(kHints[static_cast<int>(tool_)]);
    }
    Invalidate();
}

void PhotoWorkspaceWidget::SetStatus(const std::string& text,
                                     bool is_error) {
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

// ---------------------------------------------------------------------------
// Colors / brush params
// ---------------------------------------------------------------------------

ph::BrushParams PhotoWorkspaceWidget::CurrentBrushParams() const {
    ph::BrushParams p = brush_params_;
    p.eraser = tool_ == Tool::Eraser;
    return p;
}

void PhotoWorkspaceWidget::SetFgColor(Photo::Pixel color) {
    brush_params_.color = color;
    if (children_attached_) {
        suppress_option_sync_ = true;
        color_input_->SetValue(HexOf(color));
        suppress_option_sync_ = false;
    }
    Invalidate();
}

bool PhotoWorkspaceWidget::SetFgColorFromHex(const std::string& text) {
    ph::Pixel color;
    if (!ParseHexColor(text, &color)) return false;
    brush_params_.color = color;
    Invalidate();
    return true;
}

// ---------------------------------------------------------------------------
// View transform
// ---------------------------------------------------------------------------

float PhotoWorkspaceWidget::FitScale() const {
    if (doc_ == nullptr || viewport_.width <= 0.0f ||
        viewport_.height <= 0.0f || doc_->CanvasWidth() <= 0 ||
        doc_->CanvasHeight() <= 0) {
        return 1.0f;
    }
    return std::min(viewport_.width / doc_->CanvasWidth(),
                    viewport_.height / doc_->CanvasHeight());
}

float PhotoWorkspaceWidget::Scale() const {
    return FitScale() * zoom_;
}

Layout::Rect PhotoWorkspaceWidget::CanvasDest() const {
    if (doc_ == nullptr) return {};
    const float s = Scale();
    return {pan_x_, pan_y_, doc_->CanvasWidth() * s,
            doc_->CanvasHeight() * s};
}

Layout::Point PhotoWorkspaceWidget::ClientToDoc(
    const Layout::Point& p) const {
    const Layout::Rect d = CanvasDest();
    const float s = Scale();
    if (s <= 1e-6f) return {0.0f, 0.0f};
    return {(p.x - d.x) / s, (p.y - d.y) / s};
}

void PhotoWorkspaceWidget::ZoomAt(Layout::Point center, float factor) {
    const float next = std::clamp(zoom_ * factor, 0.05f, 40.0f);
    if (next == zoom_) return;
    // Doc-fraction under the cursor stays pinned (photo_viewer ZoomAt).
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
    Invalidate();
}

void PhotoWorkspaceWidget::ResetView() {
    zoom_ = 1.0f;
    if (doc_ != nullptr && viewport_.width > 0.0f &&
        viewport_.height > 0.0f) {
        const Layout::Rect d = CanvasDest();
        pan_x_ = viewport_.x + (viewport_.width - d.width) * 0.5f;
        pan_y_ = viewport_.y + (viewport_.height - d.height) * 0.5f;
    } else {
        pan_x_ = viewport_.x;
        pan_y_ = viewport_.y;
    }
    Invalidate();
}

void PhotoWorkspaceWidget::EnsureChecker(int w, int h, int origin_x,
                                         int origin_y) {
    if (w <= 0 || h <= 0) {
        checker_bgra_.clear();
        checker_w_ = checker_h_ = -1;
        return;
    }
    // origin_* is the SCREEN-space offset of the buffer's top-left
    // from the canvas dest's top-left — the pattern is anchored to the
    // canvas so it pans with the document. It repeats every two cells
    // (32px), so the cache key reduces to a phase in [0,32).
    constexpr int kCell = 16;
    const int phase_x = ((origin_x % (kCell * 2)) + kCell * 2) %
                        (kCell * 2);
    const int phase_y = ((origin_y % (kCell * 2)) + kCell * 2) %
                        (kCell * 2);
    if (checker_w_ == w && checker_h_ == h && checker_ox_ == phase_x &&
        checker_oy_ == phase_y) {
        return;
    }
    checker_w_ = w;
    checker_h_ = h;
    checker_ox_ = phase_x;
    checker_oy_ = phase_y;
    checker_bgra_.resize(static_cast<size_t>(w) * h * 4);
    // Two neutral grays — the classic transparency checker.
    const std::uint8_t a = 205, b = 158;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // (x + phase) >= 0 always — phase is already in [0,32).
            const int cx = (x + phase_x) / kCell;
            const int cy = (y + phase_y) / kCell;
            const std::uint8_t v = ((cx + cy) & 1) ? b : a;
            std::uint8_t* p =
                checker_bgra_.data() +
                (static_cast<size_t>(y) * w + x) * 4;
            p[0] = v;
            p[1] = v;
            p[2] = v;
            p[3] = 255;
        }
    }
}

// ---------------------------------------------------------------------------
// Stroke / fill / gradient
// ---------------------------------------------------------------------------

void PhotoWorkspaceWidget::BeginStroke(const Layout::Point& client) {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || node == nullptr) {
        SetStatus("No layer selected — add one in the Layers panel");
        return;
    }
    const bool paint_mask =
        mask_editing_ && node->Mask() != nullptr;
    if (!paint_mask &&
        (node->Kind() != ph::LayerKind::kRaster ||
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
        // Mask painting keys off dab ALPHA alone; the coverage value
        // (white restore / black erase) is chosen at composite time.
        params.color = ph::Pixel::RGBA(255, 255, 255, 255);
    }
    engine_ = std::make_unique<ph::BrushEngine>(params);
    engine_->BeginStroke(*scratch_);
    stroking_ = true;
    stroking_mask_ = paint_mask;
    const Layout::Point d = ClientToDoc(client);
    engine_->LineTo({d.x, d.y, 1.0f});
    Invalidate();
}

void PhotoWorkspaceWidget::ContinueStroke(const Layout::Point& client) {
    if (!stroking_ || engine_ == nullptr) return;
    const Layout::Point d = ClientToDoc(client);
    engine_->LineTo({d.x, d.y, 1.0f});
    Invalidate();
}

void PhotoWorkspaceWidget::EndStroke() {
    if (!stroking_ || engine_ == nullptr || scratch_ == nullptr) {
        stroking_ = false;
        return;
    }
    stroking_ = false;
    const bool mask_stroke = stroking_mask_;
    stroking_mask_ = false;
    const ph::IntRect dirty = engine_->EndStroke();
    bool ok = false;
    if (!dirty.IsEmpty() && doc_ != nullptr) {
        if (mask_stroke) {
            ph::LayerNode* node = ActiveLayer();
            if (node != nullptr && node->Mask() != nullptr) {
                const uint8_t value = MaskPaintValue();
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

void PhotoWorkspaceWidget::ApplyFillAt(const Layout::Point&) {
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

void PhotoWorkspaceWidget::FinishGradient() {
    if (!gradient_drag_ || doc_ == nullptr) {
        gradient_drag_ = false;
        return;
    }
    gradient_drag_ = false;
    ph::LayerNode* node = ActiveLayer();
    if (node == nullptr || node->Locked() || node->Device() == nullptr) {
        SetStatus("Select an unlocked raster layer", true);
        Invalidate();
        return;
    }
    const Layout::Point a = ClientToDoc(grad_start_);
    const Layout::Point b = ClientToDoc(grad_cur_);
    if (std::abs(a.x - b.x) + std::abs(a.y - b.y) < 2.0f) {
        Invalidate();
        return;   // degenerate drag — treat as a click, no fill
    }
    const ph::Pixel c = brush_params_.color;
    const ph::Pixel transparent =
        ph::Pixel::RGBA(c.r, c.g, c.b, 0);
    const XER::Bridge::Skia::GradientStops stops = {
        {0.0f, c.PackedU32()},
        {1.0f, transparent.PackedU32()},
    };
    if (!ph::FillLinearGradient(*doc_, node->Id(), a.x, a.y, b.x, b.y,
                                stops, hist_.get(), ClipMask())) {
        SetStatus("Gradient failed", true);
        Invalidate();
        return;
    }
    OnDocumentEdited();
    SetStatus("Gradient applied to " + node->Name());
}

// ---------------------------------------------------------------------------
// M4: selection (workspace-owned A8 device)
// ---------------------------------------------------------------------------

void PhotoWorkspaceWidget::EnsureSelectionDevice() {
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

const ph::TiledDevice* PhotoWorkspaceWidget::ClipMask() const {
    return selection_nonempty_ ? selection_.get() : nullptr;
}

bool PhotoWorkspaceWidget::HasSelection() const {
    return selection_nonempty_;
}

void PhotoWorkspaceWidget::RefreshSelectionState() {
    selection_nonempty_ =
        selection_ != nullptr && !ph::SelectionIsEmpty(*selection_);
    sel_outline_ = selection_nonempty_
                       ? ph::SelectionOutline(*selection_)
                       : std::vector<ph::IntPoint>{};
    // The Refine-Edge Apply button tracks "has selection" — cheap
    // because the sync only touches visibility/enabled flags.
    if (refine_panel_visible_) SyncRefinePanelVisibility();
}

void PhotoWorkspaceWidget::RunSelectionOp(
    const std::string& label,
    const std::function<void(ph::TiledDevice&)>& fn) {
    EnsureSelectionDevice();
    if (selection_ == nullptr || hist_ == nullptr) return;
    hist_->ExecuteDeviceOp(*selection_, label, fn);
    RefreshSelectionState();
    Invalidate();
}

void PhotoWorkspaceWidget::SelectAll() {
    RunSelectionOp("Select All",
                   [](ph::TiledDevice& s) { ph::SelectAll(s); });
}

void PhotoWorkspaceWidget::Deselect() {
    RunSelectionOp("Deselect",
                   [](ph::TiledDevice& s) { ph::SelectNone(s); });
}

void PhotoWorkspaceWidget::InvertSelection() {
    RunSelectionOp("Invert Selection",
                   [](ph::TiledDevice& s) { ph::InvertSelection(s); });
}

void PhotoWorkspaceWidget::FeatherSelectionPx(float radius_px) {
    if (radius_px <= 0.0f) return;
    RunSelectionOp("Feather Selection", [radius_px](ph::TiledDevice& s) {
        ph::FeatherSelection(s, radius_px);
    });
}

// ---------------------------------------------------------------------------
// M4+: Refine-Edge panel (modeless card over the viewport)
// ---------------------------------------------------------------------------

Layout::Rect PhotoWorkspaceWidget::RefinePanelRect() const {
    // Anchored to the viewport's top-left; sized for the five slider
    // rows + title + button row. Clamped into the viewport so a tiny
    // window never pushes the card off-canvas.
    const float w = std::min(300.0f, std::max(160.0f,
                                              viewport_.width - 16.0f));
    const float h = std::min(244.0f, std::max(140.0f,
                                              viewport_.height - 16.0f));
    return {viewport_.x + 8.0f, viewport_.y + 8.0f, w, h};
}

void PhotoWorkspaceWidget::SyncRefinePanelVisibility() {
    if (!children_attached_) return;
    const bool on = refine_panel_visible_;
    const std::shared_ptr<Widgets::Widget> panel_children[] = {
        rf_radius_label_,  rf_radius_slider_, rf_smooth_label_,
        rf_smooth_slider_, rf_feather_label_, rf_feather_slider_,
        rf_contrast_label_, rf_contrast_slider_, rf_shift_label_,
        rf_shift_slider_,  rf_apply_btn_,     rf_close_btn_};
    for (const auto& c : panel_children) {
        if (c != nullptr) c->SetVisible(on);
    }
    if (rf_apply_btn_ != nullptr) {
        rf_apply_btn_->SetEnabled(selection_nonempty_);
    }
}

void PhotoWorkspaceWidget::ToggleRefineEdgePanel() {
    refine_panel_visible_ = !refine_panel_visible_;
    SyncRefinePanelVisibility();
    Invalidate();
    RequestLayout();
    if (refine_panel_visible_) {
        SetStatus(selection_nonempty_
                      ? "Refine Edge — adjust, then Apply"
                      : "Refine Edge — make a selection first");
    }
}

void PhotoWorkspaceWidget::ApplyRefineEdge() {
    if (!selection_nonempty_) {
        SetStatus("No active selection to refine", true);
        return;
    }
    const ph::RefineEdgeParams p = refine_params_;
    const bool noop = p.radius_px <= 0.0f && p.smooth <= 0.0f &&
                      p.feather_px <= 0.0f && p.contrast == 0.0f &&
                      p.shift_px == 0.0f;
    if (noop) {
        SetStatus("Refine Edge — all sliders at zero");
        return;
    }
    RunSelectionOp("Refine Edge", [p](ph::TiledDevice& s) {
        ph::RefineSelectionEdge(s, p);
    });
    SetStatus("Refine Edge applied");
}

ph::SelectionCombine PhotoWorkspaceWidget::PendingCombine() const {
    return marq_combine_;
}

// ---------------------------------------------------------------------------
// M4: marquee gestures
// ---------------------------------------------------------------------------

static ph::IntRect DocRectFromPoints(const Layout::Point& a,
                                     const Layout::Point& b) {
    const float x0 = std::min(a.x, b.x);
    const float y0 = std::min(a.y, b.y);
    const float x1 = std::max(a.x, b.x);
    const float y1 = std::max(a.y, b.y);
    return {static_cast<int>(std::floor(x0)),
            static_cast<int>(std::floor(y0)),
            static_cast<int>(std::ceil(x1 - x0)),
            static_cast<int>(std::ceil(y1 - y0))};
}

void PhotoWorkspaceWidget::CommitMarquee() {
    if (!marquee_drag_) return;
    marquee_drag_ = false;
    const ph::SelectionCombine mode = marq_combine_;
    const float feather = sel_feather_;
    if (tool_ == Tool::Lasso) {
        auto pts = lasso_pts_;
        lasso_pts_.clear();
        RunSelectionOp("Lasso", [&, mode, feather](ph::TiledDevice& s) {
            ph::SelectPolygon(s, pts, feather, mode);
        });
        return;
    }
    const Layout::Point a = ClientToDoc(marq_anchor_);
    const Layout::Point b = ClientToDoc(marq_cur_);
    const ph::IntRect r = DocRectFromPoints(a, b);
    if (r.w < 1 || r.h < 1) return;   // click — keep old sel
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

std::vector<ph::IntPoint>
PhotoWorkspaceWidget::LiveMarqueePolygon() const {
    std::vector<ph::IntPoint> out;
    if (poly_active_ && poly_pts_.size() >= 2) {
        out = poly_pts_;
        return out;
    }
    if (!marquee_drag_) return out;
    const Layout::Point a = ClientToDoc(marq_anchor_);
    const Layout::Point b = ClientToDoc(marq_cur_);
    if (tool_ == Tool::Lasso) {
        return lasso_pts_;
    }
    const ph::IntRect r = DocRectFromPoints(a, b);
    if (r.w < 1 || r.h < 1) return out;
    if (tool_ == Tool::SelectEllipse) {
        // 32-gon inscribed in the drag rect.
        const float cx = r.x + r.w * 0.5f;
        const float cy = r.y + r.h * 0.5f;
        for (int i = 0; i < 32; ++i) {
            const float t =
                2.0f * 3.14159265f * static_cast<float>(i) / 32.0f;
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

void PhotoWorkspaceWidget::CommitPolyLasso() {
    if (!poly_active_) return;
    poly_active_ = false;
    auto pts = poly_pts_;
    poly_pts_.clear();
    const ph::SelectionCombine mode = marq_combine_;
    const float feather = sel_feather_;
    if (pts.size() < 3) return;
    RunSelectionOp(
        "Polygon Select", [pts, mode, feather](ph::TiledDevice& s) {
            ph::SelectPolygon(s, pts, feather, mode);
        });
}

void PhotoWorkspaceWidget::CancelSelectionGesture() {
    marquee_drag_ = false;
    poly_active_ = false;
    poly_pts_.clear();
    lasso_pts_.clear();
}

// ---------------------------------------------------------------------------
// M4: masks + adjustment layers
// ---------------------------------------------------------------------------

void PhotoWorkspaceWidget::ToggleActiveLayerMask() {
    ph::LayerNode* node = ActiveLayer();
    if (doc_ == nullptr || node == nullptr) {
        SetStatus("No layer selected");
        return;
    }
    if (node->HasMask()) {
        if (ph::DetachLayerMask(*doc_, node->Id(), hist_.get())) {
            mask_editing_ = false;
            mask_overlay_bgra_.clear();
            OnDocumentEdited();
            SetStatus("Removed mask from " + node->Name());
        } else {
            SetStatus("Could not remove mask", true);
        }
    } else {
        if (ph::AttachLayerMask(*doc_, node->Id(), hist_.get())) {
            OnDocumentEdited();
            SetStatus("Attached mask to " + node->Name() +
                      " — Alt+click its badge to paint it");
        } else {
            SetStatus("Could not attach mask", true);
        }
    }
}

void PhotoWorkspaceWidget::SetMaskEditing(bool on) {
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
        mask_overlay_bgra_.clear();
        mask_ovl_w_ = mask_ovl_h_ = 0;
    } else {
        RebuildMaskOverlay();
    }
    RebuildLayerRows();
    Invalidate();
}

uint8_t PhotoWorkspaceWidget::MaskPaintValue() const {
    if (tool_ == Tool::Eraser) return 0;
    const ph::Pixel c = brush_params_.color;
    // Rec.601 luma — paints the fg swatch's gray into the mask.
    return static_cast<uint8_t>(
        std::clamp((77 * c.r + 150 * c.g + 29 * c.b) >> 8, 0, 255));
}

void PhotoWorkspaceWidget::RebuildMaskOverlay() {
    mask_overlay_bgra_.clear();
    mask_ovl_w_ = mask_ovl_h_ = 0;
    ph::LayerNode* node = ActiveLayer();
    if (!mask_editing_ || doc_ == nullptr || node == nullptr ||
        node->Mask() == nullptr) {
        return;
    }
    const int w = doc_->CanvasWidth();
    const int h = doc_->CanvasHeight();
    std::vector<uint8_t> cov =
        node->Mask()->ReadRegionCoverage({0, 0, w, h});
    mask_overlay_bgra_.resize(static_cast<size_t>(w) * h * 4);
    // Quick-mask tint: hidden areas glow translucent red.
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        const uint8_t missing =
            static_cast<uint8_t>(255 - cov[i]);
        const uint8_t a = static_cast<uint8_t>((missing * 110) / 255);
        mask_overlay_bgra_[i * 4 + 0] = 0;     // B
        mask_overlay_bgra_[i * 4 + 1] = 0;     // G
        mask_overlay_bgra_[i * 4 + 2] = 255;   // R
        mask_overlay_bgra_[i * 4 + 3] = a;
    }
    mask_ovl_w_ = w;
    mask_ovl_h_ = h;
}

void PhotoWorkspaceWidget::AddAdjustmentLayer(const std::string& op) {
    if (doc_ == nullptr || hist_ == nullptr) return;
    const ph::AdjustmentOpInfo* info = ph::FindAdjustmentOp(op);
    if (info == nullptr) {
        SetStatus("Unknown adjustment op: " + op, true);
        return;
    }
    // Same create+detach+AddLayerCommand dance as AddLayerUndoable —
    // the adjustment spec rides on the detached node so redo keeps it.
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
    SetStatus("Added " + info->label);
}

bool PhotoWorkspaceWidget::SetActiveAdjustment(
    const ph::AdjustmentSpec& spec) {
    ph::LayerNode* n = ActiveLayer();
    if (n == nullptr || hist_ == nullptr) return false;
    ph::SetLayerAttributesCommand::Attributes a{
        n->Name(), n->Visible(), n->Locked(), n->Opacity(),
        n->BlendMode(), spec};
    ApplyLayerAttributes(n->Id(), a, "Adjustment Params");
    return true;   // failures surface through the status line
}

void PhotoWorkspaceWidget::SyncAdjustmentControls() {
    ph::LayerNode* n = ActiveLayer();
    const bool adj =
        n != nullptr && n->Kind() == ph::LayerKind::kAdjustment;
    adj_controls_visible_ = adj;
    if (!children_attached_) return;
    adj_op_select_->SetVisible(adj);
    adj_params_input_->SetVisible(adj);
    const bool rasterish =
        adj || (n != nullptr &&
                n->Kind() != ph::LayerKind::kGroup);
    blend_select_->SetVisible(n != nullptr);
    layer_opacity_slider_->SetVisible(n != nullptr);
    (void)rasterish;
    if (adj) {
        suppress_option_sync_ = true;
        const auto& ops = ph::AdjustmentOpRegistry();
        int sel = 0;
        for (size_t i = 0; i < ops.size(); ++i) {
            if (ops[i].name == n->Adjustment().op) {
                sel = static_cast<int>(i);
                break;
            }
        }
        adj_op_select_->SetSelectedIndex(sel);
        adj_params_input_->SetValue(
            ph::AdjustmentParamsToText(n->Adjustment()));
        suppress_option_sync_ = false;
    }
}

// ---------------------------------------------------------------------------
// M4: retouch tools
// ---------------------------------------------------------------------------

int PhotoWorkspaceWidget::RetouchRadius() const {
    return std::max(2,
                    static_cast<int>(brush_params_.diameter_px * 0.5f));
}

void PhotoWorkspaceWidget::RetouchDab(const Layout::Point& doc_pt) {
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
        // The source follows the drag: offset from where it started.
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

void PhotoWorkspaceWidget::ApplyInpaintOverSelection() {
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

// ---------------------------------------------------------------------------
// Layers panel
// ---------------------------------------------------------------------------

void PhotoWorkspaceWidget::RebuildLayerRows() {
    rows_view_.clear();
    row_hover_ = -1;
    if (doc_ == nullptr) {
        RequestLayout();
        return;
    }
    // DFS, display order top-to-bottom (siblings in reverse).
    const std::function<void(const ph::LayerNode&, int)> walk =
        [&](const ph::LayerNode& node, int depth) {
            const auto& kids = node.Children();
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                const ph::LayerNode& child = **it;
                LayerRow row;
                row.id = child.Id();
                row.depth = depth;
                row.name = child.Name();
                row.group = child.Kind() == ph::LayerKind::kGroup;
                row.adjustment =
                    child.Kind() == ph::LayerKind::kAdjustment;
                row.has_mask = child.HasMask();
                row.visible = child.Visible();
                row.locked = child.Locked();
                rows_view_.push_back(row);
                walk(child, depth + 1);
            }
        };
    walk(doc_->Root(), 0);
    RequestLayout();
}

void PhotoWorkspaceWidget::SyncLayerControls() {
    if (!children_attached_) return;
    suppress_option_sync_ = true;
    ph::LayerNode* n = doc_ != nullptr ? doc_->FindLayer(active_layer_)
                                       : nullptr;
    if (n != nullptr) {
        name_input_->SetValue(n->Name());
        layer_opacity_slider_->SetValue(n->Opacity());
        const auto& names = XER::Bridge::Skia::BlendModeNames();
        int sel = 0;
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == n->BlendMode()) {
                sel = static_cast<int>(i);
                break;
            }
        }
        blend_select_->SetSelectedIndex(sel);
    } else {
        name_input_->SetValue("");
        layer_opacity_slider_->SetValue(1.0f);
        blend_select_->SetSelectedIndex(0);
    }
    const bool has = n != nullptr;
    name_input_->SetEnabled(has);
    layer_opacity_slider_->SetEnabled(has);
    blend_select_->SetEnabled(has);
    del_btn_->SetEnabled(has);
    dup_btn_->SetEnabled(has);
    up_btn_->SetEnabled(has);
    down_btn_->SetEnabled(has);
    merge_btn_->SetEnabled(has);
    mask_btn_->SetEnabled(has);
    mask_btn_->SetText(mask_editing_ ? "Mask*" : "Mask");
    add_adj_btn_->SetEnabled(doc_ != nullptr);
    suppress_option_sync_ = false;
    SyncAdjustmentControls();
}

float PhotoWorkspaceWidget::LayersViewTop() const {
    const auto b = GetBounds();
    // header (24 + pad) + three 28px button rows + gaps — mirrors
    // Layout()'s `ly` walk.
    return b.y + kTopBarH + 130.0f;
}

float PhotoWorkspaceWidget::LayersViewBottom() const {
    const auto b = GetBounds();
    // Reserve the bottom stack: name (28) + opacity (28) + blend (28)
    // + hint (20) + padding — plus the adjustment op/params pair
    // (64) when the active layer is an adjustment node.
    return b.y + b.height - (adj_controls_visible_ ? 188.0f : 124.0f);
}

Layout::Rect PhotoWorkspaceWidget::LayerRowRect(int row) const {
    const auto b = GetBounds();
    const float panel_w = std::clamp(b.width * 0.24f, 210.0f, 300.0f);
    const float px = b.x + b.width - panel_w;
    return {px + 6.0f,
            LayersViewTop() +
                static_cast<float>(row) * kRowH - rows_scroll_,
            panel_w - 12.0f, kRowH};
}

int PhotoWorkspaceWidget::LayerRowAt(const Layout::Point& p) const {
    if (p.y < LayersViewTop() || p.y >= LayersViewBottom()) return -1;
    const int row = static_cast<int>(
        std::floor((p.y - LayersViewTop() + rows_scroll_) / kRowH));
    if (row < 0 || row >= static_cast<int>(rows_view_.size())) {
        return -1;
    }
    const auto b = GetBounds();
    const float panel_w = std::clamp(b.width * 0.24f, 210.0f, 300.0f);
    const float px = b.x + b.width - panel_w;
    if (p.x < px || p.x >= px + panel_w) return -1;
    return row;
}

void PhotoWorkspaceWidget::ToggleRowVisible(int row) {
    if (doc_ == nullptr || row < 0 ||
        row >= static_cast<int>(rows_view_.size())) {
        return;
    }
    const ph::LayerId id = rows_view_[static_cast<size_t>(row)].id;
    ph::LayerNode* n = doc_->FindLayer(id);
    if (n == nullptr) return;
    ph::SetLayerAttributesCommand::Attributes a{
        n->Name(), !n->Visible(), n->Locked(), n->Opacity(),
        n->BlendMode(), n->Adjustment()};
    ApplyLayerAttributes(id, a, "Layer Visibility");
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void PhotoWorkspaceWidget::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float right_edge = bounds.x + bounds.width;

    // ---- top bar ----
    const float ty = bounds.y + 6.0f;
    float x = bounds.x + kPad;
    const auto place = [&](const std::shared_ptr<Widgets::Button>& b,
                           float w) {
        b->Layout({x, ty, w, kBtnH}, tokens);
        x += w + 6.0f;
    };
    place(exit_btn_, 66.0f);
    place(new_btn_, 52.0f);
    place(open_btn_, 56.0f);
    place(save_btn_, 52.0f);
    place(export_btn_, 50.0f);
    x += 6.0f;
    place(undo_btn_, 52.0f);
    place(redo_btn_, 52.0f);
    undo_btn_->SetEnabled(CanUndo());
    redo_btn_->SetEnabled(CanRedo());

    // Tool options + colors share the remaining top-bar width; they
    // collapse gracefully — the hex input drops first, then labels.
    const float colors_w = 18.0f + kSwatchCount * 24.0f + 76.0f;
    float opt_right = right_edge - kPad - colors_w;
    if (opt_right < x + 360.0f) {
        color_input_->SetVisible(false);
        opt_right = right_edge - kPad - (colors_w - 76.0f);
    } else {
        color_input_->SetVisible(true);
    }
    // Options: [Size slider 90][val][Hard slider 70][val][Op slider 70][val]
    // + M4's Tol/Fthr sliders and Inv/None buttons for select tools.
    float ox = x;
    const bool brushy = tool_ == Tool::Brush || tool_ == Tool::Eraser;
    const bool retouchy = tool_ == Tool::CloneStamp ||
                          tool_ == Tool::Healing || tool_ == Tool::Spot;
    const bool selecty = tool_ == Tool::SelectRect ||
                         tool_ == Tool::SelectEllipse ||
                         tool_ == Tool::Lasso ||
                         tool_ == Tool::PolyLasso ||
                         tool_ == Tool::MagicWand;
    const auto place_opt = [&](const std::shared_ptr<Widgets::Label>& l,
                               const std::shared_ptr<Widgets::Slider>& s,
                               bool shown, const char* text, float sw) {
        if (!shown) {
            l->SetVisible(false);
            s->SetVisible(false);
            return;
        }
        const bool fits = ox + 34.0f + sw + 30.0f <= opt_right;
        l->SetVisible(fits);
        s->SetVisible(fits);
        if (!fits) return;
        l->SetText(text);
        l->Layout({ox, ty + 6.0f, 34.0f, 16.0f}, tokens);
        ox += 36.0f;
        s->Layout({ox, ty, sw, kBtnH}, tokens);
        ox += sw + 4.0f;
    };
    place_opt(size_label_, size_slider_, brushy || retouchy, "Size",
              96.0f);
    place_opt(hardness_label_, hardness_slider_, tool_ == Tool::Brush,
              "Hard", 76.0f);
    place_opt(opacity_label_, opacity_slider_,
              brushy || tool_ == Tool::Fill || tool_ == Tool::Gradient,
              "Op", 76.0f);
    place_opt(tol_label_, tol_slider_, tool_ == Tool::MagicWand, "Tol",
              76.0f);
    place_opt(feather_label_, feather_slider_,
              selecty && tool_ != Tool::MagicWand, "Fthr", 66.0f);
    const auto place_opt_btn =
        [&](const std::shared_ptr<Widgets::Button>& b, bool shown,
            float w) {
            if (!shown || ox + w > opt_right) {
                b->SetVisible(false);
                return;
            }
            b->SetVisible(true);
            b->Layout({ox, ty, w, kBtnH}, tokens);
            ox += w + 6.0f;
        };
    place_opt_btn(invert_sel_btn_, selecty, 40.0f);
    place_opt_btn(deselect_btn_, selecty, 46.0f);
    place_opt_btn(refine_btn_, selecty, 56.0f);

    // Colors: fg chip + swatches + hex input, right-aligned.
    float cx = right_edge - kPad;
    if (color_input_->IsVisible()) {
        cx -= 74.0f;
        color_input_->Layout({cx, ty, 70.0f, kBtnH}, tokens);
    }
    cx -= (kSwatchCount * 24.0f + 6.0f);
    swatch_rects_.resize(kSwatchCount);
    for (int i = 0; i < kSwatchCount; ++i) {
        swatch_rects_[static_cast<size_t>(i)] = {
            cx + static_cast<float>(i) * 24.0f, ty + 3.0f, 20.0f, 22.0f};
    }
    fg_rect_ = {cx - 26.0f, ty + 2.0f, 22.0f, 24.0f};
    // Zoom readout floats at the viewport's bottom-left corner.
    zoom_label_->Layout(
        {viewport_.x + 8.0f, bounds.y + bounds.height - 24.0f, 260.0f,
         18.0f},
        tokens);

    // ---- content bands ----
    const float content_top = bounds.y + kTopBarH;
    const float content_h = bounds.height - kTopBarH;
    const float panel_w =
        std::clamp(bounds.width * 0.24f, 210.0f, 300.0f);
    const float strip_x = bounds.x;
    const float panel_x = right_edge - panel_w;
    viewport_ = {strip_x + kToolStripW, content_top,
                 std::max(60.0f, panel_x - strip_x - kToolStripW),
                 std::max(60.0f, content_h)};
    if (need_fit_) {
        need_fit_ = false;
        ResetView();
    }

    // ---- tool strip ----
    const std::shared_ptr<Widgets::Button> tools[] = {
        brush_btn_,      eraser_btn_,   fill_btn_,    gradient_btn_,
        pan_btn_,        sel_rect_btn_, sel_ellipse_btn_, lasso_btn_,
        poly_lasso_btn_, wand_btn_,     clone_btn_,   heal_btn_,
        spot_btn_,       inpaint_btn_};
    float gy = content_top + 6.0f;
    for (const auto& b : tools) {
        b->Layout({strip_x + 4.0f, gy, kToolStripW - 8.0f, 30.0f},
                  tokens);
        gy += 34.0f;
        if (gy > bounds.y + bounds.height - 30.0f) {
            b->SetVisible(false);
        } else {
            b->SetVisible(true);
        }
    }

    // ---- layers panel controls ----
    // The "Layers" section header is drawn chrome at content_top..+24;
    // the button rows start below it.
    const float lx = panel_x + 6.0f;
    const float lw = panel_w - 12.0f;
    float ly = content_top + 28.0f;
    const float bw4 = (lw - 18.0f) / 4.0f;
    const std::shared_ptr<Widgets::Button> row1[] = {
        add_layer_btn_, add_group_btn_, add_adj_btn_, dup_btn_};
    for (int i = 0; i < 4; ++i) {
        row1[i]->Layout({lx + static_cast<float>(i) * (bw4 + 6.0f), ly,
                         bw4, kBtnH},
                        tokens);
    }
    ly += kBtnH + 6.0f;
    const std::shared_ptr<Widgets::Button> row2[] = {
        del_btn_, up_btn_, down_btn_, merge_btn_};
    for (int i = 0; i < 4; ++i) {
        row2[i]->Layout({lx + static_cast<float>(i) * (bw4 + 6.0f), ly,
                         bw4, kBtnH},
                        tokens);
    }
    ly += kBtnH + 6.0f;
    const std::shared_ptr<Widgets::Button> row3[] = {flatten_btn_,
                                                     mask_btn_};
    const float bw2 = (lw - 6.0f) / 2.0f;
    for (int i = 0; i < 2; ++i) {
        row3[i]->Layout({lx + static_cast<float>(i) * (bw2 + 6.0f), ly,
                         bw2, kBtnH},
                        tokens);
    }
    // Rows area is manual — clamp the scroll into the live range.
    const float view_h = LayersViewBottom() - LayersViewTop();
    const float content_rows =
        static_cast<float>(rows_view_.size()) * kRowH;
    rows_scroll_ = std::clamp(
        rows_scroll_, 0.0f, std::max(0.0f, content_rows - view_h));

    // Bottom stack: name input, opacity slider, blend select, hint —
    // plus the adjustment-layer editor pair when the active layer is
    // an adjustment node (SyncAdjustmentControls owns visibility).
    float by = bounds.y + bounds.height - 26.0f;
    hint_label_->Layout({lx, by, lw, 20.0f}, tokens);
    by -= 30.0f;
    if (adj_controls_visible_) {
        adj_params_input_->Layout({lx, by, lw, kBtnH}, tokens);
        by -= 32.0f;
        adj_op_select_->Layout({lx, by, lw, kBtnH}, tokens);
        by -= 32.0f;
    }
    blend_select_->Layout({lx, by, lw, kBtnH}, tokens);
    by -= 32.0f;
    layer_opacity_slider_->Layout({lx + 40.0f, by, lw - 40.0f, kBtnH},
                                  tokens);
    by -= 32.0f;
    name_input_->Layout({lx, by, lw, kBtnH}, tokens);
    // The rows' cached rects are recomputed lazily by LayerRowRect.

    // ---- Refine-Edge panel (modeless card over the viewport) ----
    if (refine_panel_visible_) {
        const Layout::Rect pr = RefinePanelRect();
        const float row_h = 28.0f;
        float ry = pr.y + 34.0f;   // below the drawn title band
        const auto rf_row =
            [&](const std::shared_ptr<Widgets::Label>& l,
                const std::shared_ptr<Widgets::Slider>& s) {
                l->Layout({pr.x + 12.0f, ry + 6.0f, 66.0f, 16.0f},
                          tokens);
                s->Layout({pr.x + 84.0f, ry, pr.width - 96.0f, row_h},
                          tokens);
                ry += row_h + 4.0f;
            };
        rf_row(rf_radius_label_, rf_radius_slider_);
        rf_row(rf_smooth_label_, rf_smooth_slider_);
        rf_row(rf_feather_label_, rf_feather_slider_);
        rf_row(rf_contrast_label_, rf_contrast_slider_);
        rf_row(rf_shift_label_, rf_shift_slider_);
        const float bw = (pr.width - 30.0f) * 0.5f;
        rf_apply_btn_->Layout({pr.x + 10.0f, ry + 2.0f, bw, kBtnH},
                              tokens);
        rf_close_btn_->Layout({pr.x + 20.0f + bw, ry + 2.0f, bw, kBtnH},
                              tokens);
    }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

namespace {

// Marching-ants style dashed polyline through `pts` (client coords,
// closed): alternating `dash`px runs along the path, phase-offset for
// animation. Each dash run gets `color`.
void DrawAntsPolyline(XER::UI::Rendering::Renderer& renderer,
                      const std::vector<Layout::Point>& pts,
                      Color color, float dash_px,
                      float phase_px, float thickness) {
    if (pts.size() < 2) return;
    const float period = dash_px * 2.0f;
    double d = std::fmod(static_cast<double>(phase_px),
                       static_cast<double>(period));
    bool drawing = d < dash_px;
    float run_left = drawing ? static_cast<float>(dash_px - d)
                             : static_cast<float>(period - d);
    for (size_t i = 0; i < pts.size(); ++i) {
        Layout::Point a = pts[i];
        const Layout::Point b = pts[(i + 1) % pts.size()];
        float seg_len = std::hypot(b.x - a.x, b.y - a.y);
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

}  // namespace

void PhotoWorkspaceWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.background, 0.0f);

    // Top bar surface + separator.
    renderer.FillRect({bounds.x, bounds.y, bounds.width, kTopBarH},
                      tokens.panel, 0.0f);
    renderer.DrawLine({bounds.x, bounds.y + kTopBarH},
                      {bounds.x + bounds.width, bounds.y + kTopBarH},
                      tokens.border, 1.0f);

    // Tool strip surface + separator.
    renderer.FillRect({bounds.x, bounds.y + kTopBarH, kToolStripW,
                       bounds.height - kTopBarH},
                      tokens.panel, 0.0f);
    renderer.DrawLine({bounds.x + kToolStripW, bounds.y + kTopBarH},
                      {bounds.x + kToolStripW,
                       bounds.y + bounds.height},
                      tokens.border, 1.0f);

    // ---- viewport ----
    renderer.FillRect(viewport_, tokens.background, 0.0f);
    renderer.StrokeRect(viewport_, tokens.border, 1.0f, 0.0f);

    if (doc_ != nullptr && doc_bgra_w_ > 0) {
        const Layout::Rect dest = CanvasDest();
        renderer.PushClip(viewport_);
        // Checkerboard under transparency — anchored to document
        // pixels (pan/zoom move the pattern with the canvas).
        const Layout::Rect clip = dest.Intersect(viewport_);
        if (clip.width > 0.5f && clip.height > 0.5f) {
            // Screen-space offset of the clip from the canvas origin —
            // the checker is anchored to the canvas, so the pattern
            // moves with pan/zoom.
            const int ox = static_cast<int>(
                std::lround(clip.x - dest.x));
            const int oy = static_cast<int>(
                std::lround(clip.y - dest.y));
            const int cw = static_cast<int>(std::ceil(clip.width));
            const int ch = static_cast<int>(std::ceil(clip.height));
            EnsureChecker(cw, ch, ox, oy);
            if (!checker_bgra_.empty()) {
                renderer.DrawFrameBgra(checker_bgra_.data(), cw, ch,
                                       clip, 1.0f);
            }
        }
        // Document composite.
        renderer.DrawFrameBgra(doc_bgra_.data(), doc_bgra_w_,
                               doc_bgra_h_, dest, 1.0f);

        // Live stroke overlay: the scratch TiledDevice's painted
        // extent blitted on top (srcOver preview; eraser previews at
        // half alpha as an "erase zone" hint — the dstOut result lands
        // at CompositeStroke on release).
        if (stroking_ && scratch_ != nullptr) {
            const ph::IntRect ext = scratch_->Extent();
            if (!ext.IsEmpty()) {
                const auto u32 = scratch_->ReadRegionU32(ext);
                std::vector<std::uint8_t> bgra;
                U32ToBgra(u32, &bgra);
                const float s = Scale();
                const Layout::Rect sd{dest.x + ext.x * s,
                                      dest.y + ext.y * s, ext.w * s,
                                      ext.h * s};
                renderer.DrawFrameBgra(
                    bgra.data(), ext.w, ext.h, sd,
                    tool_ == Tool::Eraser ? 0.5f : 1.0f);
            }
        }
        // Quick-mask tint while editing a layer mask (drawn after the
        // stroke preview so live mask dabs show through).
        if (mask_editing_ && !mask_overlay_bgra_.empty() &&
            mask_ovl_w_ > 0) {
            renderer.DrawFrameBgra(mask_overlay_bgra_.data(),
                                   mask_ovl_w_, mask_ovl_h_, dest,
                                   1.0f);
        }
        // Gradient drag preview: start->current line + endpoints.
        if (gradient_drag_) {
            renderer.DrawLine(grad_start_, grad_cur_, tokens.accent,
                              2.0f);
            renderer.StrokeRect(
                {grad_start_.x - 3.0f, grad_start_.y - 3.0f, 6.0f,
                 6.0f},
                tokens.accent, 1.0f, 3.0f);
        }
        // Marching ants: live marquee shape takes precedence over the
        // committed outline (drawn black + offset white = ants).
        const float s = Scale();
        const auto to_client = [&](const ph::IntPoint& p) {
            return Layout::Point{dest.x + p.x * s,
                                 dest.y + p.y * s};
        };
        const auto live = LiveMarqueePolygon();
        const auto& poly =
            live.size() >= 2 ? live : sel_outline_;
        if (poly.size() >= 2) {
            std::vector<Layout::Point> ants;
            ants.reserve(poly.size());
            for (const ph::IntPoint& p : poly) {
                ants.push_back(to_client(p));
            }
            const float phase = std::fmod(
                static_cast<float>(ant_phase_) * 40.0f, 8.0f);
            DrawAntsPolyline(renderer, ants, tokens.foreground, 4.0f,
                             phase, 1.0f);
            DrawAntsPolyline(renderer, ants, tokens.background, 4.0f,
                             phase + 4.0f, 1.0f);
        }
        // Clone/heal source marker while retouching.
        if ((tool_ == Tool::CloneStamp || tool_ == Tool::Healing) &&
            clone_src_set_) {
            const Layout::Point sc = to_client(clone_src_);
            renderer.DrawLine({sc.x - 6.0f, sc.y}, {sc.x + 6.0f, sc.y},
                              tokens.accent, 1.5f);
            renderer.DrawLine({sc.x, sc.y - 6.0f}, {sc.x, sc.y + 6.0f},
                              tokens.accent, 1.5f);
        }
        // Canvas hairline.
        renderer.StrokeRect(dest,
                            VeTheme::WithAlpha(tokens.foreground, 0.3f),
                            1.0f, 0.0f);
        renderer.PopClip();
    } else {
        Rendering::TextStyle style;
        style.color = tokens.muted;
        style.size = tokens.font_body;
        renderer.DrawText(
            "No document — New (Ctrl+N) or Open (Ctrl+O)",
            {viewport_.x, viewport_.y + viewport_.height * 0.45f,
             viewport_.width, 24.0f},
            style);
    }

    // Brush cursor: a rounded ring tracking the doc-space diameter at
    // the cursor — painter-style size feedback over the canvas.
    if (cursor_in_vp_ &&
        (tool_ == Tool::Brush || tool_ == Tool::Eraser ||
         tool_ == Tool::CloneStamp || tool_ == Tool::Healing ||
         tool_ == Tool::Spot)) {
        const float d = std::max(4.0f, brush_params_.diameter_px *
                                           Scale());
        const Layout::Rect ring{cursor_pos_.x - d * 0.5f,
                                cursor_pos_.y - d * 0.5f, d, d};
        renderer.PushClip(viewport_);
        renderer.StrokeRect(ring, tokens.foreground, 1.0f, d * 0.5f);
        renderer.StrokeRect(ring,
                            VeTheme::WithAlpha(tokens.accent, 0.7f),
                            0.5f, d * 0.5f);
        renderer.PopClip();
    }

    // ---- colors: fg chip + swatches ----
    const Color fg = ToColor(brush_params_.color);
    for (int i = 0; i < static_cast<int>(swatch_rects_.size()); ++i) {
        const auto& r = swatch_rects_[static_cast<size_t>(i)];
        if (r.width <= 0.0f) continue;
        renderer.FillRect(r, ToColor(kSwatches[i]),
                          tokens.radius_small);
        renderer.StrokeRect(r, tokens.border, 1.0f,
                            tokens.radius_small);
    }
    if (fg_rect_.width > 0.0f) {
        renderer.FillRect(fg_rect_, fg, tokens.radius_small);
        renderer.StrokeRect(fg_rect_, tokens.foreground, 1.5f,
                            tokens.radius_small);
    }

    // ---- layers panel ----
    const float panel_w =
        std::clamp(bounds.width * 0.24f, 210.0f, 300.0f);
    const Layout::Rect panel{bounds.x + bounds.width - panel_w,
                             bounds.y + kTopBarH, panel_w,
                             bounds.height - kTopBarH};
    renderer.FillRect(panel, tokens.surface, 0.0f);
    renderer.DrawLine({panel.x, panel.y},
                      {panel.x, panel.y + panel.height}, tokens.border,
                      1.0f);
    VeIcons::DrawSectionHeader(
        renderer, tokens,
        {panel.x + 8.0f, bounds.y + kTopBarH + 2.0f, panel_w - 16.0f,
         VeTheme::kRowHeight},
        "Layers", false);

    // Rows.
    const float view_top = LayersViewTop();
    const float view_bottom = LayersViewBottom();
    renderer.PushClip({panel.x, view_top, panel.width,
                       std::max(0.0f, view_bottom - view_top)});
    Rendering::TextStyle name_style;
    name_style.size = tokens.font_small;
    for (size_t i = 0; i < rows_view_.size(); ++i) {
        const LayerRow& row = rows_view_[i];
        const Layout::Rect r = LayerRowRect(static_cast<int>(i));
        if (r.y + r.height < view_top || r.y > view_bottom) continue;
        const bool selected = row.id == active_layer_;
        if (selected) {
            renderer.FillRect(r, VeTheme::WithAlpha(tokens.accent, 0.14f),
                              tokens.radius_small);
            renderer.FillRect({r.x, r.y + 3.0f, 2.0f, r.height - 6.0f},
                              tokens.accent, 1.0f);
        } else if (static_cast<int>(i) == row_hover_) {
            renderer.FillRect(r, tokens.hover, tokens.radius_small);
        }
        // Eye box: the visibility toggle (hit area = eye_rect_).
        const Layout::Rect eye{r.x + 4.0f + row.depth * 10.0f, r.y + 5.0f,
                               16.0f, 16.0f};
        if (!VeIcons::DrawToggleIcon(renderer, row.visible,
                                     "track.visible", "track.hidden", 14,
                                     row.visible ? tokens.foreground
                                                 : tokens.muted,
                                     eye)) {
            Rendering::TextStyle eye_style;
            eye_style.color =
                row.visible ? tokens.foreground : tokens.muted;
            eye_style.size = 11.0f;
            renderer.DrawText(row.visible ? "*" : "-", eye, eye_style);
        }
        // Group / adjustment badge + name.
        float tx = eye.Right() + 6.0f;
        if (row.group || row.adjustment) {
            renderer.FillRect({tx, r.y + 6.0f, 14.0f, 14.0f},
                              row.adjustment ? tokens.accent
                                             : tokens.secondary,
                              3.0f);
            Rendering::TextStyle badge;
            badge.color = tokens.foreground;
            badge.size = 10.0f;
            badge.weight = 700;
            renderer.DrawText(row.adjustment ? "A" : "G",
                              {tx + 2.5f, r.y + 6.5f, 12.0f, 14.0f},
                              badge);
            tx += 18.0f;
        }
        // Mask badge on the row's right edge: click toggles mask
        // editing for the active layer (highlighted while editing).
        if (row.has_mask) {
            const Layout::Rect mb{r.Right() - 20.0f, r.y + 5.0f, 16.0f,
                                  16.0f};
            const bool editing =
                mask_editing_ && row.id == active_layer_;
            renderer.FillRect(
                mb,
                editing ? tokens.accent
                        : VeTheme::WithAlpha(tokens.secondary, 0.6f),
                3.0f);
            Rendering::TextStyle badge;
            badge.color = tokens.foreground;
            badge.size = 10.0f;
            badge.weight = 700;
            renderer.DrawText("M", {mb.x + 3.0f, mb.y + 0.5f, 12.0f,
                                    14.0f},
                              badge);
        }
        name_style.color = row.visible ? tokens.foreground : tokens.muted;
        name_style.weight = selected ? 600 : 400;
        renderer.DrawText(
            row.name + (row.locked ? " [locked]" : ""),
            {tx, r.y + 4.0f,
             std::max(0.0f, r.Right() - tx -
                                (row.has_mask ? 26.0f : 4.0f)),
             18.0f},
            name_style);
    }
    renderer.PopClip();
    if (rows_view_.empty()) {
        renderer.DrawText(
            "no layers",
            {panel.x + 12.0f, view_top + 8.0f, panel.width - 24.0f,
             18.0f},
            {tokens.muted, tokens.font_small, 400});
    }
    // "Opacity" caption before the slider.
    const float op_y = bounds.y + bounds.height - 26.0f - 30.0f - 32.0f;
    renderer.DrawText(
        "Opacity",
        {panel.x + 6.0f, op_y + 6.0f, 40.0f, 16.0f},
        {tokens.muted, tokens.font_small, 400});
    // Zoom caption bottom-left of the viewport.
    char zoom_text[48];
    std::snprintf(zoom_text, sizeof(zoom_text), "%d%%  |  wheel: zoom",
                  static_cast<int>(zoom_ * 100.0f));
    zoom_label_->SetText(zoom_text);

    // ---- Refine-Edge panel card (children draw themselves on top) --
    if (refine_panel_visible_) {
        const Layout::Rect pr = RefinePanelRect();
        renderer.FillRect(pr, tokens.surface, tokens.radius_large);
        renderer.StrokeRect(pr, tokens.border, 1.0f,
                            tokens.radius_large);
        renderer.FillRect({pr.x, pr.y, 3.0f, pr.height}, tokens.accent,
                          tokens.radius_large);
        VeIcons::DrawSectionHeader(
            renderer, tokens,
            {pr.x + 10.0f, pr.y + 2.0f, pr.width - 20.0f,
             VeTheme::kRowHeight},
            "Refine Edge", false);
        if (!selection_nonempty_) {
            renderer.DrawText(
                "no active selection",
                {pr.x + 12.0f, pr.y + 30.0f, pr.width - 24.0f, 16.0f},
                {tokens.muted, tokens.font_small, 400});
        }
    }

    if (IsFocused()) {
        RenderFocusRing(renderer, tokens, tokens.radius_large);
    }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

bool PhotoWorkspaceWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    using Widgets::Event;
    using Widgets::KeyCode;
    using Widgets::MouseButton;

    switch (event.type) {
        case Event::Type::MouseWheel: {
            // The modeless Refine-Edge card swallows wheel input over
            // its rect — a scroll there must not zoom the canvas under
            // the panel (its own children handle their fields first).
            if (refine_panel_visible_ &&
                RefinePanelRect().Contains(event.position)) {
                return true;
            }
            // Layers list scrolls; the viewport zooms at the cursor.
            const int row_zone = LayerRowAt(event.position);
            const auto b = GetBounds();
            const float panel_w =
                std::clamp(b.width * 0.24f, 210.0f, 300.0f);
            const bool in_panel =
                event.position.x >= b.x + b.width - panel_w &&
                event.position.y >= LayersViewTop();
            if (row_zone >= 0 || in_panel) {
                const float view_h =
                    LayersViewBottom() - LayersViewTop();
                const float range = std::max(
                    0.0f,
                    static_cast<float>(rows_view_.size()) * kRowH -
                        view_h);
                if (range > 0.0f) {
                    rows_scroll_ = std::clamp(
                        rows_scroll_ - event.wheel_delta * 0.5f, 0.0f,
                        range);
                    Invalidate();
                }
                return true;
            }
            if (viewport_.Contains(event.position)) {
                ZoomAt(event.position,
                       event.wheel_delta > 0 ? 1.15f : 1.0f / 1.15f);
                return true;
            }
            return false;
        }

        case Event::Type::MouseDown: {
            if (event.mouse_button != MouseButton::Left &&
                event.mouse_button != MouseButton::Middle) {
                return false;
            }
            const Layout::Point p = event.position;
            // The modeless Refine-Edge card eats pointer presses over
            // its rect — the canvas marquee/pan logic must not see a
            // click that landed on the panel's background.
            if (refine_panel_visible_ && RefinePanelRect().Contains(p)) {
                return true;
            }
            // Swatch pick (top bar).
            for (int i = 0;
                 i < static_cast<int>(swatch_rects_.size()); ++i) {
                if (swatch_rects_[static_cast<size_t>(i)].Contains(p)) {
                    SetFgColor(kSwatches[i]);
                    return true;
                }
            }
            // Layers rows: eye toggle / mask badge / row select.
            const int row = LayerRowAt(p);
            if (row >= 0) {
                const LayerRow& lr =
                    rows_view_[static_cast<size_t>(row)];
                const Layout::Rect rr = LayerRowRect(row);
                const Layout::Rect eye{
                    rr.x + 4.0f + lr.depth * 10.0f, rr.y + 3.0f, 20.0f,
                    rr.height - 6.0f};
                const Layout::Rect badge{rr.Right() - 20.0f, rr.y + 5.0f,
                                         16.0f, 16.0f};
                if (eye.Contains(p)) {
                    ToggleRowVisible(row);
                } else if (lr.has_mask && badge.Contains(p)) {
                    if (lr.id == active_layer_) {
                        SetMaskEditing(!mask_editing_);
                    } else {
                        SelectLayer(lr.id);
                    }
                } else {
                    SelectLayer(lr.id);
                }
                return true;
            }
            if (!viewport_.Contains(p)) return false;
            if (event.mouse_button == MouseButton::Middle ||
                tool_ == Tool::Pan) {
                panning_ = true;
                pan_last_ = p;
                return true;
            }
            switch (tool_) {
                case Tool::Brush:
                case Tool::Eraser:
                    BeginStroke(p);
                    return stroking_;
                case Tool::Fill:
                    ApplyFillAt(p);
                    return true;
                case Tool::Gradient:
                    gradient_drag_ = true;
                    grad_start_ = p;
                    grad_cur_ = p;
                    Invalidate();
                    return true;
                case Tool::SelectRect:
                case Tool::SelectEllipse:
                case Tool::Lasso:
                    marq_combine_ =
                        event.is_shift
                            ? ph::SelectionCombine::kAdd
                            : event.is_alt
                                  ? ph::SelectionCombine::kSubtract
                                  : ph::SelectionCombine::kReplace;
                    marquee_drag_ = true;
                    marq_anchor_ = marq_cur_ = p;
                    lasso_pts_.clear();
                    if (tool_ == Tool::Lasso) {
                        const Layout::Point d = ClientToDoc(p);
                        lasso_pts_.push_back(
                            {static_cast<int>(std::lround(d.x)),
                             static_cast<int>(std::lround(d.y))});
                    }
                    Invalidate();
                    return true;
                case Tool::PolyLasso: {
                    marq_combine_ =
                        event.is_shift
                            ? ph::SelectionCombine::kAdd
                            : event.is_alt
                                  ? ph::SelectionCombine::kSubtract
                                  : ph::SelectionCombine::kReplace;
                    const Layout::Point d = ClientToDoc(p);
                    const ph::IntPoint pt{
                        static_cast<int>(std::lround(d.x)),
                        static_cast<int>(std::lround(d.y))};
                    if (!poly_active_) {
                        poly_active_ = true;
                        poly_pts_.clear();
                    } else if (poly_pts_.size() >= 3) {
                        // Click near the start vertex closes the loop.
                        const Layout::Rect dest = CanvasDest();
                        const float sc = Scale();
                        const Layout::Point c0{
                            dest.x + poly_pts_.front().x * sc,
                            dest.y + poly_pts_.front().y * sc};
                        if (std::hypot(p.x - c0.x, p.y - c0.y) <=
                            10.0f) {
                            CommitPolyLasso();
                            return true;
                        }
                    }
                    poly_pts_.push_back(pt);
                    Invalidate();
                    return true;
                }
                case Tool::MagicWand: {
                    if (doc_ == nullptr) return false;
                    marq_combine_ =
                        event.is_shift
                            ? ph::SelectionCombine::kAdd
                            : event.is_alt
                                  ? ph::SelectionCombine::kSubtract
                                  : ph::SelectionCombine::kReplace;
                    const Layout::Point d = ClientToDoc(p);
                    const int sx =
                        static_cast<int>(std::floor(d.x));
                    const int sy =
                        static_cast<int>(std::floor(d.y));
                    if (sx < 0 || sy < 0 ||
                        sx >= doc_->CanvasWidth() ||
                        sy >= doc_->CanvasHeight()) {
                        return true;
                    }
                    const auto flat = doc_->FlattenU32();
                    const ph::SelectionCombine mode = marq_combine_;
                    const int tol =
                        static_cast<int>(std::lround(wand_tol_));
                    const int w = doc_->CanvasWidth();
                    const int h = doc_->CanvasHeight();
                    const float feather = sel_feather_;
                    RunSelectionOp(
                        "Magic Wand",
                        [&, flat, w, h, sx, sy, tol, mode,
                         feather](ph::TiledDevice& s) {
                            ph::SelectColorRange(s, flat, w, h, sx, sy,
                                                 tol, mode);
                            if (feather > 0.0f) {
                                ph::FeatherSelection(s, feather);
                            }
                        });
                    return true;
                }
                case Tool::CloneStamp:
                case Tool::Healing: {
                    const Layout::Point d = ClientToDoc(p);
                    const ph::IntPoint pt{
                        static_cast<int>(std::lround(d.x)),
                        static_cast<int>(std::lround(d.y))};
                    if (event.is_alt) {
                        clone_src_ = pt;
                        clone_src_set_ = true;
                        SetStatus("Source picked — drag to paint");
                        Invalidate();
                        return true;
                    }
                    if (!clone_src_set_) {
                        SetStatus(
                            "Alt+click to pick a source first", true);
                        return true;
                    }
                    retouch_drag_ = true;
                    retouch_origin_ = pt;
                    if (hist_ != nullptr) {
                        hist_->BeginMacro(
                            tool_ == Tool::CloneStamp
                                ? "Clone Stamp"
                                : "Healing Brush");
                    }
                    RetouchDab(d);
                    return true;
                }
                case Tool::Spot: {
                    const Layout::Point d = ClientToDoc(p);
                    retouch_drag_ = true;
                    retouch_origin_ =
                        {static_cast<int>(std::lround(d.x)),
                         static_cast<int>(std::lround(d.y))};
                    if (hist_ != nullptr) {
                        hist_->BeginMacro("Spot Remove");
                    }
                    RetouchDab(d);
                    return true;
                }
                case Tool::Inpaint:
                    ApplyInpaintOverSelection();
                    return true;
                default:
                    return false;
            }
        }

        case Event::Type::MouseMove: {
            const int row = LayerRowAt(event.position);
            if (row != row_hover_) {
                row_hover_ = row;
                Invalidate();
            }
            const bool in_vp = viewport_.Contains(event.position);
            if (in_vp != cursor_in_vp_) {
                cursor_in_vp_ = in_vp;
                Invalidate();
            }
            cursor_pos_ = event.position;
            if (panning_) {
                pan_x_ += event.position.x - pan_last_.x;
                pan_y_ += event.position.y - pan_last_.y;
                pan_last_ = event.position;
                Invalidate();
                return true;
            }
            if (stroking_) {
                ContinueStroke(event.position);
                return true;
            }
            if (gradient_drag_) {
                grad_cur_ = event.position;
                Invalidate();
                return true;
            }
            if (marquee_drag_) {
                marq_cur_ = event.position;
                if (tool_ == Tool::Lasso) {
                    const Layout::Point d =
                        ClientToDoc(event.position);
                    lasso_pts_.push_back(
                        {static_cast<int>(std::lround(d.x)),
                         static_cast<int>(std::lround(d.y))});
                }
                Invalidate();
                return true;
            }
            if (retouch_drag_) {
                RetouchDab(ClientToDoc(event.position));
                return true;
            }
            return row >= 0;
        }

        case Event::Type::MouseUp: {
            if (panning_) {
                panning_ = false;
                return true;
            }
            if (stroking_) {
                EndStroke();
                return true;
            }
            if (gradient_drag_) {
                grad_cur_ = event.position;
                FinishGradient();
                return true;
            }
            if (marquee_drag_) {
                marq_cur_ = event.position;
                if (tool_ == Tool::Lasso &&
                    lasso_pts_.size() >= 2) {
                    const Layout::Point d =
                        ClientToDoc(event.position);
                    lasso_pts_.push_back(
                        {static_cast<int>(std::lround(d.x)),
                         static_cast<int>(std::lround(d.y))});
                }
                CommitMarquee();
                return true;
            }
            if (retouch_drag_) {
                retouch_drag_ = false;
                if (hist_ != nullptr) hist_->EndMacro();
                return true;
            }
            return false;
        }

        case Event::Type::Click:
            // Selection happens on MouseDown so drags over a row's eye
            // don't re-select; clicks are swallowed to keep the event
            // from bubbling into other widgets.
            return LayerRowAt(event.position) >= 0 ||
                   (refine_panel_visible_ &&
                    RefinePanelRect().Contains(event.position)) ||
                   viewport_.Contains(event.position);

        case Event::Type::KeyDown: {
            if (event.key == KeyCode::Delete && !event.is_ctrl &&
                !event.is_alt) {
                DeleteActiveLayer();
                return true;
            }
            if (event.key == KeyCode::Escape) {
                if (refine_panel_visible_ && !stroking_ &&
                    !marquee_drag_ && !poly_active_) {
                    // No gesture in flight: Escape closes the modeless
                    // panel (same convention as dialogs).
                    refine_panel_visible_ = false;
                    SyncRefinePanelVisibility();
                    Invalidate();
                    return true;
                }
                if (stroking_ || gradient_drag_ || marquee_drag_ ||
                    poly_active_ || retouch_drag_) {
                    stroking_ = false;
                    stroking_mask_ = false;
                    gradient_drag_ = false;
                    retouch_drag_ = false;
                    if (hist_ != nullptr) {
                        // A retouch macro may be open — closing twice
                        // is safe (EndMacro no-ops on an empty stack).
                        hist_->EndMacro();
                    }
                    CancelSelectionGesture();
                    engine_.reset();
                    scratch_.reset();
                    Invalidate();
                }
                return true;
            }
            if (event.key == KeyCode::Enter) {
                if (poly_active_) {
                    CommitPolyLasso();
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
            // Arrow keys pan the canvas.
            if (!event.is_alt) {
                const float step = 24.0f;
                switch (event.key) {
                    case KeyCode::ArrowLeft:
                        pan_x_ += step;
                        Invalidate();
                        return true;
                    case KeyCode::ArrowRight:
                        pan_x_ -= step;
                        Invalidate();
                        return true;
                    case KeyCode::ArrowUp:
                        pan_y_ += step;
                        Invalidate();
                        return true;
                    case KeyCode::ArrowDown:
                        pan_y_ -= step;
                        Invalidate();
                        return true;
                    default:
                        break;
                }
            }
            return false;
        }

        case Event::Type::TextInput: {
            if (event.is_ctrl || event.is_alt || event.text.size() != 1) {
                return false;
            }
            switch (event.text.front()) {
                case 'b': case 'B': SetTool(Tool::Brush); return true;
                case 'e': case 'E': SetTool(Tool::Eraser); return true;
                case 'f': case 'F': SetTool(Tool::Fill); return true;
                case 'g': case 'G': SetTool(Tool::Gradient); return true;
                case 'h': case 'H': SetTool(Tool::Pan); return true;
                case 'm': case 'M': SetTool(Tool::SelectRect); return true;
                case 'o': case 'O': SetTool(Tool::SelectEllipse); return true;
                case 'l': case 'L': SetTool(Tool::Lasso); return true;
                case 'p': case 'P': SetTool(Tool::PolyLasso); return true;
                case 'w': case 'W': SetTool(Tool::MagicWand); return true;
                case 's': case 'S': SetTool(Tool::CloneStamp); return true;
                case 'j': case 'J': SetTool(Tool::Healing); return true;
                case 'k': case 'K': SetTool(Tool::Spot); return true;
                case 'i': case 'I': SetTool(Tool::Inpaint); return true;
                case '[':
                    brush_params_.diameter_px = std::max(
                        1.0f, brush_params_.diameter_px / 1.2f);
                    if (children_attached_) {
                        size_slider_->SetValue(
                            brush_params_.diameter_px);
                    }
                    Invalidate();
                    return true;
                case ']':
                    brush_params_.diameter_px = std::min(
                        400.0f, brush_params_.diameter_px * 1.2f);
                    if (children_attached_) {
                        size_slider_->SetValue(
                            brush_params_.diameter_px);
                    }
                    Invalidate();
                    return true;
                default:
                    return false;
            }
        }

        default:
            return false;
    }
}

void PhotoWorkspaceWidget::Tick(double dt_seconds) {
    // Marching-ants phase: animate only while an outline is on screen
    // (committed selection or a live marquee) — otherwise Tick stays
    // a no-op so idle frames cost nothing.
    if (selection_nonempty_ || marquee_drag_ || poly_active_) {
        ant_phase_ += dt_seconds;
        Invalidate();
    }
}

// ---------------------------------------------------------------------------
// Headless smoke check ("photo_workspace") — see the header comment.
// ---------------------------------------------------------------------------

std::string PhotoWorkspaceSmoke() {
    namespace ph = Photo;
    namespace W = XER::UI::Widgets;
    using XER::UI::Layout::Point;

    auto any_painted = [](ph::Document& d) {
        for (const std::uint32_t v : d.FlattenU32()) {
            if ((v & 0xFF) > 0) return true;   // alpha byte
        }
        return false;
    };

    auto ws = std::make_shared<PhotoWorkspaceWidget>(nullptr);
    ws->EnsureDocument();
    ph::Document* doc = ws->Doc();
    if (doc == nullptr) return "EnsureDocument produced no document";
    if (doc->CanvasWidth() != 1280 || doc->CanvasHeight() != 720) {
        return "unexpected default canvas size";
    }
    // Minimum-window layout so the viewport + children exist.
    ws->Layout({0.0f, 0.0f, 1100.0f, 700.0f},
               XER::UI::Theme::Tokens{});
    if (ws->HasDocument() == false || doc->Flatten().Width() != 1280) {
        return "layout broke the document";
    }

    const auto send = [&](W::Event::Type type, float x, float y) {
        W::Event ev{};
        ev.type = type;
        ev.position = {x, y};
        if (type == W::Event::Type::MouseDown) {
            ev.mouse_button = W::MouseButton::Left;
        }
        ws->HandleEvent(ev);
    };
    // Viewport center (tool strip 40 + top bar 40; panel ~264 wide).
    const Point vc{438.0f, 370.0f};
    if (any_painted(*doc)) return "fresh document is not transparent";
    send(W::Event::Type::MouseDown, vc.x, vc.y);
    send(W::Event::Type::MouseMove, vc.x + 80.0f, vc.y + 60.0f);
    send(W::Event::Type::MouseMove, vc.x - 40.0f, vc.y + 100.0f);
    send(W::Event::Type::MouseUp, vc.x - 40.0f, vc.y + 100.0f);
    if (!any_painted(*doc)) {
        return "scripted stroke painted no pixels";
    }
    const auto painted = doc->FlattenU32();

    // Ctrl+Z erases the stroke; Ctrl+Y restores it pixel-exact.
    W::Event key{};
    key.type = W::Event::Type::KeyDown;
    key.is_ctrl = true;
    key.key = W::KeyCode::Z;
    ws->HandleEvent(key);
    if (any_painted(*doc)) {
        return "Ctrl+Z did not remove the stroke";
    }
    key.key = W::KeyCode::Y;
    ws->HandleEvent(key);
    if (doc->FlattenU32() != painted) {
        return "Ctrl+Y redo not pixel-exact";
    }

    // Fill tool: 'f' selects it, a canvas click floods the layer.
    W::Event txt{};
    txt.type = W::Event::Type::TextInput;
    txt.text = "f";
    ws->HandleEvent(txt);
    send(W::Event::Type::MouseDown, vc.x, vc.y);
    send(W::Event::Type::MouseUp, vc.x, vc.y);
    {
        const auto flat = doc->FlattenU32();
        bool all_opaque = !flat.empty();
        for (const std::uint32_t v : flat) {
            if ((v & 0xFF) < 255) {
                all_opaque = false;
                break;
            }
        }
        if (!all_opaque) {
            return "fill tool left transparent pixels";
        }
    }

    // Layer ops: add/duplicate/move/merge/flatten/delete through the
    // same public buttons' handlers — all must stay undoable.
    ws->AddLayer(false);
    ws->AddLayer(true);   // group
    ws->DuplicateActiveLayer();
    ws->MoveActiveLayer(-1);
    ws->MoveActiveLayer(1);
    ws->MergeActiveLayer();
    ws->DeleteActiveLayer();
    ws->FlattenImage();
    // Undo back to the filled single-layer doc.
    key.key = W::KeyCode::Z;
    for (int i = 0; i < 8; ++i) ws->HandleEvent(key);
    if (!any_painted(*doc)) {
        return "undo chain lost the fill";
    }

    // ---- M4: selection ops ride the same undo stack ----------------
    ws->SelectAll();
    if (!ws->HasSelection()) {
        return "SelectAll produced no selection";
    }
    ws->Deselect();
    if (ws->HasSelection()) {
        return "Deselect left coverage behind";
    }
    key.key = W::KeyCode::Z;
    ws->HandleEvent(key);
    if (!ws->HasSelection()) {
        return "selection deselect-undo failed";
    }
    key.key = W::KeyCode::Y;
    ws->HandleEvent(key);
    if (ws->HasSelection()) {
        return "selection deselect-redo failed";
    }

    // ---- M4: layer mask + mask-editing stroke ----------------------
    ws->ToggleActiveLayerMask();
    {
        bool any_mask = false;
        for (const auto& c : doc->Root().Children()) {
            any_mask = any_mask || c->HasMask();
        }
        if (!any_mask) {
            return "ToggleActiveLayerMask attached no mask";
        }
    }
    ws->SetMaskEditing(true);
    if (!ws->MaskEditing()) {
        return "SetMaskEditing(true) did not engage";
    }
    // Eraser stroke paints coverage 0 into the mask — the painted
    // region turns transparent in the composite.
    W::Event sw{};
    sw.type = W::Event::Type::TextInput;
    sw.text = "e";
    ws->HandleEvent(sw);
    send(W::Event::Type::MouseDown, vc.x, vc.y);
    send(W::Event::Type::MouseMove, vc.x + 60.0f, vc.y);
    send(W::Event::Type::MouseUp, vc.x + 60.0f, vc.y);
    {
        const auto flat = doc->FlattenU32();
        const std::uint32_t center =
            flat[static_cast<size_t>(360) * 1280 + 640];
        if ((center & 0xFF) >= 32) {
            return "mask stroke did not hide layer pixels";
        }
    }
    key.key = W::KeyCode::Z;
    ws->HandleEvent(key);
    if (!any_painted(*doc)) {
        return "mask stroke undo did not restore coverage";
    }
    ws->SetMaskEditing(false);
    sw.text = "b";   // back to the brush for the checks below
    ws->HandleEvent(sw);

    // ---- M4: adjustment layer + undoable params --------------------
    {
        const auto flat_before = doc->FlattenU32();
        ws->AddAdjustmentLayer("invert");
        const auto flat_adj = doc->FlattenU32();
        if (flat_adj == flat_before) {
            return "adjustment layer produced no pixel change";
        }
        bool has_adj = false;
        for (const auto& c : doc->Root().Children()) {
            has_adj =
                has_adj || c->Kind() == ph::LayerKind::kAdjustment;
        }
        if (!has_adj) {
            return "AddAdjustmentLayer left no adjustment node";
        }
        ph::AdjustmentSpec spec = ph::DefaultAdjustmentSpec("invert");
        if (!ws->SetActiveAdjustment(spec)) {
            return "SetActiveAdjustment failed";
        }
        key.key = W::KeyCode::Z;
        ws->HandleEvent(key);   // undo the param set
        key.key = W::KeyCode::Z;
        ws->HandleEvent(key);   // undo the layer add
        if (doc->FlattenU32() != flat_before) {
            return "adjustment undo did not restore the composite";
        }
    }

    // File round trips: PNG import path (OpenPath) + .xerphoto save.
    {
        const auto tmp = std::filesystem::temp_directory_path();
        const std::string png =
            (tmp / "xer_photo_ws.png").generic_string();
        const std::string xph =
            (tmp / "xer_photo_ws.xerphoto").generic_string();
        std::string err;
        if (!doc->SavePng(png, &err)) {
            return "SavePng failed: " + err;
        }
        if (!ws->OpenPath(png)) {
            return "OpenPath(png) failed";
        }
        if (ws->Doc()->CanvasWidth() != 1280 ||
            ws->Doc()->CanvasHeight() != 720) {
            return "PNG import changed canvas size";
        }
        if (!ph::SaveDocument(*ws->Doc(), xph, &err)) {
            return "SaveDocument failed: " + err;
        }
        if (!ws->OpenPath(xph)) {
            return "OpenPath(.xerphoto) failed";
        }
        // doc_path_ is now bound — Save(false) writes headlessly.
        if (!ws->Save(false)) {
            return "Save(false) on bound .xerphoto failed";
        }
        std::error_code ec;
        std::filesystem::remove(png, ec);
        std::filesystem::remove(xph, ec);
    }
    return {};
}

}  // namespace XER::VideoEditor::Ui
