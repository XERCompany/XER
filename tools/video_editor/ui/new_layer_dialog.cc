#include "new_layer_dialog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

#include "generated_source.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/field.h"
#include "widgets/label.h"
#include "widgets/text_input.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Color;

constexpr float kPanelWidth = 460.0f;
constexpr float kPanelHeight = 320.0f;
constexpr float kPanelPad = 22.0f;

// The Field whose inner control is `control` (the Field owns the
// widget it captions) — same helper shape SpeedDialog uses.
XER::UI::Widgets::Field* FieldHosting(
    XER::UI::Widgets::Widget* parent,
    const XER::UI::Widgets::Widget* control) {
    if (parent == nullptr || control == nullptr) return nullptr;
    for (const auto& child : parent->GetChildren()) {
        auto* field =
            dynamic_cast<XER::UI::Widgets::Field*>(child.get());
        if (field == nullptr) continue;
        for (const auto& inner : field->GetChildren()) {
            if (inner.get() == control) return field;
        }
    }
    return nullptr;
}

// Lenient numeric parse: empty/garbage/non-finite -> NaN (invalid).
double ParseNumber(const std::string& text) {
    if (text.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(value)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return value;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

NewLayerDialog::NewLayerDialog(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(false);
}

void NewLayerDialog::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;
    title_ = std::make_shared<Widgets::Label>("New color matte");
    name_input_ = std::make_shared<Widgets::TextInput>("");
    name_input_->SetPlaceholder("Color Matte");
    color_input_ = std::make_shared<Widgets::TextInput>("#3a5fd9");
    duration_input_ = std::make_shared<Widgets::TextInput>("5.000");

    const auto refresh = [this](const std::string&) {
        if (syncing_fields_) return;
        RefreshValidation();
    };
    name_input_->SetOnValueChanged(refresh);
    color_input_->SetOnValueChanged(refresh);
    duration_input_->SetOnValueChanged(refresh);
    const auto submit = [this] { Apply(); };
    name_input_->SetOnSubmit(submit);
    color_input_->SetOnSubmit(submit);
    duration_input_->SetOnSubmit(submit);

    apply_button_ = std::make_shared<Widgets::Button>(
        "Add", Widgets::ButtonStyle::Primary);
    apply_button_->SetOnClick([this] { Apply(); });
    cancel_button_ = std::make_shared<Widgets::Button>(
        "Cancel", Widgets::ButtonStyle::Secondary);
    cancel_button_->SetOnClick([this] { Cancel(); });

    AddChild(title_);
    const auto wrap_field =
        [this](const char* caption, const char* description,
               const std::shared_ptr<Widgets::TextInput>& input) {
            auto field = std::make_shared<Widgets::Field>();
            field->SetLabel(caption);
            field->SetDescription(description);
            field->AddChild(input);
            AddChild(field);
        };
    wrap_field("Name", "optional \u2014 label on the clip",
               name_input_);
    wrap_field("Color", "#RRGGBB or #RRGGBBAA", color_input_);
    wrap_field("Duration (s)", "seconds", duration_input_);
    AddChild(apply_button_);
    AddChild(cancel_button_);
    RefreshValidation();
}

void NewLayerDialog::SetDefaults(const std::string& name,
                                 const std::string& default_hex,
                                 double duration_seconds) {
    EnsureChildren();
    syncing_fields_ = true;
    name_input_->SetValue(name);
    color_input_->SetValue(IsMatteHex(default_hex) ? default_hex
                                                   : "#3a5fd9");
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f",
                  std::isfinite(duration_seconds) && duration_seconds > 0.0
                      ? duration_seconds
                      : 5.0);
    duration_input_->SetValue(buffer);
    syncing_fields_ = false;
    RefreshValidation();
    Invalidate();
}

bool NewLayerDialog::IsMatteHex(const std::string& text) {
    std::size_t start = 0;
    if (!text.empty() && text.front() == '#') start = 1;
    const std::size_t digits = text.size() - start;
    if (digits != 6 && digits != 8) return false;
    for (std::size_t i = start; i < text.size(); ++i) {
        if (HexDigit(text[i]) < 0) return false;
    }
    return true;
}

std::string NewLayerDialog::NormalizeHex(const std::string& text) {
    if (!IsMatteHex(text)) return {};
    std::string out = text;
    if (out.front() != '#') out.insert(out.begin(), '#');
    for (std::size_t i = 1; i < out.size(); ++i) {
        out[i] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(out[i])));
    }
    return out;
}

NewLayerSpec NewLayerDialog::GetSpec() const {
    NewLayerSpec spec;
    if (!children_attached_) return spec;
    spec.name = name_input_->GetValue();
    spec.color_hex = NormalizeHex(color_input_->GetValue());
    const double duration = ParseNumber(duration_input_->GetValue());
    spec.duration_seconds =
        std::isfinite(duration) && duration > 0.0 ? duration : 5.0;
    return spec;
}

bool NewLayerDialog::Valid() const {
    if (!children_attached_) return false;
    const double duration = ParseNumber(duration_input_->GetValue());
    return IsMatteHex(color_input_->GetValue()) &&
           std::isfinite(duration) && duration > 0.0;
}

void NewLayerDialog::RefreshValidation() {
    if (!children_attached_) return;
    const bool color_ok = IsMatteHex(color_input_->GetValue());
    const double duration = ParseNumber(duration_input_->GetValue());
    const bool duration_ok =
        std::isfinite(duration) && duration > 0.0;
    hint_text_.clear();
    if (!color_ok) {
        hint_text_ = "Color must be #RRGGBB or #RRGGBBAA.";
    } else if (!duration_ok) {
        hint_text_ = "Duration must be a positive number of seconds.";
    }
    apply_button_->SetEnabled(color_ok && duration_ok);
    if (auto* field = FieldHosting(this, color_input_.get())) {
        field->SetError(!color_ok ? hint_text_ : "");
    }
    if (auto* field = FieldHosting(this, duration_input_.get())) {
        field->SetError(color_ok && !duration_ok ? hint_text_ : "");
    }
    Invalidate();
}

void NewLayerDialog::Apply() {
    if (!Valid() || !hooks_.on_apply) return;
    hooks_.on_apply(GetSpec());
}

void NewLayerDialog::Cancel() {
    if (hooks_.on_cancel) hooks_.on_cancel();
}

Layout::Rect NewLayerDialog::PanelRect() const {
    const auto bounds = GetBounds();
    const float width = std::min(kPanelWidth, bounds.width - 40.0f);
    return {bounds.x + (bounds.width - width) * 0.5f,
            bounds.y + (bounds.height - kPanelHeight) * 0.5f,
            width, kPanelHeight};
}

Layout::Rect NewLayerDialog::SwatchRect() const {
    const auto panel = PanelRect();
    // Right of the color Field's input column (Layout() positions).
    return {panel.x + kPanelPad + 170.0f, panel.y + 158.0f,
            panel.width - kPanelPad * 2.0f - 170.0f, 26.0f};
}

void NewLayerDialog::Render(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, VeTheme::kScrim, 0.0f);
    const auto panel = PanelRect();
    renderer.FillRect(panel, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(panel, tokens.border, 1.0f,
                        tokens.radius_large);
    renderer.FillRect({panel.x, panel.y, 4.0f, panel.height},
                      tokens.accent, tokens.radius_large);

    // Live matte preview: the lenient parser falls back to black, so
    // a half-typed hex still shows *something* — ParseMatteColor
    // returns {b,g,r,a}; the renderer wants float rgba.
    const auto bgra = ParseMatteColor(color_input_->GetValue());
    const Color swatch{bgra[2] / 255.0f, bgra[1] / 255.0f,
                       bgra[0] / 255.0f, bgra[3] / 255.0f};
    const Layout::Rect sw = SwatchRect();
    renderer.FillRect(sw, swatch, tokens.radius_small);
    renderer.StrokeRect(sw, tokens.border, 1.0f, tokens.radius_small);
}

void NewLayerDialog::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const auto panel = PanelRect();
    const float px = panel.x + kPanelPad;
    const float pw = panel.width - kPanelPad * 2.0f;

    title_->Layout({px, panel.y + 14.0f, pw, 26.0f}, tokens);

    const float caption_h =
        tokens.font_small * 1.4f + tokens.spacing_small * 0.5f;
    const float footer_h = tokens.font_small * 1.5f + 6.0f;
    const float field_h = caption_h + 32.0f + footer_h;
    int row = 0;
    for (const auto* input :
         {name_input_.get(), color_input_.get(),
          duration_input_.get()}) {
        if (auto* field = FieldHosting(this, input)) {
            field->Layout(
                {px, panel.y + 56.0f +
                         static_cast<float>(row) * (field_h + 6.0f),
                 input == color_input_.get() ? 160.0f : pw, field_h},
                tokens);
        }
        ++row;
    }

    const float button_y = panel.y + panel.height - 48.0f;
    cancel_button_->Layout({panel.x + panel.width - kPanelPad - 196.0f,
                            button_y, 94.0f, 34.0f},
                           tokens);
    apply_button_->Layout({panel.x + panel.width - kPanelPad - 94.0f,
                           button_y, 94.0f, 34.0f},
                          tokens);
}

bool NewLayerDialog::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    if (event.type == Event::Type::KeyDown) {
        if (event.key == XER::UI::Widgets::KeyCode::Escape) {
            Cancel();
            return true;
        }
        if (event.key == XER::UI::Widgets::KeyCode::Enter) {
            Apply();
            return true;
        }
    }
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left &&
        !PanelRect().Contains(event.position)) {
        Cancel();   // backdrop click cancels (modal convention)
        return true;
    }
    // Modal: consume pointer events inside and outside the panel so
    // the workspace below stays inert while the dialog is up.
    return event.type == Event::Type::MouseDown ||
           event.type == Event::Type::MouseUp ||
           event.type == Event::Type::Click;
}

void NewLayerDialog::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
