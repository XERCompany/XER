#include "lab_panel.h"

#include "lab/mkl_accel.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/button_group.h"
#include "widgets/field.h"
#include "widgets/label.h"
#include "widgets/progress.h"
#include "widgets/separator.h"
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
    // Content taller than the tab rect must clip instead of spilling
    // onto the panel below (same flag the inspector sets for scroll).
    SetClipsChildren(true);
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

    // Mode selector as a segmented strip: ButtonGroup joins the three
    // buttons edge-to-edge; the active mode keeps the Primary fill
    // while the rest drop back to Secondary (SyncModeButtons).
    mode_group_ = std::make_shared<Widgets::ButtonGroup>(
        Widgets::ButtonGroup::Orientation::Horizontal);
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
    mode_group_->AddChild(cutout_btn_);
    mode_group_->AddChild(remove_btn_);
    mode_group_->AddChild(layer_btn_);

    // Region fields: each Field hosts a TextInput under its caption so
    // x/y/w/h are labeled instead of anonymous boxes. Enter inside any
    // field submits the run — the same convention the inspector uses
    // for its clip rows.
    const auto make_field =
        [this](const char* caption, const char* tooltip,
               std::shared_ptr<Widgets::Field>* field,
               std::shared_ptr<Widgets::TextInput>* input) {
            auto f = std::make_shared<Widgets::Field>();
            f->SetLabel(caption);
            auto in = std::make_shared<Widgets::TextInput>("0");
            in->SetPlaceholder("0");
            in->SetTooltip(tooltip);
            in->SetOnSubmit([this] {
                if (!busy_) StartRun();
            });
            f->AddChild(in);
            *field = f;
            *input = in;
        };
    make_field("X", "Region left edge (px)", &x_field_, &x_input_);
    make_field("Y", "Region top edge (px)", &y_field_, &y_input_);
    make_field("W", "Region width (px)", &w_field_, &w_input_);
    make_field("H", "Region height (px)", &h_field_, &h_input_);
    make_field("Feather (px)", "Edge feather in pixels",
               &feather_field_, &feather_input_);

    lasso_label_ = std::make_shared<Widgets::Label>("Lasso: none", true);

    // Inpaint tuning: the Field caption doubles as the live readout —
    // UpdateRadiusLabel rewrites it as "Search radius: N px".
    radius_field_ = std::make_shared<Widgets::Field>();
    radius_slider_ =
        std::make_shared<Widgets::Slider>(4.0f, 128.0f, 24.0f);
    radius_slider_->SetTooltip("Inpaint search radius (px)");
    radius_slider_->SetOnValueChanged([this](float) {
        if (children_attached_) UpdateRadiusLabel();
    });
    radius_field_->AddChild(radius_slider_);

    sep_mode_ = std::make_shared<Widgets::Separator>(
        Widgets::Orientation::Horizontal);
    sep_region_ = std::make_shared<Widgets::Separator>(
        Widgets::Orientation::Horizontal);
    sep_run_ = std::make_shared<Widgets::Separator>(
        Widgets::Orientation::Horizontal);

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
    AddChild(mode_group_);
    AddChild(sep_mode_);
    AddChild(x_field_);
    AddChild(y_field_);
    AddChild(w_field_);
    AddChild(h_field_);
    AddChild(feather_field_);
    AddChild(lasso_label_);
    AddChild(sep_region_);
    AddChild(radius_field_);
    AddChild(sep_run_);
    AddChild(cancel_btn_);
    AddChild(run_btn_);
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
    radius_field_->SetLabel("Search radius: " +
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
    // Keep GetRegion() honest: hand-edited fields count as the region
    // once they validate, not just rects pushed via SetRegion.
    region_ = p.region;
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
    // Section headers (MODE / REGION / INPAINT) — the inspector's
    // drawn-label convention: muted, small, weight 600.
    renderer.PushClip(bounds);
    for (const DrawnLine& line : drawn_) {
        renderer.DrawText(
            line.text, line.rect,
            XER::UI::Rendering::TextStyle{
                tokens.muted, tokens.font_small, 600});
    }
    renderer.PopClip();
}

void LabPanel::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;
    const float inner_x = bounds.x + pad;
    const float inner_w = bounds.width - pad * 2.0f;
    // Field's own caption band (label_h in field.cc) + a 28px control.
    const float caption_h = tokens.font_small * 1.4f + gap * 0.5f;
    const float field_h = caption_h + 28.0f;
    float y = bounds.y + pad * 0.6f;

    drawn_.clear();
    // Muted section header, then a 2px breath before the content.
    const auto section = [&](const char* text) {
        drawn_.push_back({{inner_x, y, inner_w, 16.0f}, text});
        y += 18.0f;
    };
    const auto separator =
        [&](const std::shared_ptr<XER::UI::Widgets::Separator>& sep) {
            sep->Layout({inner_x, y, inner_w, 8.0f}, tokens);
            y += 12.0f;
        };

    title_->Layout({inner_x, y, inner_w, 24.0f}, tokens);
    y += 32.0f;

    // ---- mode ----
    section("MODE");
    mode_group_->Layout({inner_x, y, inner_w, 30.0f}, tokens);
    y += 30.0f + gap;

    separator(sep_mode_);

    // ---- region ----
    section("REGION");
    const float quarter_w = (inner_w - gap * 3.0f) / 4.0f;
    x_field_->Layout({inner_x, y, quarter_w, field_h}, tokens);
    y_field_->Layout({inner_x + (quarter_w + gap), y, quarter_w,
                      field_h},
                     tokens);
    w_field_->Layout({inner_x + (quarter_w + gap) * 2.0f, y, quarter_w,
                      field_h},
                     tokens);
    h_field_->Layout({inner_x + (quarter_w + gap) * 3.0f, y, quarter_w,
                      field_h},
                     tokens);
    y += field_h + gap * 0.75f;

    // Feather on the left half; the lasso readout sits on the right
    // half, vertically aligned with the input row under the captions.
    const float half_w = (inner_w - gap) * 0.5f;
    feather_field_->Layout({inner_x, y, half_w, field_h}, tokens);
    lasso_label_->Layout(
        {inner_x + half_w + gap, y + caption_h, half_w, 28.0f}, tokens);
    y += field_h + gap;

    separator(sep_region_);

    // ---- inpaint ----
    section("INPAINT");
    const float radius_h = caption_h + 24.0f;
    radius_field_->Layout({inner_x, y, inner_w, radius_h}, tokens);
    y += radius_h + gap;

    separator(sep_run_);
    y += 2.0f;

    // ---- actions ----
    // Destructive/secondary Cancel on the left, primary Run on the
    // right — same action-row order as the inspector (Delete/Apply).
    cancel_btn_->Layout({inner_x, y, half_w, 32.0f}, tokens);
    run_btn_->Layout({inner_x + half_w + gap, y, half_w, 32.0f},
                     tokens);
    y += 40.0f;

    progress_->Layout({inner_x, y, inner_w, 14.0f}, tokens);
    y += 20.0f;
    status_->Layout({inner_x, y, inner_w, 20.0f}, tokens);
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
