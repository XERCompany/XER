// Object Lab control panel (Phase 2): Photoshop-style object ops on
// the monitor frame — cut out, remove (inpaint), copy to layer.
// The panel owns an Lab::AsyncOp worker; the app's on_run hook runs on
// that worker thread, supplies the current frame, calls the ops in
// lab/object_ops.h and places the result (e.g. as a new clip layer).
// Progress/cancel flow through Lab::JobControl.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lab/async_op.h"
#include "lab/object_ops.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class ButtonGroup;
class Field;
class Label;
class Progress;
class Separator;
class Slider;
class TextInput;
}

namespace XER::VideoEditor::Ui {

class LabPanel final : public XER::UI::Widgets::Widget {
public:
    enum class Mode {
        Cutout,       // CutOutRect / CutOutLasso -> new frame
        Remove,       // InpaintRect / InpaintMask on the region
        CopyToLayer,  // CopyRegionToLayer -> masked layer payload
        RemoveBackground,  // BiRefNet alpha matte -> RGBA cutout layer
        SmartSelect,  // EfficientSAM click points -> stored AI mask
    };

    struct Region {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };

    struct RunParams {
        Mode mode = Mode::Cutout;
        Region region;
        int feather_px = 0;
        int search_radius = 24;              // inpaint only
        std::vector<Lab::IntPoint> polygon;  // lasso; empty => rect
        // SmartSelect prompt: positive click points in frame pixels,
        // collected on the monitor while SmartSelect is the active
        // mode (LabPanel::AddPoint).
        std::vector<Lab::IntPoint> points;
        // Stored AI mask (w*h bytes, frame-sized) produced by a
        // SmartSelect run; filled by the host's on_run_prepare from
        // app state — the worker prefers it over rect/lasso for
        // Cutout/Remove/CopyToLayer when present and size-matching.
        std::shared_ptr<const std::vector<std::uint8_t>> mask;
        // Opaque host payload filled by Hooks::on_run_prepare on the UI
        // thread — the panel only copies it into the worker lambda.
        // The app uses it to hand the worker a snapshot of whatever UI
        // state on_run needs (project copy, playhead, project path) so
        // the job never dereferences live UI state.
        std::shared_ptr<const void> context;
        // Effective mask choice for Cutout/CopyToLayer/Remove:
        // polygon (>=3 pts) wins over the rect when present.
        bool UseLasso() const { return polygon.size() >= 3; }
    };

    struct Hooks {
        // Runs on the AsyncOp WORKER thread — do not touch UI state
        // inside; stash results and apply them on the next tick. The
        // app decodes/grabs the current monitor frame, runs the
        // requested Lab op and returns success. Poll
        // ctl.ShouldCancel() in long loops (inpaint wiring does this).
        std::function<bool(const RunParams&, const Lab::JobControl&)>
            on_run;
        // Optional UI-thread hook invoked in StartRun BEFORE the job
        // is dispatched — the host snapshots live state the worker
        // will need into params->context (see RunParams::context).
        std::function<void(RunParams*)> on_run_prepare;
        // Always invoked on the UI thread from the panel itself.
        std::function<void(const std::string&, bool is_error)> on_status;
        // Fired (UI thread) whenever the smart-select point set
        // changes — the host mirrors the points onto the monitor
        // overlay so clicks are visible.
        std::function<void()> on_points_changed;
        // The panel's Clear button also drops the host's stored AI
        // mask (it lives in app state, not here).
        std::function<void()> on_clear_mask;
    };

    explicit LabPanel(Hooks hooks = {});
    ~LabPanel() override;

    // Monitor-drawn rect pushed by the app; syncs the x/y/w/h fields.
    void SetRegion(Region region);
    Region GetRegion() const { return region_; }
    // Monitor-drawn lasso points; empty clears back to rect mode.
    void SetPolygon(std::vector<Lab::IntPoint> points);
    // SmartSelect prompt points — the app pushes monitor clicks here
    // while SmartSelect is the active mode.
    void AddPoint(Lab::IntPoint point);
    void ClearPoints();
    const std::vector<Lab::IntPoint>& Points() const { return points_; }
    // Readout for the stored AI mask ("AI mask: none" / "AI mask:
    // WxH ready") — set by the host after a SmartSelect run lands.
    void SetAiMaskText(const std::string& text);
    void SetMode(Mode mode);
    Mode GetMode() const { return mode_; }
    bool IsBusy() const { return busy_; }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {230.0f, 380.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {320.0f, 440.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    // Section header drawn by the panel itself — same convention as
    // InspectorWidget's muted font_small weight-600 labels.
    struct DrawnLine {
        XER::UI::Layout::Rect rect;
        std::string text;
    };

    void EnsureChildren();
    bool CollectParams(RunParams* params);
    void StartRun();
    void RequestCancelJob();
    void SetStatus(const std::string& text, bool is_error = false);
    void SyncModeButtons();
    void SyncRegionInputs();
    void UpdateRadiusLabel();
    void UpdatePointsLabel();
    static const char* ModeName(Mode mode);

    Hooks hooks_;
    Mode mode_ = Mode::Cutout;
    Region region_;
    std::vector<Lab::IntPoint> polygon_;
    std::vector<Lab::IntPoint> points_;  // smart-select prompt
    Lab::AsyncOp op_;
    bool busy_ = false;
    bool children_attached_ = false;
    bool syncing_ = false;

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::ButtonGroup> mode_group_;
    std::shared_ptr<XER::UI::Widgets::Button> cutout_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> remove_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> layer_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> nobg_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> smart_btn_;
    std::shared_ptr<XER::UI::Widgets::Separator> sep_mode_;
    std::shared_ptr<XER::UI::Widgets::Field> x_field_;
    std::shared_ptr<XER::UI::Widgets::Field> y_field_;
    std::shared_ptr<XER::UI::Widgets::Field> w_field_;
    std::shared_ptr<XER::UI::Widgets::Field> h_field_;
    std::shared_ptr<XER::UI::Widgets::Field> feather_field_;
    std::shared_ptr<XER::UI::Widgets::TextInput> x_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> y_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> w_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> h_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> feather_input_;
    std::shared_ptr<XER::UI::Widgets::Label> lasso_label_;
    std::shared_ptr<XER::UI::Widgets::Label> points_label_;
    std::shared_ptr<XER::UI::Widgets::Label> ai_mask_label_;
    std::shared_ptr<XER::UI::Widgets::Button> ai_clear_btn_;
    std::shared_ptr<XER::UI::Widgets::Separator> sep_region_;
    std::shared_ptr<XER::UI::Widgets::Field> radius_field_;
    std::shared_ptr<XER::UI::Widgets::Slider> radius_slider_;
    std::shared_ptr<XER::UI::Widgets::Separator> sep_run_;
    std::shared_ptr<XER::UI::Widgets::Button> run_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> cancel_btn_;
    std::shared_ptr<XER::UI::Widgets::Progress> progress_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    std::vector<DrawnLine> drawn_;
};

}  // namespace XER::VideoEditor::Ui
