// Photo workspace — the M3+M4 UI face of the photo editor core
// (tools/video_editor/photo/: Document/LayerNode tree, TiledDevice,
// BrushEngine, PhotoHistory, selections, masks, adjustments,
// .xerphoto). A single self-contained widget the app swaps in for the
// whole content band when the "Photo" workspace preset is applied
// (workspace_layout.cc / VideoEditorAppWidget::ApplyWorkspace):
//
//   [ top bar: file ops | tool options | colors ]          ~40px
//   [ tool strip ] [ canvas viewport ] [ layers panel ]
//
//   * Viewport shows Document::Flatten() over a checkerboard;
//     wheel = zoom-at-cursor, middle-drag or the Hand tool = pan.
//     Full reflatten per edit (M3 contract — dirty-rect later).
//   * Pointer strokes stream through BrushEngine onto a scratch
//     TiledDevice (drawn live as an overlay) and land via
//     BrushEngine::CompositeStroke inside one undoable transaction.
//   * The layers panel drives Document tree ops + SetLayerAttributes
//     through PhotoHistory — every edit (stroke, fill, gradient,
//     visibility, opacity, blend, rename, add/remove/move/merge/
//     flatten/duplicate, mask attach/detach, adjustment params) is
//     undoable with Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y.
//   * File: New (w×h dialog), Open (.xerphoto + png/jpg/... through
//     Document::OpenPng's magic-detect ImageLoader chain),
//     Save .xerphoto, Export PNG.
//
// M4:
//   * Selection tools (rect/ellipse/lasso/poly lasso/magic wand)
//     write an A8 TiledDevice the workspace owns; a marching-ants
//     outline is drawn on the canvas and every paint op clips to the
//     coverage. Shift = add, Alt = subtract on marquee drags;
//     Ctrl+A select all, Ctrl+D deselect, "Inv" button inverts.
//   * The "Mask" layers button attaches a Gray8 mask device; while
//     mask-editing is on, brush strokes paint coverage toward the
//     fg luminance (eraser -> 0) and a quick-mask red tint shows the
//     hidden area.
//   * "+Adj" adds an adjustment layer (named op over the
//     composite-of-below); the op Select + params TextInput edit it
//     through SetLayerAttributesCommand.
//   * Clone stamp (Alt+click = source pick), healing brush, spot
//     remove, and inpaint-over-selection ride image::RetouchTool
//     through undoable patch transactions.
//
// The widget never touches the timeline/project model — it owns its
// own Document + PhotoHistory and reports text through on_status.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "photo/brush.h"
#include "photo/document.h"
#include "photo/photo_history.h"
#include "photo/selection.h"
#include "photo/tile_store.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
class Select;
class Slider;
class TextInput;
}

namespace XER::VideoEditor::Ui {

class PhotoWorkspaceWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Status-strip text (text, is_error) — routed to the app's
        // SetStatus so the shared bottom strip keeps working.
        std::function<void(const std::string&, bool)> on_status;
        // "‹ Video" button — the host switches back to a video
        // workspace preset.
        std::function<void()> on_exit;
    };

    explicit PhotoWorkspaceWidget(void* parent_hwnd, Hooks hooks = {});

    // Lazily creates the default 1280x720 document (first entry into
    // the workspace, or after New/Open fails with no doc left).
    void EnsureDocument();
    bool HasDocument() const { return doc_ != nullptr; }
    bool IsDirty() const { return doc_dirty_; }
    Photo::Document* Doc() { return doc_.get(); }

    // ---- file ops (toolbar buttons + app menus + shortcuts) --------
    void NewDocumentDialog();
    void OpenDialog();
    // Opens .xerphoto via Photo::LoadDocument; anything else goes
    // through Document::OpenPng (ImageLoader magic-detect: png/jpg/
    // webp/tiff/psd/...). Used by the dialog AND the smoke check.
    bool OpenPath(const std::string& utf8_path);
    // Save .xerphoto (choose_path=true forces the Save As dialog).
    bool Save(bool choose_path);
    bool ExportPng();

    // ---- undo (PhotoHistory) ---------------------------------------
    void Undo();
    void Redo();
    bool CanUndo() const;
    bool CanRedo() const;
    std::string UndoLabel() const;
    std::string RedoLabel() const;

    // ---- layer ops (layers panel + Layer menu) ---------------------
    void AddLayer(bool group);
    void DeleteActiveLayer();
    void DuplicateActiveLayer();
    void MoveActiveLayer(int direction);  // +1 = toward top, -1 = down
    void MergeActiveLayer();
    void FlattenImage();

    // ---- M4: selection (Select menu parity) -------------------------
    // The workspace owns an A8 TiledDevice selection (not a document
    // layer); all ops below are undoable via PhotoHistory mementos.
    bool HasSelection() const;
    void SelectAll();
    void Deselect();
    void InvertSelection();
    void FeatherSelectionPx(float radius_px);

    // ---- M4: masks + adjustment layers ------------------------------
    // Attaches a fresh A8 mask to the active layer, or detaches it
    // (undoable either way). While mask-editing is on, brush strokes
    // paint coverage instead of pixels.
    void ToggleActiveLayerMask();
    bool MaskEditing() const { return mask_editing_; }
    void SetMaskEditing(bool on);
    // Adds an adjustment layer with the op's default params
    // (undoable); `op` is an adjust_ops registry name.
    void AddAdjustmentLayer(const std::string& op);
    // Rewrites the active adjustment layer's spec through
    // SetLayerAttributesCommand (undoable "Adjustment Params").
    bool SetActiveAdjustment(const Photo::AdjustmentSpec& spec);

    // Replaces the document (ownership) + resets history/selection;
    // `display_name` is the tab/file label shown in the header.
    void SetDocument(std::unique_ptr<Photo::Document> doc,
                     std::string display_name);
    // Re-flatten + repaint (document mutated outside the widget).
    void Reflatten();

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {520.0f, 300.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {1100.0f, 640.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    enum class Tool {
        Brush, Eraser, Fill, Gradient, Pan,
        SelectRect, SelectEllipse, Lasso, PolyLasso, MagicWand,
        CloneStamp, Healing, Spot, Inpaint,
    };

    // Display-list row for the layers panel: the tree's DFS flattened
    // top-to-bottom (children vectors are bottom-to-top, so rows are
    // emitted in reverse sibling order with a depth indent).
    struct LayerRow {
        Photo::LayerId id = Photo::kInvalidLayerId;
        int depth = 0;
        std::string name;
        bool group = false;
        bool adjustment = false;
        bool has_mask = false;
        bool visible = true;
        bool locked = false;
        // Hit boxes are computed on demand via LayerRowRect() —
        // scroll offset makes caching them inside the row stale-fast.
    };

    void EnsureChildren();
    void SetTool(Tool tool);
    void SetStatus(const std::string& text, bool is_error = false);

    // ---- document plumbing ------------------------------------------
    void OnDocumentEdited();    // reflatten + rows + dirty + controls
    void RebuildLayerRows();    // DFS -> rows_view_
    void SyncLayerControls();   // name/opacity/blend <- active layer
    Photo::LayerNode* ActiveLayer();
    Photo::LayerId FallbackLayerId() const;  // topmost selectable
    void SelectLayer(Photo::LayerId id);
    void ApplyLayerAttributes(Photo::LayerId id,
                              const Photo::SetLayerAttributesCommand::
                                  Attributes& next,
                              const std::string& label);
    // AddLayerCommand needs an already-detached node — Document makes
    // nodes only attached, so create + detach then Execute the command.
    bool AddLayerUndoable(Photo::LayerKind kind, const std::string& name,
                          Photo::LayerId* out_id = nullptr);

    // ---- tools ------------------------------------------------------
    void BeginStroke(const XER::UI::Layout::Point& client);
    void ContinueStroke(const XER::UI::Layout::Point& client);
    void EndStroke();
    void ApplyFillAt(const XER::UI::Layout::Point& client);
    void FinishGradient();
    Photo::BrushParams CurrentBrushParams() const;
    void SetFgColor(Photo::Pixel color);
    bool SetFgColorFromHex(const std::string& text);

    // ---- M4: selection internals --------------------------------------
    void EnsureSelectionDevice();
    // The active selection device for paint-op clipping (nullptr when
    // the selection is empty).
    const Photo::TiledDevice* ClipMask() const;
    // Runs `fn` on the selection device inside one undoable
    // ExecuteDeviceOp, then refreshes outline/state.
    void RunSelectionOp(
        const std::string& label,
        const std::function<void(Photo::TiledDevice&)>& fn);
    // Recomputes has-selection + the marching-ants outline.
    void RefreshSelectionState();
    Photo::SelectionCombine PendingCombine() const;
    // Commits the in-progress marquee drag (rect/ellipse/lasso).
    void CommitMarquee();
    // The doc-space polygon to draw as the ants outline for the
    // in-progress marquee (empty = use sel_outline_).
    std::vector<Photo::IntPoint> LiveMarqueePolygon() const;
    void CommitPolyLasso();
    void CancelSelectionGesture();

    // ---- M4: retouch --------------------------------------------------
    int RetouchRadius() const;
    void RetouchDab(const XER::UI::Layout::Point& doc);
    void ApplyInpaintOverSelection();

    // ---- M4: mask internals --------------------------------------------
    // Quick-mask overlay (red tint where coverage < 255), rebuilt
    // after every reflatten while mask-editing is on.
    void RebuildMaskOverlay();
    // Coverage value a mask stroke paints: fg-luma for the brush,
    // 0 (black erase) for the eraser — the PS "paint gray" model.
    uint8_t MaskPaintValue() const;

    // ---- M4: adjustment controls ---------------------------------------
    void SyncAdjustmentControls();
    bool adj_controls_visible_ = false;

    // ---- view transform (zoom-at-cursor + pan, photo_viewer pattern)
    float FitScale() const;
    float Scale() const;
    XER::UI::Layout::Rect CanvasDest() const;
    XER::UI::Layout::Point ClientToDoc(
        const XER::UI::Layout::Point& p) const;
    void ZoomAt(XER::UI::Layout::Point center, float factor);
    void ResetView();

    // ---- checkerboard ------------------------------------------------
    // BGRA checker image covering `rect` (viewport-clipped canvas
    // dest), anchored to DOCUMENT space: `origin_x/y` is the doc-space
    // offset of the rect's top-left so panning moves the pattern with
    // the canvas. Regenerated only when size/phase changes.
    void EnsureChecker(int w, int h, int origin_x, int origin_y);

    // ---- layers-panel helpers ----------------------------------------
    int LayerRowAt(const XER::UI::Layout::Point& p) const;
    XER::UI::Layout::Rect LayerRowRect(int row) const;
    float LayersViewTop() const;
    float LayersViewBottom() const;
    void ToggleRowVisible(int row);

    Hooks hooks_;
    void* parent_hwnd_ = nullptr;
    bool children_attached_ = false;

    // ---- document state ----------------------------------------------
    std::unique_ptr<Photo::Document> doc_;
    // PhotoHistory binds a Document& — always rebuilt together with
    // doc_ (see SetDocument / EnsureDocument).
    std::unique_ptr<Photo::PhotoHistory> hist_;
    Photo::LayerId active_layer_ = Photo::kInvalidLayerId;
    std::string doc_path_;    // .xerphoto path once saved/loaded
    std::string doc_name_;    // header label ("" = "Untitled")
    bool doc_dirty_ = false;

    // ---- tools --------------------------------------------------------
    Tool tool_ = Tool::Brush;
    Photo::BrushParams brush_params_;
    bool suppress_option_sync_ = false;

    // ---- viewport -----------------------------------------------------
    XER::UI::Layout::Rect viewport_{};
    float zoom_ = 1.0f;      // multiplier on fit scale
    float pan_x_ = 0.0f;     // canvas dest top-left, widget coords
    float pan_y_ = 0.0f;
    bool need_fit_ = true;   // fit-to-view on next layout/render
    bool panning_ = false;
    XER::UI::Layout::Point pan_last_{};
    // Cursor tracking for the brush-size ring (viewport coords).
    bool cursor_in_vp_ = false;
    XER::UI::Layout::Point cursor_pos_{};
    std::vector<uint8_t> doc_bgra_;     // Flatten() -> BGRA upload
    int doc_bgra_w_ = 0, doc_bgra_h_ = 0;
    // Checkerboard under transparency (see EnsureChecker).
    std::vector<uint8_t> checker_bgra_;
    int checker_w_ = -1, checker_h_ = -1;
    int checker_ox_ = 0, checker_oy_ = 0;

    // ---- stroke state --------------------------------------------------
    std::unique_ptr<Photo::TiledDevice> scratch_;   // dab target
    std::unique_ptr<Photo::BrushEngine> engine_;    // per-stroke
    bool stroking_ = false;
    bool stroking_mask_ = false;   // stroke targets the layer's A8 mask
    bool gradient_drag_ = false;
    XER::UI::Layout::Point grad_start_{};   // client coords
    XER::UI::Layout::Point grad_cur_{};

    // ---- selection (workspace-owned A8 device) --------------------------
    std::unique_ptr<Photo::TiledDevice> selection_;
    bool selection_nonempty_ = false;             // cached coverage>0
    std::vector<Photo::IntPoint> sel_outline_;    // doc-px ants poly
    double ant_phase_ = 0.0;                      // marching-ants phase
    // In-progress marquee (client coords for rect/ellipse; doc points
    // for lasso/poly).
    bool marquee_drag_ = false;
    Photo::SelectionCombine marq_combine_ =
        Photo::SelectionCombine::kReplace;
    XER::UI::Layout::Point marq_anchor_{};
    XER::UI::Layout::Point marq_cur_{};
    std::vector<Photo::IntPoint> lasso_pts_;
    bool poly_active_ = false;
    std::vector<Photo::IntPoint> poly_pts_;
    float wand_tol_ = 32.0f;      // magic-wand tolerance (0..255)
    float sel_feather_ = 0.0f;    // marquee feather option (px)

    // ---- retouch ---------------------------------------------------------
    bool retouch_drag_ = false;
    bool clone_src_set_ = false;
    Photo::IntPoint clone_src_;       // doc px, Alt+click pick
    Photo::IntPoint retouch_origin_;  // doc px where the drag began

    // ---- mask editing -----------------------------------------------------
    bool mask_editing_ = false;
    // Quick-mask tint overlay (canvas-sized BGRA; regenerated on
    // reflatten while mask-editing).
    std::vector<std::uint8_t> mask_overlay_bgra_;
    int mask_ovl_w_ = 0, mask_ovl_h_ = 0;

    // ---- colors ---------------------------------------------------------
    // Foreground color = brush_params_.color. `swatch_rects_` is
    // index-aligned with the static palette in the .cc; Layout fills.
    std::vector<XER::UI::Layout::Rect> swatch_rects_;
    XER::UI::Layout::Rect fg_rect_{};

    // ---- layers panel ----------------------------------------------------
    std::vector<LayerRow> rows_view_;     // top-to-bottom display list
    int row_hover_ = -1;
    float rows_scroll_ = 0.0f;
    static constexpr float kRowH = 26.0f;

    // ---- children ---------------------------------------------------------
    std::shared_ptr<XER::UI::Widgets::Button> exit_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> new_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> open_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> save_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> export_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> undo_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> redo_btn_;
    // Tool strip (left column).
    std::shared_ptr<XER::UI::Widgets::Button> brush_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> eraser_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> fill_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> gradient_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> pan_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> sel_rect_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> sel_ellipse_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> lasso_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> poly_lasso_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> wand_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> clone_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> heal_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> spot_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> inpaint_btn_;
    // Tool options (top bar, right side).
    std::shared_ptr<XER::UI::Widgets::Label> size_label_;
    std::shared_ptr<XER::UI::Widgets::Slider> size_slider_;
    std::shared_ptr<XER::UI::Widgets::Label> hardness_label_;
    std::shared_ptr<XER::UI::Widgets::Slider> hardness_slider_;
    std::shared_ptr<XER::UI::Widgets::Label> opacity_label_;
    std::shared_ptr<XER::UI::Widgets::Slider> opacity_slider_;
    std::shared_ptr<XER::UI::Widgets::Label> tol_label_;
    std::shared_ptr<XER::UI::Widgets::Slider> tol_slider_;
    std::shared_ptr<XER::UI::Widgets::Label> feather_label_;
    std::shared_ptr<XER::UI::Widgets::Slider> feather_slider_;
    std::shared_ptr<XER::UI::Widgets::Button> invert_sel_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> deselect_btn_;
    std::shared_ptr<XER::UI::Widgets::TextInput> color_input_;
    std::shared_ptr<XER::UI::Widgets::Label> zoom_label_;
    // Layers panel controls (right column).
    std::shared_ptr<XER::UI::Widgets::Button> add_layer_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> add_group_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> add_adj_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> dup_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> del_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> up_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> down_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> merge_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> flatten_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> mask_btn_;
    std::shared_ptr<XER::UI::Widgets::TextInput> name_input_;
    std::shared_ptr<XER::UI::Widgets::Slider> layer_opacity_slider_;
    std::shared_ptr<XER::UI::Widgets::Select> blend_select_;
    std::shared_ptr<XER::UI::Widgets::Select> adj_op_select_;
    std::shared_ptr<XER::UI::Widgets::TextInput> adj_params_input_;
    std::shared_ptr<XER::UI::Widgets::Label> hint_label_;
};

// Headless smoke entry point — registered from main.cc as the
// "photo_workspace" check (the suite lives in the UI-free core, so it
// can't see this class). Constructs the widget, lays it out at the
// app's minimum window size, scripts a brush stroke through synthetic
// Events, and asserts on flatten/undo/redo pixels plus file round
// trips. Empty string = OK, otherwise a one-line reason.
std::string PhotoWorkspaceSmoke();

}  // namespace XER::VideoEditor::Ui
