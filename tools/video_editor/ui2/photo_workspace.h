// ui2 photo workspace — the photo editor's face inside the ui2
// shell, rebuilt on the shared widget kit (the legacy
// ui/photo_workspace.* monolith was deleted at the ui2 swap-over;
// this is a fresh composed design, not a port):
//
//   ┌──────────────────────────────────────────────────────────┐
//   │ top bar: New Open Save▾ Undo Redo | tool opts | fg | zoom │
//   ├──────┬───────────────────────────────┬───────────────────┤
//   │ tools│ PhotoCanvasSurface            │ Tabs: Layers |    │
//   │ col. │ (kit Surface child)           │       Adjust      │
//   └──────┴───────────────────────────────┴───────────────────┘
//
// Composition and ownership follow the surface_hooks.h contract:
//   * The workspace (the "shell" here) owns the photo::Document, the
//     PhotoHistory and the workspace selection device; every model
//     edit funnels through PhotoHistory so strokes, marquees, layer
//     structure and attribute edits all undo with Ctrl+Z/Y.
//   * PhotoCanvasSurface owns VIEW state only (zoom/pan, cursors,
//     overlay caches) and receives doc-state pushes: the flattened
//     BGRA composite, selection/mask coverage, and live-gesture
//     previews (stroke scratch, marquee polygon, gradient line,
//     crop/move frames, clone source cross).
//   * The canvas emits doc-space pointer gestures back through
//     PhotoCanvasHooks; the workspace interprets them per tool
//     (BrushEngine strokes, marquee selection ops, retouch dabs,
//     fill/gradient, eyedropper, crop/move commits).
//
// VeApp swaps this widget into the center of the top split when the
// "Photo" workspace preset is applied (see workspace_layout.cc) —
// the same slot the monitor pair otherwise fills. File actions also
// hang off the shell File menu.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "photo/brush.h"
#include "photo/document.h"
#include "photo/photo_history.h"
#include "photo/selection.h"
#include "photo/tile_store.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Card;
class ColorPicker;
class Dialog;
class Eyedropper;
class Label;
class PieMenu;
class ScrollArea;
class ScrubNumberField;
class Select;
class Surface;
class TextInput;
class TreeView;
class Widget;
}  // namespace XER::UI::Widgets

namespace XER::VideoEditor::Ui2 {

class DockTabGroup;
class LinearBox;
class PhotoWorkspace;

// ── PhotoCanvasHooks ─────────────────────────────────────────────────
// Doc-space pointer gesture stream (the photo analogue of
// MonitorHooks): the surface maps client events through its
// zoom/pan transform and reports document-pixel points plus the raw
// modifier flags; the workspace decides what each tool does with
// them. All coordinates are float doc pixels.
struct PhotoCanvasHooks {
    // Left-button gestures inside the canvas area. `shift`/`alt`
    // carry the modifier flags at press time; marquee tools resolve
    // combine modes from them (shift=add, alt=subtract).
    std::function<void(float x, float y, bool shift, bool alt)>
        on_canvas_down;
    std::function<void(float x, float y)> on_canvas_drag;
    std::function<void(float x, float y)> on_canvas_up;
    // Right-button press at a client point — the workspace opens the
    // tool PieMenu there (marking-menu tool switching).
    std::function<void(XER::UI::Layout::Point)> on_right_down;
    // First-press activation (focus/last-active routing) and status
    // lines for the strip.
    std::function<void()> on_activate;
    std::function<void(const std::string&, bool)> on_status;
    // Unhandled surface events (keys/text/focus — the kit Surface
    // keeps focus after canvas presses, and the widget tree routes
    // key events to the focused widget only). The workspace wires
    // this to its own HandleEvent so shortcuts keep working while
    // the canvas is focused.
    std::function<bool(const XER::UI::Widgets::Event&)> on_event;
};

// ── PhotoCanvasSurface ───────────────────────────────────────────────
// Letterboxed photo-document canvas on a kit Surface: checkerboard
// under transparency, zoom-at-cursor/pan view state, quick-mask tint
// + traced contour over an A8 coverage push, marching-ants selection
// outlines and the live tool previews the workspace pushes per
// gesture. Paints only inside the canvas rect; no document state is
// held — everything arrives as buffers/points.
class PhotoCanvasSurface final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<PhotoCanvasSurface> Create();
    ~PhotoCanvasSurface() override;

    // ---- hooks ----
    void SetHooks(PhotoCanvasHooks hooks);
    const PhotoCanvasHooks& Hooks() const { return hooks_; }

    // ---- document-state pushes ----
    // Flattened composite, packed BGRA8 (copied). 0x0 clears.
    void SetComposite(std::vector<std::uint8_t> bgra, int width,
                      int height);
    bool HasComposite() const { return composite_w_ > 0; }
    // Status line painted in place of the canvas when no composite is
    // available ("No document — …").
    void SetStatusText(std::string text);
    // Committed selection outline (doc px) drawn as marching ants;
    // empty clears. Live-marquee previews use SetLiveOutline.
    void SetSelectionOutline(std::vector<Photo::IntPoint> doc_points);
    // In-progress marquee polygon (doc px) — takes precedence over
    // the committed outline while non-empty.
    void SetLiveOutline(std::vector<Photo::IntPoint> doc_points);
    // A8 coverage overlay (w*h bytes) drawn as a tint + traced
    // boundary — the monitor-surface mask pattern. The photo
    // workspace pushes quick-mask (255-coverage → red tint) and may
    // recolor via SetMaskTint. nullptr clears.
    void SetMaskOverlay(
        std::shared_ptr<const std::vector<std::uint8_t>> coverage,
        int width, int height);
    void SetMaskTint(XER::UI::Theme::Color color);
    // Live stroke preview: the scratch dab buffer's painted extent in
    // doc pixels (BGRA), drawn srcOver on top of the composite.
    // `opacity` < 1 previews erase zones. Empty clears.
    void SetStrokePreview(std::vector<std::uint8_t> bgra,
                          Photo::IntRect doc_extent, float opacity);
    // Gradient drag preview line (doc px endpoints; disabled when
    // `on` is false).
    void SetGradientLine(bool on, float ax, float ay, float bx,
                         float by);
    // Clone/heal source cross (doc px; `on` = source picked).
    void SetSourceMarker(bool on, Photo::IntPoint doc_point);
    // Brush cursor ring: doc-space diameter while a paint-like tool
    // hovers the canvas (0 disables).
    void SetBrushRing(float doc_diameter);
    // Move-tool preview: translated extent frame of the dragged
    // layer (doc rect + doc-px offset; `on` false clears).
    void SetMovePreview(bool on, Photo::IntRect layer_rect,
                        float dx, float dy);
    // Crop-tool overlay: dim everything outside the doc-px rect;
    // `on` false clears.
    void SetCropPreview(bool on, Photo::IntRect doc_rect);

    // ---- view state ----
    // Zoom multiplier over the letterbox fit (1.0 = fit), clamped to
    // [kMinZoom, kMaxZoom]. SetZoom(<=0) resets to fit.
    void SetZoom(float zoom);
    float Zoom() const { return zoom_; }
    void ZoomFit() { SetZoom(1.0f); }
    void SetPanMode(bool on);   // left-drag pans (hand tool)
    bool PanMode() const { return pan_mode_; }
    void SetOnZoomChanged(std::function<void(float)> cb);

    // ---- geometry (pure functions of bounds + view state) ----
    // The rect the composite paints into inside the canvas area
    // (letterboxed, zoomed, panned). Exported for hit mapping.
    XER::UI::Layout::Rect CanvasDest() const;
    float FitScale() const;
    float Scale() const;
    // Client point -> document pixels (may lie outside the canvas).
    XER::UI::Layout::Point ClientToDoc(
        const XER::UI::Layout::Point& p) const;
    XER::UI::Layout::Point DocToClient(float x, float y) const;
    static constexpr float kMinZoom = 0.05f;
    static constexpr float kMaxZoom = 40.0f;
    XER::UI::Widgets::Surface* KitSurface() const {
        return surface_.get();
    }

    // ---- widget contract ----
    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {240.0f, 160.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {720.0f, 480.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    PhotoCanvasSurface();

    void Paint(XER::UI::Rendering::Renderer& renderer,
               const XER::UI::Layout::Rect& content,
               const XER::UI::Theme::Tokens& tokens);
    bool OnSurfaceEvent(const XER::UI::Widgets::Event& event);
    void EnsureChecker(int w, int h, int origin_x, int origin_y);
    void EnsureMaskOverlayBuilt(XER::UI::Theme::Color tint);
    void ZoomAt(XER::UI::Layout::Point center, float factor);
    void EmitStatus(const std::string& text, bool is_error = false);

    PhotoCanvasHooks hooks_;
    std::shared_ptr<XER::UI::Widgets::Surface> surface_;
    std::function<void(float)> on_zoom_changed_;

    // Composite (pushed BGRA).
    std::vector<std::uint8_t> composite_;
    int composite_w_ = 0, composite_h_ = 0;
    std::string status_text_ = "No document — New (Ctrl+N) or "
                               "Open (Ctrl+O)";

    // Overlays (pushed).
    std::vector<Photo::IntPoint> sel_outline_;
    std::vector<Photo::IntPoint> live_outline_;
    std::shared_ptr<const std::vector<std::uint8_t>> mask_cov_;
    int mask_w_ = 0, mask_h_ = 0;
    XER::UI::Theme::Color mask_tint_{1.0f, 0.18f, 0.15f, 0.42f};
    std::vector<std::uint8_t> mask_bgra_;      // premultiplied tint
    std::vector<Photo::IntPoint> mask_contour_;
    const void* mask_built_for_ = nullptr;
    float built_tint_[4] = {};
    std::vector<std::uint8_t> stroke_bgra_;
    Photo::IntRect stroke_ext_{};
    float stroke_opacity_ = 1.0f;
    bool grad_line_on_ = false;
    float grad_ax_ = 0.0f, grad_ay_ = 0.0f, grad_bx_ = 0.0f,
          grad_by_ = 0.0f;
    bool src_marker_on_ = false;
    Photo::IntPoint src_marker_{};
    float brush_ring_diameter_ = 0.0f;
    bool move_preview_on_ = false;
    Photo::IntRect move_rect_{};
    float move_dx_ = 0.0f, move_dy_ = 0.0f;
    bool crop_on_ = false;
    Photo::IntRect crop_rect_{};

    // View state.
    float zoom_ = 1.0f;
    float pan_x_ = 0.0f, pan_y_ = 0.0f;   // canvas dest top-left
    bool need_fit_ = true;
    bool pan_mode_ = false;
    bool panning_ = false;
    XER::UI::Layout::Point pan_last_{};
    bool gesture_down_ = false;
    bool cursor_in_ = false;
    XER::UI::Layout::Point cursor_pos_{};
    double ant_phase_ = 0.0;

    // Checkerboard cache (document-anchored, see EnsureChecker).
    std::vector<std::uint8_t> checker_bgra_;
    int checker_w_ = -1, checker_h_ = -1;
    int checker_ox_ = 0, checker_oy_ = 0;
};

// ── PhotoWorkspace ───────────────────────────────────────────────────
// The full photo-editing band the shell swaps in: owns Document +
// PhotoHistory + the workspace selection device, drives the canvas
// surface and the Layers/Adjust panels, and funnels every edit
// through the history stack. File ops (New/Open/Save/Save As/Export
// PNG) are exposed both as top-bar buttons and public methods for
// the shell File menu.
class PhotoWorkspace final : public XER::UI::Widgets::Widget {
public:
    enum class Tool {
        Move,            // drag = move layer pixels (commit on release)
        Pan,             // hand — left-drag pans the view
        SelectRect, SelectEllipse, Lasso, PolyLasso, MagicWand,
        Brush, Eraser, Fill, Gradient,
        CloneStamp, Healing, Spot, Inpaint,
        Crop, Eyedropper,
    };

    struct Hooks {
        // Status-strip text (text, is_error) routed to the shell's
        // status line.
        std::function<void(const std::string&, bool)> on_status;
    };

    static std::shared_ptr<PhotoWorkspace> Create(Hooks hooks = {});
    ~PhotoWorkspace() override;

    // Lazily creates the default 1280x720 document (first entry or
    // after New/Open fails with nothing left).
    void EnsureDocument();
    bool HasDocument() const { return doc_ != nullptr; }
    bool IsDirty() const { return doc_dirty_; }
    Photo::Document* Doc() { return doc_.get(); }
    const Photo::Document* Doc() const { return doc_.get(); }
    const std::string& DocumentPath() const { return doc_path_; }
    const std::string& DocumentName() const { return doc_name_; }

    // ---- file ops ----
    // New / open / save / export. The *Dialog variants pop the native
    // pickers (Windows only — headless runs go through OpenPath/Save
    // directly). `choose_path` on Save forces Save-As.
    void NewDocument(int width, int height);
    void NewDocumentDialog();
    void OpenDialog();
    bool OpenPath(const std::string& utf8_path);
    bool Save(bool choose_path);
    bool ExportPng();

    // ---- undo ----
    void Undo();
    void Redo();
    bool CanUndo() const;
    bool CanRedo() const;
    std::string UndoLabel() const;
    std::string RedoLabel() const;

    // ---- tools ----
    void SetTool(Tool tool);
    Tool CurrentTool() const { return tool_; }
    // Strip contract (smoke-checked): icon-only buttons with a
    // tooltip, no visible text — counts the compliant ones.
    int IconOnlyToolButtonCount() const;
    void SetForegroundColor(Photo::Pixel color);
    Photo::Pixel ForegroundColor() const { return brush_params_.color; }

    // ---- layers (panel + model ops; all undoable) ----
    void AddLayer(bool group);
    void DeleteActiveLayer();
    void DuplicateActiveLayer();
    void MoveActiveLayer(int direction);   // +1 toward top, -1 down
    void MergeActiveLayer();
    void FlattenImage();
    void ToggleActiveLayerMask();
    bool MaskEditing() const { return mask_editing_; }
    void SetMaskEditing(bool on);
    void AddAdjustmentLayer(const std::string& op);
    bool SetActiveAdjustment(const Photo::AdjustmentSpec& spec);

    // ---- selection (workspace-owned A8 device; undoable) ----
    bool HasSelection() const { return selection_nonempty_; }
    void SelectAll();
    void Deselect();
    void InvertSelection();
    void FeatherSelectionPx(float radius_px);
    void ApplyRefineEdge(const Photo::RefineEdgeParams& params);

    // Re-flatten + repaint after out-of-band document mutation.
    void Reflatten();

    // ---- panels/surfaces (headless-discovery seam) ----
    PhotoCanvasSurface* Canvas() const { return canvas_.get(); }
    XER::UI::Widgets::TreeView* LayersTree() const {
        return layers_tree_.get();
    }
    Photo::LayerId ActiveLayerId() const { return active_layer_; }
    Photo::PhotoHistory* History() { return hist_.get(); }

    // ---- widget contract ----
    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    void Layout(const XER::UI::Layout::Rect& bounds,
                const XER::UI::Theme::Tokens& tokens) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {640.0f, 360.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {1100.0f, 640.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit PhotoWorkspace(Hooks hooks);

    // ---- structure ----
    void BuildChildren();
    void SetStatus(const std::string& text, bool is_error = false);
    void SyncTopBar();        // tool options + zoom readout + undo/redo
    void SyncToolButtons();   // checkable tool group state

    // ---- document plumbing ----
    void OnDocumentEdited();           // reflatten + rows + dirty
    void OnDocumentReplaced();         // doc swap (new/open/crop)
    void RefreshCanvasComposite();
    Photo::LayerNode* ActiveLayer();
    Photo::LayerId FallbackLayerId() const;
    void SelectLayer(Photo::LayerId id);
    void ApplyLayerAttributes(
        Photo::LayerId id,
        const Photo::SetLayerAttributesCommand::Attributes& next,
        const std::string& label);
    bool AddLayerUndoable(Photo::LayerKind kind,
                          const std::string& name,
                          Photo::LayerId* out_id = nullptr);
    void SetDocument(std::unique_ptr<Photo::Document> doc,
                     std::string display_name);

    // ---- layers panel ----
    void RebuildLayerTree();           // DFS -> TreeView items
    void SyncLayerControls();          // name/eye/lock/opacity/blend
    // Flattened (visible) rows matching the TreeView's DFS order —
    // the row overlay duplicates this walk for hit zones.
    void FlattenRows(
        std::vector<Photo::LayerId>* out) const;

    // ---- adjustments panel ----
    void RebuildAdjustmentCards();
    void SyncAdjustmentBinding();      // card fields <- active spec
    // Live-preview mutation without a history record (restored on
    // commit through the gesture-start spec so undo stays correct).
    // `op_index` is the card's registry index — edits on a card that
    // isn't bound to the active adjustment are ignored.
    void ApplyAdjustmentLive(int op_index, int param_index,
                             double value);
    void CommitAdjustmentParam(int op_index, int param_index);

    // ---- tools ----
    Photo::BrushParams CurrentBrushParams() const;
    // Stroke points arrive already in document pixels (the canvas
    // hooks map client -> doc).
    void BeginStroke(const XER::UI::Layout::Point& doc);
    void ContinueStroke(const XER::UI::Layout::Point& doc);
    void EndStroke();
    // Push the scratch extent's BGRA to the canvas preview slot.
    void PushStrokePreview();
    void ApplyFillAt(const XER::UI::Layout::Point& client);
    void FinishGradient();
    int RetouchRadius() const;
    void RetouchDab(const XER::UI::Layout::Point& doc);
    void ApplyInpaintOverSelection();

    // ---- selection internals ----
    void EnsureSelectionDevice();
    const Photo::TiledDevice* ClipMask() const;
    void RunSelectionOp(
        const std::string& label,
        const std::function<void(Photo::TiledDevice&)>& fn);
    void RefreshSelectionState();
    Photo::SelectionCombine PendingCombine() const;
    void CommitMarquee();
    std::vector<Photo::IntPoint> LiveMarqueePolygon() const;
    void CommitPolyLasso();
    void CancelSelectionGesture();

    // ---- mask internals ----
    void RebuildMaskOverlay();
    uint8_t MaskPaintValue() const;

    // ---- canvas gesture handlers (PhotoCanvasHooks impl) ----
    void OnCanvasDown(float dx, float dy, bool shift, bool alt);
    void OnCanvasDrag(float dx, float dy);
    void OnCanvasUp(float dx, float dy);
    void OnCanvasRightDown(XER::UI::Layout::Point client);

    // ---- crop ----
    void CommitCrop();   // rebuilds the document at crop_rect_

    Hooks hooks_;

    // ---- document state ----
    std::unique_ptr<Photo::Document> doc_;
    std::unique_ptr<Photo::PhotoHistory> hist_;
    Photo::LayerId active_layer_ = Photo::kInvalidLayerId;
    std::string doc_path_;
    std::string doc_name_;
    bool doc_dirty_ = false;

    // ---- tools ----
    Tool tool_ = Tool::Brush;
    Photo::BrushParams brush_params_;
    float wand_tol_ = 32.0f;
    float sel_feather_ = 0.0f;

    // ---- stroke state ----
    std::unique_ptr<Photo::TiledDevice> scratch_;
    std::unique_ptr<Photo::BrushEngine> engine_;
    bool stroking_ = false;
    bool stroking_mask_ = false;
    bool gradient_drag_ = false;
    float grad_ax_ = 0.0f, grad_ay_ = 0.0f, grad_bx_ = 0.0f,
          grad_by_ = 0.0f;   // doc px

    // ---- selection ----
    std::unique_ptr<Photo::TiledDevice> selection_;
    bool selection_nonempty_ = false;
    std::vector<Photo::IntPoint> sel_outline_;
    bool marquee_drag_ = false;
    Photo::SelectionCombine marq_combine_ =
        Photo::SelectionCombine::kReplace;
    float marq_ax_ = 0.0f, marq_ay_ = 0.0f, marq_bx_ = 0.0f,
          marq_by_ = 0.0f;   // doc px
    std::vector<Photo::IntPoint> lasso_pts_;
    bool poly_active_ = false;
    std::vector<Photo::IntPoint> poly_pts_;
    Photo::RefineEdgeParams refine_params_;

    // ---- retouch / move / crop ----
    bool retouch_drag_ = false;
    bool clone_src_set_ = false;
    Photo::IntPoint clone_src_;
    Photo::IntPoint retouch_origin_;
    bool move_drag_ = false;
    float move_ax_ = 0.0f, move_ay_ = 0.0f;   // drag anchor (doc px)
    float move_dx_ = 0.0f, move_dy_ = 0.0f;   // running delta (doc px)
    Photo::IntRect move_extent_{};            // dragged layer's extent
    bool crop_drag_ = false;
    Photo::IntRect crop_rect_{};

    // ---- mask editing ----
    bool mask_editing_ = false;

    // ---- adjustment live-gesture spec ----
    // Captured on the first live scrub change so the commit path can
    // restore it before pushing the undoable command (the command's
    // prev must be the pre-gesture spec).
    std::unique_ptr<Photo::AdjustmentSpec> pre_gesture_spec_;

    // ---- children ----
    std::shared_ptr<LinearBox> top_bar_;
    std::shared_ptr<XER::UI::Widgets::Button> new_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> open_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> save_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> undo_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> redo_btn_;
    // Contextual tool options (fixed strip; visibility follows tool).
    std::shared_ptr<XER::UI::Widgets::Label> size_label_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> size_field_;
    std::shared_ptr<XER::UI::Widgets::Label> hard_label_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> hard_field_;
    std::shared_ptr<XER::UI::Widgets::Label> opacity_label_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> opacity_field_;
    std::shared_ptr<XER::UI::Widgets::Label> tol_label_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> tol_field_;
    std::shared_ptr<XER::UI::Widgets::Label> feather_label_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField> feather_field_;
    std::shared_ptr<XER::UI::Widgets::Button> invert_sel_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> deselect_btn_;
    // Foreground-color chip — a file-local ColorChip widget, stored
    // generically (dynamic_cast at the pick sites).
    std::shared_ptr<XER::UI::Widgets::Widget> fg_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> zoom_label_;

    // Tools column: icon-only checkable Button group (index-aligned
    // with kToolInfos in the .cc) inside a narrow ScrollArea so short
    // windows scroll rather than clip the strip.
    std::shared_ptr<LinearBox> tools_col_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> tools_scroll_;
    std::vector<std::shared_ptr<XER::UI::Widgets::Button>> tool_btns_;

    std::shared_ptr<PhotoCanvasSurface> canvas_;

    // Right dock: a DockTabGroup (kit-style tab strip + one visible
    // panel) holding the Layers and Adjust tabs.
    std::shared_ptr<DockTabGroup> right_dock_;
    // Layers tab: a file-local host that stacks [toolbar | tree +
    // row-chrome overlay | selected-layer strip]. Stored generically;
    // all interactive children stay reachable through the typed
    // members below.
    std::shared_ptr<XER::UI::Widgets::Widget> layers_panel_;
    // Adjust tab: cards inside a ScrollArea (adjust_panel_ is the
    // scrolled column).
    std::shared_ptr<XER::UI::Widgets::ScrollArea> adjust_scroll_;
    std::shared_ptr<LinearBox> adjust_panel_;
    // Row-chrome overlay over the layer tree (file-local widget):
    // paints per-row eye/lock/mask-badge affordances and routes
    // clicks on their hit zones.
    std::shared_ptr<XER::UI::Widgets::Widget> layer_chrome_;

    // Layers panel widgets.
    std::shared_ptr<XER::UI::Widgets::TreeView> layers_tree_;
    std::shared_ptr<XER::UI::Widgets::Button> add_layer_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> dup_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> del_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> up_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> down_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> merge_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> flatten_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> mask_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> eye_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> lock_btn_;
    std::shared_ptr<XER::UI::Widgets::TextInput> name_input_;
    std::shared_ptr<XER::UI::Widgets::ScrubNumberField>
        layer_opacity_field_;
    std::shared_ptr<XER::UI::Widgets::Select> blend_select_;

    // Adjustments panel: one collapsible Card per registry op, each
    // card holding a LinearBox of param rows; row fields are kept
    // flat, index-aligned across all ops.
    std::shared_ptr<XER::UI::Widgets::Button> add_adj_btn_;
    std::vector<std::shared_ptr<XER::UI::Widgets::Card>> adj_cards_;
    std::vector<std::vector<
        std::shared_ptr<XER::UI::Widgets::ScrubNumberField>>>
        adj_fields_;
    int adj_bound_op_ = -1;     // registry index bound to fields
    bool suppress_adj_sync_ = false;
    float gesture_opacity_ = -1.0f;  // opacity scrub gesture capture

    // Overlay helpers.
    std::shared_ptr<XER::UI::Widgets::Eyedropper> eyedropper_;
    std::shared_ptr<XER::UI::Widgets::PieMenu> pie_menu_;
    std::shared_ptr<XER::UI::Widgets::ColorPicker> color_picker_;
    std::shared_ptr<XER::UI::Widgets::Dialog> dialog_;
};

// Headless smoke entry — registered from main.cc as
// "photo_workspace". Instantiates the workspace through a real
// WidgetTree, scripts canvas paint, layer ops, a rect selection, an
// adjustment commit and undo/redo; returns "" on success.
std::string Ui2PhotoWorkspaceSmoke();

}  // namespace XER::VideoEditor::Ui2
