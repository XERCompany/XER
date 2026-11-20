#include "lab_panel.h"

#include "lab/mkl_accel.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/progress.h"
#include "widgets/slider.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui {
namespace {

// Strict integer field parse — floats would silently truncate a
// monitor-dragged rect, which is exactly the bug this guards against.
bool ParseInt(const std::string& text, int* value) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE) {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

std::string IntText(int value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

}  // namespace

LabPanel::LabPanel(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
}

LabPanel::~LabPanel() {
    op_.RequestCancel();  // AsyncOp dtor joins; be explicit anyway.
}

const char* LabPanel::ModeName(Mode mode) {
    switch (mode) {
        case Mode::Cutout: return "Cut out";
        case Mode::Remove: return "Remove";
        case Mode::CopyToLayer: return "Copy to layer";
    }
    return "?";
}

void LabPanel::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    title_ = std::make_shared<Widgets::Label>("OBJECT LAB");

    cutout_btn_ = std::make_shared<Widgets::Button>(
        "Cut out", Widgets::ButtonStyle::Primary);
    cutout_btn_->SetTooltip("Extract the region into a new layer frame");
    cutout_btn_->SetOnClick([this] { SetMode(Mode::Cutout); });
    remove_btn_ = std::make_shared<Widgets::Button>(
        "Remove", Widgets::ButtonStyle::Secondary);
    remove_btn_->SetTooltip("Inpaint the region away (slow, cancellable)");
    remove_btn_->SetOnClick([this] { SetMode(Mode::Remove); });
    layer_btn_ = std::make_shared<Widgets::Button>(
        "To layer", Widgets::ButtonStyle::Secondary);
    layer_btn_->SetTooltip("Copy the region onto a new transparent layer");
    layer_btn_->SetOnClick([this] { SetMode(Mode::CopyToLayer); });

    auto field = [](const char* placeholder) {
        auto input = std::make_shared<Widgets::TextInput>("0");
        input->SetPlaceholder(placeholder);
        return input;
    };
    x_input_ = field("x");
    y_input_ = field("y");
    w_input_ = field("w");
    h_input_ = field("h");
    feather_input_ = field("feather px");

    lasso_label_ = std::make_shared<Widgets::Label>("Lasso: none", true);
    radius_slider_ =
        std::make_shared<Widgets::Slider>(4.0f, 128.0f, 24.0f);
    radius_slider_->SetTooltip("Inpaint search radius (px)");
    radius_slider_->SetOnValueChanged([this](float) {
        if (children_attached_) UpdateRadiusLabel();
    });
    radius_label_ = std::make_shared<Widgets::Label>("", true);

    run_btn_ = std::make_shared<Widgets::Button>(
        "Run", Widgets::ButtonStyle::Primary);
    run_btn_->SetOnClick([this] { StartRun(); });
    cancel_btn_ = std::make_shared<Widgets::Button>(
        "Cancel", Widgets::ButtonStyle::Destructive);
    cancel_btn_->SetEnabled(false);
    cancel_btn_->SetOnClick([this] { RequestCancelJob(); });
    progress_ = std::make_shared<Widgets::Progress>(0.0f);
    status_ = std::make_shared<Widgets::Label>("Ready", true);

    AddChild(title_);
    AddChild(cutout_btn_);
    AddChild(remove_btn_);
    AddChild(layer_btn_);
    AddChild(x_input_);
    AddChild(y_input_);
    AddChild(w_input_);
    AddChild(h_input_);
    AddChild(feather_input_);
    AddChild(lasso_label_);
    AddChild(radius_slider_);
    AddChild(radius_label_);
    AddChild(run_btn_);
    AddChild(cancel_btn_);
    AddChild(progress_);
    AddChild(status_);

    SyncRegionInputs();
    SyncModeButtons();
    UpdateRadiusLabel();
}

void LabPanel::SetRegion(Region region) {
    region_ = region;
    if (children_attached_) SyncRegionInputs();
    Invalidate();
}

void LabPanel::SetPolygon(std::vector<Lab::IntPoint> points) {
    polygon_ = std::move(points);
    if (children_attached_) {
        lasso_label_->SetText(
            polygon_.size() >= 3
                ? "Lasso: " + std::to_string(polygon_.size()) + " pts"
                : "Lasso: none");
    }
    Invalidate();
}

void LabPanel::SetMode(Mode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    if (children_attached_) SyncModeButtons();
    Invalidate();
}

void LabPanel::SyncModeButtons() {
    if (!children_attached_) return;
    namespace Widgets = XER::UI::Widgets;
    cutout_btn_->SetStyle(mode_ == Mode::Cutout
                              ? Widgets::ButtonStyle::Primary
                              : Widgets::ButtonStyle::Secondary);
    remove_btn_->SetStyle(mode_ == Mode::Remove
                              ? Widgets::ButtonStyle::Primary
                              : Widgets::ButtonStyle::Secondary);
    layer_btn_->SetStyle(mode_ == Mode::CopyToLayer
                             ? Widgets::ButtonStyle::Primary
                             : Widgets::ButtonStyle::Secondary);
}

void LabPanel::SyncRegionInputs() {
    syncing_ = true;
    x_input_->SetValue(IntText(region_.x));
    y_input_->SetValue(IntText(region_.y));
    w_input_->SetValue(IntText(region_.w));
    h_input_->SetValue(IntText(region_.h));
    syncing_ = false;
}

void LabPanel::UpdateRadiusLabel() {
    radius_label_->SetText("Search radius: " +
                           IntText(static_cast<int>(
                               radius_slider_->GetValue())) +
                           " px");
}

bool LabPanel::CollectParams(RunParams* params) {
    if (params == nullptr) return false;
    RunParams p;
    p.mode = mode_;
    p.polygon = polygon_;
    int v = 0;
    const std::pair<XER::UI::Widgets::TextInput*, const char*> fields[] = {
        {x_input_.get(), "x"}, {y_input_.get(), "y"},
        {w_input_.get(), "w"}, {h_input_.get(), "h"},
    };
    int* targets[] = {&p.region.x, &p.region.y, &p.region.w, &p.region.h};
    for (size_t i = 0; i < std::size(fields); ++i) {
        if (!ParseInt(fields[i].first->GetValue(), &v)) {
            SetStatus(std::string("Bad integer in '") + fields[i].second +
                          "'",
                      true);
            return false;
        }
        *targets[i] = v;
    }
    if (p.region.w <= 0 || p.region.h <= 0) {
        SetStatus("Region w/h must be > 0", true);
        return false;
    }
    if (!ParseInt(feather_input_->GetValue(), &p.feather_px) ||
        p.feather_px < 0) {
        SetStatus("Feather must be an integer >= 0", true);
        return false;
    }
    p.search_radius =
        static_cast<int>(radius_slider_->GetValue() + 0.5f);
    *params = std::move(p);
    return true;
}

void LabPanel::StartRun() {
    if (busy_ || op_.IsRunning()) return;
    RunParams params;
    if (!CollectParams(&params)) return;
    if (!hooks_.on_run) {
        SetStatus("No run hook bound", true);
        return;
    }
    progress_->SetValue(0.0f);
    // UI-thread hook: the host snapshots whatever state the worker
    // will need (project copy etc.) into params->context BEFORE the
    // job lambda captures params — the worker then reads the snapshot
    // instead of racing live UI state.
    if (hooks_.on_run_prepare) {
        hooks_.on_run_prepare(&params);
    }
    // Copy hooks + params into the job — never capture `this`: the
    // panel could be destroyed while the worker is still running.
    const Hooks hooks = hooks_;
    const bool started =
        op_.Start([hooks, params](const Lab::JobControl& ctl) {
            return hooks.on_run ? hooks.on_run(params, ctl) : false;
        });
    if (!started) {
        SetStatus("Could not start job", true);
        return;
    }
    busy_ = true;
    run_btn_->SetEnabled(false);
    cancel_btn_->SetEnabled(true);
    SetStatus(std::string("Running: ") + ModeName(params.mode) + " (" +
              Lab::BackendName() + ")");
}

void LabPanel::RequestCancelJob() {
    op_.RequestCancel();
    if (busy_) SetStatus("Cancelling…");
}

void LabPanel::SetStatus(const std::string& text, bool is_error) {
    if (children_attached_) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void LabPanel::Render(XER::UI::Rendering::Renderer& renderer,
                      const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f,
                        tokens.radius_large);
    if (busy_) {
        renderer.FillRect({bounds.x + 10.0f, bounds.y + 34.0f,
                           bounds.width - 20.0f, 3.0f},
                          tokens.accent, 2.0f);
    }
}

void LabPanel::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float inner_w = bounds.width - pad * 2.0f;
    const float half_w = (inner_w - pad * 0.5f) * 0.5f;
    float y = bounds.y + pad * 0.6f;

    title_->Layout({bounds.x + pad, y, inner_w, 24.0f}, tokens);
    y += 30.0f;

    // Mode selector row: three radio-style buttons.
    const float mode_w = (inner_w - pad) / 3.0f;
    cutout_btn_->Layout({bounds.x + pad, y, mode_w, 30.0f}, tokens);
    remove_btn_->Layout(
        {bounds.x + pad + mode_w + pad * 0.5f, y, mode_w, 30.0f},
        tokens);
    layer_btn_->Layout(
        {bounds.x + pad + (mode_w + pad * 0.5f) * 2.0f, y, mode_w,
         30.0f},
        tokens);
    y += 40.0f;

    // Region row: x/y on one line, w/h on the next.
    x_input_->Layout({bounds.x + pad, y, half_w, 30.0f}, tokens);
    y_input_->Layout({bounds.x + pad + half_w + pad * 0.5f, y, half_w,
                      30.0f},
                     tokens);
    y += 36.0f;
    w_input_->Layout({bounds.x + pad, y, half_w, 30.0f}, tokens);
    h_input_->Layout({bounds.x + pad + half_w + pad * 0.5f, y, half_w,
                      30.0f},
                     tokens);
    y += 36.0f;

    feather_input_->Layout({bounds.x + pad, y, half_w, 30.0f}, tokens);
    lasso_label_->Layout(
        {bounds.x + pad + half_w + pad * 0.5f, y + 4.0f, half_w, 22.0f},
        tokens);
    y += 38.0f;

    radius_slider_->Layout({bounds.x + pad, y, inner_w, 24.0f}, tokens);
    y += 26.0f;
    radius_label_->Layout({bounds.x + pad, y, inner_w, 20.0f}, tokens);
    y += 30.0f;

    run_btn_->Layout({bounds.x + pad, y, half_w, 32.0f}, tokens);
    cancel_btn_->Layout(
        {bounds.x + pad + half_w + pad * 0.5f, y, half_w, 32.0f},
        tokens);
    y += 42.0f;

    progress_->Layout({bounds.x + pad, y, inner_w, 16.0f}, tokens);
    y += 26.0f;
    status_->Layout({bounds.x + pad, y, inner_w, 22.0f}, tokens);
}

bool LabPanel::HandleEvent(const XER::UI::Widgets::Event& event) {
    if (event.type == XER::UI::Widgets::Event::Type::KeyDown &&
        event.key == XER::UI::Widgets::KeyCode::Enter && !busy_) {
        StartRun();
        return true;
    }
    return false;
}

void LabPanel::Tick(double dt_seconds) {
    (void)dt_seconds;
    if (!busy_) return;
    progress_->SetValue(op_.Progress());
    bool ok = false;
    bool cancelled = false;
    std::string error;
    if (op_.PopResult(&ok, &cancelled, &error)) {
        busy_ = false;
        run_btn_->SetEnabled(true);
        cancel_btn_->SetEnabled(false);
        if (cancelled) {
            SetStatus("Cancelled", true);
        } else if (ok) {
            progress_->SetValue(1.0f);
            SetStatus("Done");
        } else {
            SetStatus(error.empty() ? "Failed" : error, true);
        }
    }
    Invalidate();  // keep polling while the job is alive
}

}  // namespace XER::VideoEditor::Ui
