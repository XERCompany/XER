// title_editor.cc — see title_editor.h for the dialog contract.
// Layout convention follows lab_panel/settings_dialog: every control
// sits in a Field (caption + control), chip rows are ButtonGroups,
// toggles are shadcn Toggle buttons, and the modal swallows every
// pointer event while it is up.
#include "title_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "font_catalog.h"
#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "text_style.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/button_group.h"
#include "widgets/combobox.h"
#include "widgets/field.h"
#include "widgets/label.h"
#include "widgets/slider.h"
#include "widgets/text_input.h"
#include "widgets/textarea.h"
#include "widgets/toggle.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
namespace Widgets = XER::UI::Widgets;

constexpr float kPanelWidth = 620.0f;
constexpr float kPanelPad = 22.0f;
constexpr float kFieldGap = 10.0f;

// The Field whose inner control is `control` (a Field owns the widget
// it captions).
Widgets::Field* FieldHosting(Widgets::Widget* parent,
                             const Widgets::Widget* control) {
    if (parent == nullptr || control == nullptr) return nullptr;
    for (const auto& child : parent->GetChildren()) {
        auto* field = dynamic_cast<Widgets::Field*>(child.get());
        if (field == nullptr) continue;
        for (const auto& inner : field->GetChildren()) {
            if (inner.get() == control) return field;
        }
    }
    return nullptr;
}

// Keeps a chip strip's active-mode highlight in sync — selected is
// Primary, the rest Secondary (the lab_panel mode convention).
void SyncButtons(Widgets::ButtonGroup* group, int selected) {
    if (group == nullptr) return;
    const auto& buttons = group->GetChildren();
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        static_cast<Widgets::Button*>(buttons[i].get())
            ->SetStyle(static_cast<int>(i) == selected
                           ? Widgets::ButtonStyle::Primary
                           : Widgets::ButtonStyle::Secondary);
    }
}

constexpr const char* kAlignLabels[] = {"Left", "Center", "Right"};
constexpr const char* kVAlignLabels[] = {"Top", "Middle", "Bottom"};

// The weight strip's canonical set — DirectWrite weights map 1:1 onto
// ClipTextStyle::font_weight.
constexpr struct {
    const char* label;
    int weight;
} kWeights[] = {
    {"Regular", 400}, {"Medium", 500}, {"SemiBold", 600},
    {"Bold", 700},    {"Black", 900},
};

// Lenient parses: empty/garbage -> fallback, no exceptions.
float ParseFloatSafe(const std::string& text, float fallback, float lo,
                     float hi) {
    if (text.empty()) return fallback;
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(v)) return fallback;
    return std::clamp(v, lo, hi);
}

int ParseIntSafe(const std::string& text, int fallback, int lo, int hi) {
    if (text.empty()) return fallback;
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str()) return fallback;
    return static_cast<int>(
        std::clamp<long>(v, static_cast<long>(lo), static_cast<long>(hi)));
}

std::string FloatToString(float v) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(v));
    std::string s = buffer;
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s.empty() || s == "-0") s = "0";
    return s;
}

std::string IntToString(int value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

std::string RgbaToHex(const std::array<std::uint8_t, 4>& rgba) {
    char buffer[16];
    if (rgba[3] == 255) {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", rgba[0],
                      rgba[1], rgba[2]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X",
                      rgba[0], rgba[1], rgba[2], rgba[3]);
    }
    return buffer;
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    const int lo = std::tolower(static_cast<unsigned char>(c));
    if (lo >= 'a' && lo <= 'f') return lo - 'a' + 10;
    return -1;
}

// Lenient hex -> RGBA (input may carry a stray '#'); `fallback` on any
// malformed text.
std::array<std::uint8_t, 4> ParseHexRgba(
    const std::string& text,
    const std::array<std::uint8_t, 4>& fallback) {
    std::size_t start = text.empty() || text.front() != '#' ? 0 : 1;
    const std::size_t digits = text.size() - start;
    if (digits != 6 && digits != 8) return fallback;
    int nibbles[8];
    for (std::size_t i = 0; i < digits; ++i) {
        const int v = HexDigit(text[start + i]);
        if (v < 0) return fallback;
        nibbles[i] = v;
    }
    const auto byte = [&](std::size_t i) {
        return static_cast<std::uint8_t>(nibbles[i] * 16 + nibbles[i + 1]);
    };
    std::array<std::uint8_t, 4> out{byte(0), byte(2), byte(4), 255};
    if (digits == 8) out[3] = byte(6);
    return out;
}

}  // namespace

// TitleStyle <-> ClipTextStyle: same fields, RGBA->BGRA swap on colors.
ClipTextStyle TitleStyleToClipStyle(const TitleStyle& t) {
    ClipTextStyle s;
    s.text = t.text;
    s.font_family = t.font_family;
    s.font_weight = t.font_weight;
    s.italic = t.italic;
    s.underline = t.underline;
    s.strike = t.strike;
    s.size_px = t.size_px;
    s.letter_spacing = t.letter_spacing;
    s.line_spacing = t.line_spacing;
    s.align = t.align == 0   ? ClipTextStyle::Align::Left
              : t.align == 2 ? ClipTextStyle::Align::Right
                             : ClipTextStyle::Align::Center;
    s.valign = t.valign == 1   ? ClipTextStyle::VAlign::Middle
               : t.valign == 2 ? ClipTextStyle::VAlign::Bottom
                               : ClipTextStyle::VAlign::Top;
    s.pos_x = t.x;
    s.pos_y = t.y;
    s.box_w = t.w;
    s.box_h = t.h;
    s.corner_radius = t.corner_radius;
    s.rotate_deg = t.rotate_deg;
    s.baseline_px = t.baseline_px;
    s.color = {t.color[2], t.color[1], t.color[0], t.color[3]};
    s.fill_opacity = t.opacity;
    s.outline_px = t.outline_px;
    s.outline_color = {t.outline_color[2], t.outline_color[1],
                       t.outline_color[0], t.outline_color[3]};
    s.bg_color = {t.bg_color[2], t.bg_color[1], t.bg_color[0],
                  t.bg_color[3]};
    s.shadow_explicit = t.shadow_explicit;
    s.shadow_x = t.shadow_x;
    s.shadow_y = t.shadow_y;
    s.shadow_blur = t.shadow_blur;
    s.shadow_color = {t.shadow_color[2], t.shadow_color[1],
                      t.shadow_color[0], t.shadow_color[3]};
    return s;
}

TitleStyle TitleStyleFromClipStyle(const ClipTextStyle& s) {
    TitleStyle t;
    t.text = s.text;
    t.font_family = s.font_family;
    t.font_weight = s.font_weight;
    t.italic = s.italic;
    t.underline = s.underline;
    t.strike = s.strike;
    t.size_px = s.size_px;
    t.letter_spacing = s.letter_spacing;
    t.line_spacing = s.line_spacing;
    t.align = s.align == ClipTextStyle::Align::Left   ? 0
              : s.align == ClipTextStyle::Align::Right ? 2
                                                     : 1;
    t.valign = s.valign == ClipTextStyle::VAlign::Middle ? 1
               : s.valign == ClipTextStyle::VAlign::Bottom ? 2
                                                         : 0;
    t.x = s.pos_x;
    t.y = s.pos_y;
    t.w = s.box_w;
    t.h = s.box_h;
    t.corner_radius = s.corner_radius;
    t.rotate_deg = s.rotate_deg;
    t.baseline_px = s.baseline_px;
    t.color = {s.color[2], s.color[1], s.color[0], s.color[3]};
    t.opacity = s.fill_opacity;
    t.outline_px = s.outline_px;
    t.outline_color = {s.outline_color[2], s.outline_color[1],
                       s.outline_color[0], s.outline_color[3]};
    t.bg_color = {s.bg_color[2], s.bg_color[1], s.bg_color[0],
                  s.bg_color[3]};
    t.shadow_explicit = s.shadow_explicit;
    t.shadow_x = s.shadow_x;
    t.shadow_y = s.shadow_y;
    t.shadow_blur = s.shadow_blur;
    t.shadow_color = {s.shadow_color[2], s.shadow_color[1],
                      s.shadow_color[0], s.shadow_color[3]};
    return t;
}

TitleEditor::TitleEditor(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(false);
}

void TitleEditor::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    VeIcons::InstallButtonIconResolver();

    title_ = std::make_shared<Widgets::Label>("Edit title");
    text_area_ = std::make_shared<Widgets::Textarea>();
    text_area_->SetPlaceholder("Title text\u2026");

    font_combo_ = Widgets::Combobox::Create();
    font_combo_->SetPlaceholder("Font family\u2026");
    font_combo_->SetOnSelect(
        [this](const Widgets::Combobox::Option& option) {
            font_combo_->SetValue(option.label);
            RefreshWeightOptions();
            MarkPreviewDirty();
        });
    weight_combo_ = Widgets::Combobox::Create();
    weight_combo_->SetPlaceholder("Weight\u2026");
    weight_combo_->SetOnSelect(
        [this](const Widgets::Combobox::Option& option) {
            weight_combo_->SetValue(option.value.empty() ? option.label
                                                         : option.value);
            MarkPreviewDirty();
        });

    const auto make_input = [this](const char* value) {
        auto input = std::make_shared<Widgets::TextInput>(value);
        input->SetOnValueChanged(
            [this](const std::string&) { MarkPreviewDirty(); });
        input->SetOnSubmit([this] { Apply(); });
        return input;
    };
    size_input_ = make_input("64");
    tracking_input_ = make_input("0");
    leading_input_ = make_input("0");
    x_input_ = make_input("0");
    y_input_ = make_input("0");
    rotate_input_ = make_input("0");
    w_input_ = make_input("0");
    h_input_ = make_input("0");
    radius_input_ = make_input("0");
    baseline_input_ = make_input("0");
    fill_hex_input_ = make_input("#FFFFFF");
    outline_input_ = make_input("0");
    outline_hex_input_ = make_input("#000000");
    bg_hex_input_ = make_input("#00000000");
    shadow_x_input_ = make_input("2");
    shadow_y_input_ = make_input("2");
    shadow_blur_input_ = make_input("4");
    shadow_hex_input_ = make_input("#00000080");

    opacity_slider_ =
        std::make_shared<Widgets::Slider>(0.0f, 1.0f, 1.0f);
    opacity_slider_->SetOnValueChanged(
        [this](float) { MarkPreviewDirty(); });

    // Align + valign as segmented chip strips.
    const auto make_strip = [](const char* const* labels, int count,
                               std::function<void(int)> on_pick) {
        auto group = std::make_shared<Widgets::ButtonGroup>(
            Widgets::ButtonGroup::Orientation::Horizontal);
        for (int i = 0; i < count; ++i) {
            auto button = std::make_shared<Widgets::Button>(
                labels[i], Widgets::ButtonStyle::Secondary);
            button->SetOnClick([on_pick, i] { on_pick(i); });
            group->AddChild(button);
        }
        return group;
    };
    align_group_ = make_strip(kAlignLabels, 3, [this](int i) {
        SelectAlign(i);
    });
    valign_group_ = make_strip(kVAlignLabels, 3, [this](int i) {
        SelectVAlign(i);
    });

    // B/I/U/S toggles in one joined strip.
    const auto make_toggle = [this](const char* text) {
        auto toggle =
            std::make_shared<Widgets::Toggle>(text, false);
        toggle->SetOnPressedChanged(
            [this](bool) { MarkPreviewDirty(); });
        return toggle;
    };
    bold_toggle_ = make_toggle("B");
    italic_toggle_ = make_toggle("I");
    underline_toggle_ = make_toggle("U");
    strike_toggle_ = make_toggle("S");
    deco_group_ = std::make_shared<Widgets::ButtonGroup>(
        Widgets::ButtonGroup::Orientation::Horizontal);
    deco_group_->AddChild(bold_toggle_);
    deco_group_->AddChild(italic_toggle_);
    deco_group_->AddChild(underline_toggle_);
    deco_group_->AddChild(strike_toggle_);
    shadow_toggle_ = std::make_shared<Widgets::Toggle>("Shadow", false);
    shadow_toggle_->SetOnPressedChanged(
        [this](bool) { MarkPreviewDirty(); });

    const auto wrap_field =
        [this](const char* caption,
               const std::shared_ptr<Widgets::Widget>& control) {
            auto field = std::make_shared<Widgets::Field>();
            field->SetLabel(caption);
            field->AddChild(control);
            AddChild(field);
        };
    wrap_field("Text", text_area_);
    wrap_field("Font family", font_combo_);
    wrap_field("Style", weight_combo_);
    wrap_field("Size (px)", size_input_);
    wrap_field("Tracking", tracking_input_);
    wrap_field("Leading", leading_input_);
    wrap_field("Align", align_group_);
    wrap_field("Vertical align", valign_group_);
    wrap_field("Decorations", deco_group_);
    wrap_field("X", x_input_);
    wrap_field("Y", y_input_);
    wrap_field("Rotation", rotate_input_);
    wrap_field("Box W", w_input_);
    wrap_field("Box H", h_input_);
    wrap_field("Radius", radius_input_);
    wrap_field("Baseline", baseline_input_);
    wrap_field("Fill color", fill_hex_input_);
    wrap_field("Fill opacity", opacity_slider_);
    wrap_field("Outline px", outline_input_);
    wrap_field("Outline color", outline_hex_input_);
    wrap_field("Box color", bg_hex_input_);
    wrap_field("Drop shadow", shadow_toggle_);
    wrap_field("Shadow X", shadow_x_input_);
    wrap_field("Shadow Y", shadow_y_input_);
    wrap_field("Shadow blur", shadow_blur_input_);
    wrap_field("Shadow color", shadow_hex_input_);

    apply_button_ = std::make_shared<Widgets::Button>(
        "Apply", Widgets::ButtonStyle::Primary);
    apply_button_->SetOnClick([this] { Apply(); });
    cancel_button_ = std::make_shared<Widgets::Button>(
        "Cancel", Widgets::ButtonStyle::Secondary);
    cancel_button_->SetOnClick([this] { Cancel(); });
    AddChild(title_);
    AddChild(apply_button_);
    AddChild(cancel_button_);

    RefreshFontOptions();
    SyncButtons(align_group_.get(), align_);
    SyncButtons(valign_group_.get(), valign_);
}

void TitleEditor::RefreshFontOptions() {
    if (!font_combo_) return;
    std::vector<Widgets::Combobox::Option> options;
    for (const std::string& name : FontCatalog::FamilyNames()) {
        options.push_back({name, name});
    }
    if (options.empty()) {
        // No system catalog (Skia-less build) — the field still takes
        // a typed family name.
        options.push_back({"Default", ""});
    }
    font_combo_->SetOptions(std::move(options));
}

void TitleEditor::RefreshWeightOptions() {
    if (!weight_combo_) return;
    std::vector<Widgets::Combobox::Option> options;
    const std::string family =
        font_combo_ ? font_combo_->GetValue() : std::string{};
    for (const FontStyleInfo& info : FontCatalog::StylesOf(family)) {
        options.push_back({info.name, std::to_string(info.weight)});
    }
    if (options.empty()) {
        for (const auto& w : kWeights) {
            options.push_back(
                {w.label, std::to_string(w.weight)});
        }
    }
    weight_combo_->SetOptions(std::move(options));
}

void TitleEditor::SetStyle(const TitleStyle& style) {
    EnsureChildren();
    text_area_->SetText(style.text);
    font_combo_->SetValue(style.font_family);
    RefreshWeightOptions();
    weight_combo_->SetValue(IntToString(style.font_weight));
    size_input_->SetValue(IntToString(style.size_px));
    tracking_input_->SetValue(FloatToString(style.letter_spacing));
    leading_input_->SetValue(FloatToString(style.line_spacing));
    x_input_->SetValue(FloatToString(style.x));
    y_input_->SetValue(FloatToString(style.y));
    rotate_input_->SetValue(FloatToString(style.rotate_deg));
    w_input_->SetValue(FloatToString(style.w));
    h_input_->SetValue(FloatToString(style.h));
    radius_input_->SetValue(FloatToString(style.corner_radius));
    baseline_input_->SetValue(FloatToString(style.baseline_px));
    fill_hex_input_->SetValue(RgbaToHex(style.color));
    opacity_slider_->SetValue(std::clamp(style.opacity, 0.0f, 1.0f));
    outline_input_->SetValue(FloatToString(style.outline_px));
    outline_hex_input_->SetValue(RgbaToHex(style.outline_color));
    bg_hex_input_->SetValue(RgbaToHex(style.bg_color));
    shadow_toggle_->SetPressed(style.shadow_explicit);
    shadow_x_input_->SetValue(FloatToString(style.shadow_x));
    shadow_y_input_->SetValue(FloatToString(style.shadow_y));
    shadow_blur_input_->SetValue(FloatToString(style.shadow_blur));
    shadow_hex_input_->SetValue(RgbaToHex(style.shadow_color));
    bold_toggle_->SetPressed(style.font_weight >= 600);
    italic_toggle_->SetPressed(style.italic);
    underline_toggle_->SetPressed(style.underline);
    strike_toggle_->SetPressed(style.strike);
    align_ = std::clamp(style.align, 0, 2);
    valign_ = std::clamp(style.valign, 0, 2);
    SyncButtons(align_group_.get(), align_);
    SyncButtons(valign_group_.get(), valign_);
    MarkPreviewDirty();
    Invalidate();
}

TitleStyle TitleEditor::GetStyle() const {
    TitleStyle out;
    if (text_area_) out.text = text_area_->GetText();
    if (font_combo_) {
        // A family typed without picking a dropdown row still counts —
        // the text field wins over the last selected option's value.
        const std::string typed = font_combo_->GetText();
        out.font_family =
            !typed.empty() ? typed : font_combo_->GetValue();
    }
    out.font_weight = 400;
    if (weight_combo_) {
        const std::string wv = weight_combo_->GetValue();
        const std::string wt = weight_combo_->GetText();
        out.font_weight = ParseIntSafe(
            !wv.empty() ? wv : wt, 400, 100, 900);
    }
    // The B toggle and the weight combo are two views of the same
    // property — the toggle wins when it is pressed.
    if (bold_toggle_ && bold_toggle_->IsPressed() &&
        out.font_weight < 600) {
        out.font_weight = 700;
    }
    out.italic = italic_toggle_ && italic_toggle_->IsPressed();
    out.underline = underline_toggle_ && underline_toggle_->IsPressed();
    out.strike = strike_toggle_ && strike_toggle_->IsPressed();
    out.size_px = size_input_
                      ? ParseIntSafe(size_input_->GetValue(), 64, 1,
                                     1024)
                      : out.size_px;
    out.letter_spacing =
        tracking_input_ ? ParseFloatSafe(tracking_input_->GetValue(),
                                         0.0f, -256.0f, 2048.0f)
                        : 0.0f;
    out.line_spacing =
        leading_input_ ? ParseFloatSafe(leading_input_->GetValue(),
                                        0.0f, 0.0f, 4096.0f)
                       : 0.0f;
    out.x = x_input_ ? ParseFloatSafe(x_input_->GetValue(), 0.0f,
                                      -8192.0f, 8192.0f)
                     : 0.0f;
    out.y = y_input_ ? ParseFloatSafe(y_input_->GetValue(), 0.0f,
                                      -8192.0f, 8192.0f)
                     : 0.0f;
    out.rotate_deg =
        rotate_input_ ? ParseFloatSafe(rotate_input_->GetValue(), 0.0f,
                                       -360.0f, 360.0f)
                      : 0.0f;
    out.w = w_input_ ? ParseFloatSafe(w_input_->GetValue(), 0.0f, 0.0f,
                                      4096.0f)
                     : 0.0f;
    out.h = h_input_ ? ParseFloatSafe(h_input_->GetValue(), 0.0f, 0.0f,
                                      4096.0f)
                     : 0.0f;
    out.corner_radius =
        radius_input_ ? ParseFloatSafe(radius_input_->GetValue(), 0.0f,
                                       0.0f, 2048.0f)
                      : 0.0f;
    out.baseline_px =
        baseline_input_
            ? ParseFloatSafe(baseline_input_->GetValue(), 0.0f,
                             -4096.0f, 4096.0f)
            : 0.0f;
    if (fill_hex_input_) {
        out.color =
            ParseHexRgba(fill_hex_input_->GetValue(), out.color);
    }
    out.opacity =
        opacity_slider_ ? opacity_slider_->GetValue() : 1.0f;
    out.outline_px =
        outline_input_ ? ParseFloatSafe(outline_input_->GetValue(),
                                        0.0f, 0.0f, 512.0f)
                       : 0.0f;
    if (outline_hex_input_) {
        out.outline_color = ParseHexRgba(
            outline_hex_input_->GetValue(), out.outline_color);
    }
    if (bg_hex_input_) {
        out.bg_color =
            ParseHexRgba(bg_hex_input_->GetValue(), out.bg_color);
    }
    out.shadow_explicit =
        shadow_toggle_ && shadow_toggle_->IsPressed();
    if (out.shadow_explicit) {
        out.shadow_x =
            shadow_x_input_
                ? ParseFloatSafe(shadow_x_input_->GetValue(), 0.0f,
                                 -1024.0f, 1024.0f)
                : 0.0f;
        out.shadow_y =
            shadow_y_input_
                ? ParseFloatSafe(shadow_y_input_->GetValue(), 0.0f,
                                 -1024.0f, 1024.0f)
                : 0.0f;
        out.shadow_blur =
            shadow_blur_input_
                ? ParseFloatSafe(shadow_blur_input_->GetValue(), 0.0f,
                                 0.0f, 512.0f)
                : 0.0f;
        if (shadow_hex_input_) {
            out.shadow_color = ParseHexRgba(
                shadow_hex_input_->GetValue(), out.shadow_color);
        }
    }
    out.align = align_;
    out.valign = valign_;
    return out;
}

void TitleEditor::SetFont(const std::string& family, int weight,
                          bool italic) {
    EnsureChildren();
    font_combo_->SetValue(family);
    RefreshWeightOptions();
    weight_combo_->SetValue(IntToString(weight));
    bold_toggle_->SetPressed(weight >= 600);
    italic_toggle_->SetPressed(italic);
    MarkPreviewDirty();
    Invalidate();
}

void TitleEditor::SelectAlign(int index) {
    if (index < 0 || index > 2 || index == align_) return;
    align_ = index;
    SyncButtons(align_group_.get(), align_);
    MarkPreviewDirty();
    Invalidate();
}

void TitleEditor::SelectVAlign(int index) {
    if (index < 0 || index > 2 || index == valign_) return;
    valign_ = index;
    SyncButtons(valign_group_.get(), valign_);
    MarkPreviewDirty();
    Invalidate();
}

void TitleEditor::MarkPreviewDirty() {
    preview_dirty_ = true;
    Invalidate();
}

// Rasterizes the current style at the preview rect's size through the
// real clip renderer — the preview can't drift from what ships.
void TitleEditor::UpdatePreview() {
    preview_w_ = 0;
    preview_h_ = 0;
    const ClipTextStyle style = TitleStyleToClipStyle(GetStyle());
    if (!RenderClipText(style, 540, &preview_frame_) ||
        !preview_frame_.IsValid()) {
        return;
    }
    preview_w_ = preview_frame_.Width();
    preview_h_ = preview_frame_.Height();
}

void TitleEditor::Apply() {
    if (hooks_.on_apply) {
        hooks_.on_apply(GetStyle());
    }
}

void TitleEditor::Cancel() {
    if (hooks_.on_cancel) hooks_.on_cancel();
}

// The panel is tall: rows are laid out top-down with a running cursor
// inside Layout(); PreviewRect mirrors the same arithmetic. Eight
// field rows of 60px (52 + 8 gap) sit between the preview and the
// button strip.
static float PanelHeight() {
    return 14.0f + 26.0f + 8.0f +   // title
           76.0f + 8.0f +           // text field
           74.0f + 8.0f +           // preview
           60.0f * 8.0f +           // field rows
           48.0f + 14.0f;           // buttons + bottom pad
}

Layout::Rect TitleEditor::PanelRect() const {
    const auto bounds = GetBounds();
    const float width = std::min(kPanelWidth, bounds.width - 40.0f);
    const float height =
        std::min(PanelHeight(), bounds.height - 40.0f);
    return {bounds.x + (bounds.width - width) * 0.5f,
            bounds.y + (bounds.height - height) * 0.5f, width, height};
}

Layout::Rect TitleEditor::PreviewRect() const {
    const auto panel = PanelRect();
    return {panel.x + kPanelPad,
            panel.y + 14.0f + 26.0f + 8.0f + 76.0f + 8.0f,
            panel.width - kPanelPad * 2.0f, 74.0f};
}

void TitleEditor::Render(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, VeTheme::kScrim, 0.0f);
    const auto panel = PanelRect();
    renderer.FillRect(panel, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(panel, tokens.border, 1.0f, tokens.radius_large);
    renderer.FillRect({panel.x, panel.y, 4.0f, panel.height},
                      tokens.accent, tokens.radius_large);

    // Live preview: checkerboard board + the style rasterized through
    // the real text pipeline.
    const auto preview = PreviewRect();
    renderer.FillRect(preview, tokens.input, tokens.radius_small);
    renderer.StrokeRect(preview, tokens.border, 1.0f,
                        tokens.radius_small);
    if (preview_dirty_) {
        preview_dirty_ = false;
        UpdatePreview();
    }
    if (preview_w_ > 0 && preview_h_ > 0 &&
        preview_frame_.IsValid()) {
        const float scale =
            std::min(preview.width / preview_w_,
                     preview.height / preview_h_);
        const float dw = preview_w_ * scale;
        const float dh = preview_h_ * scale;
        const Layout::Rect dst{
            preview.x + (preview.width - dw) * 0.5f,
            preview.y + (preview.height - dh) * 0.5f, dw, dh};
        renderer.DrawFrameBgra(preview_frame_.Data(), preview_w_,
                               preview_h_, dst);
    }
}

void TitleEditor::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const auto panel = PanelRect();
    const float px = panel.x + kPanelPad;
    const float pw = panel.width - kPanelPad * 2.0f;
    const float caption_h =
        tokens.font_small * 1.4f + tokens.spacing_small * 0.5f;
    const float row_h = caption_h + 32.0f;

    float y = panel.y + 14.0f;
    title_->Layout({px, y, pw, 26.0f}, tokens);
    y += 26.0f + 8.0f;

    const auto place = [this, &tokens](Widgets::Widget* control, float x,
                              float fy, float w, float h) {
        if (auto* field = FieldHosting(this, control)) {
            field->Layout({x, fy, w, h}, tokens);
        }
    };
    // Lay out a whole field-row: each pair is {control, width share}.
    const auto row = [&](float ry,
                         std::initializer_list<
                             std::pair<Widgets::Widget*, float>>
                             cells) {
        float shares = 0.0f;
        for (const auto& cell : cells) shares += cell.second;
        const float gaps =
            kFieldGap * (static_cast<float>(cells.size()) - 1.0f);
        const float unit = (pw - gaps) / shares;
        float cx = px;
        for (const auto& cell : cells) {
            const float cw = cell.second * unit;
            place(cell.first, cx, ry, cw, row_h);
            cx += cw + kFieldGap;
        }
        return ry + row_h + 8.0f;
    };

    place(text_area_.get(), px, y, pw, caption_h + 56.0f);
    y += 76.0f + 8.0f;
    y += 74.0f + 8.0f;   // preview rect (drawn in Render)

    y = row(y, {{font_combo_.get(), 2.0f}, {weight_combo_.get(), 1.0f}});
    y = row(y,
            {{size_input_.get(), 1.0f},
             {tracking_input_.get(), 1.0f},
             {leading_input_.get(), 1.0f}});
    y = row(y,
            {{align_group_.get(), 1.4f},
             {valign_group_.get(), 1.4f},
             {deco_group_.get(), 1.6f}});
    y = row(y,
            {{x_input_.get(), 1.0f},
             {y_input_.get(), 1.0f},
             {rotate_input_.get(), 1.0f}});
    y = row(y,
            {{w_input_.get(), 1.0f},
             {h_input_.get(), 1.0f},
             {radius_input_.get(), 1.0f},
             {baseline_input_.get(), 1.0f}});
    y = row(y,
            {{fill_hex_input_.get(), 1.0f},
             {opacity_slider_.get(), 2.0f}});
    y = row(y,
            {{outline_input_.get(), 1.0f},
             {outline_hex_input_.get(), 1.0f},
             {bg_hex_input_.get(), 1.0f}});
    y = row(y,
            {{shadow_toggle_.get(), 1.0f},
             {shadow_x_input_.get(), 1.0f},
             {shadow_y_input_.get(), 1.0f},
             {shadow_blur_input_.get(), 1.0f},
             {shadow_hex_input_.get(), 1.4f}});

    const float button_y = panel.y + panel.height - 48.0f;
    cancel_button_->Layout({panel.x + panel.width - kPanelPad - 196.0f,
                            button_y, 94.0f, 34.0f},
                           tokens);
    apply_button_->Layout({panel.x + panel.width - kPanelPad - 94.0f,
                           button_y, 94.0f, 34.0f},
                          tokens);
}

bool TitleEditor::HandleEvent(const XER::UI::Widgets::Event& event) {
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
        event.mouse_button == MouseButton::Left) {
        // Backdrop click outside the card cancels (modal convention).
        if (!PanelRect().Contains(event.position)) {
            Cancel();
            return true;
        }
    }
    // Modal: consume pointer events inside and outside the panel so the
    // workspace below stays inert while the dialog is up.
    return event.type == Event::Type::MouseDown ||
           event.type == Event::Type::MouseUp ||
           event.type == Event::Type::Click;
}

void TitleEditor::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
