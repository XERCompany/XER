#include "color_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "rendering/renderer.h"
#include "shell_widgets.h"    // LinearBox
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/card.h"
#include "widgets/color_picker.h"
#include "widgets/label.h"
#include "widgets/scroll_area.h"
#include "widgets/slider.h"

// windows.h may have re-defined DrawText before we got here.
#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;

constexpr float kWheelPx = 76.0f;   // disc diameter incl. ring

// ---- ColorWheel ------------------------------------------------------
// The M3 mini-wheel: a locally-drawn hue disc with a draggable offset
// dot (the spec's "simple implementation is explicitly acceptable"
// wheel). The disc raster is generated once per pixel size and cached;
// the dot is two rounded FillRects on top. Dragging emits live
// on_change (x,y in [-1,1], +x warm / +y magenta, clamped to the unit
// disc); release emits on_committed once so the host can bracket the
// gesture in a history macro.
class ColorWheel final : public W::Widget {
public:
    ColorWheel() { SetFocusable(true); }

    void SetOffset(float x, float y) {
        offset_x_ = std::clamp(x, -1.0f, 1.0f);
        offset_y_ = std::clamp(y, -1.0f, 1.0f);
        Invalidate();
    }
    std::pair<float, float> Offset() const {
        return {offset_x_, offset_y_};
    }
    void SetOnChange(std::function<void(float, float)> cb) {
        on_change_ = std::move(cb);
    }
    void SetOnCommitted(std::function<void()> cb) {
        on_commit_ = std::move(cb);
    }

    void Render(Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        const Layout::Rect b = GetBounds();
        const float cx = b.x + b.width / 2.0f;
        const float cy = b.y + b.height / 2.0f;
        const float r = std::min(b.width, b.height) / 2.0f;
        if (r <= 4.0f) return;
        EnsureDisc(std::max(8, static_cast<int>(b.width)));
        if (!disc_.empty()) {
            renderer.DrawFrameBgra(
                disc_.data(), disc_px_, disc_px_,
                {cx - r, cy - r, r * 2.0f, r * 2.0f});
        }
        renderer.StrokeRect({cx - r, cy - r, r * 2.0f, r * 2.0f},
                            tokens.border, 1.0f, r);
        // Offset dot — white core over a dark ring so it reads on any
        // disc hue.
        const float dot_r = r - 5.0f;
        const float dx = cx + offset_x_ * dot_r;
        const float dy = cy - offset_y_ * dot_r;
        renderer.FillRect({dx - 4.0f, dy - 4.0f, 8.0f, 8.0f},
                          {0.0f, 0.0f, 0.0f, 0.85f}, 4.0f);
        renderer.FillRect({dx - 2.5f, dy - 2.5f, 5.0f, 5.0f},
                          {1.0f, 1.0f, 1.0f, 1.0f}, 2.5f);
    }

    void Layout(const Layout::Rect& bounds) override {
        SetBounds(bounds);
    }

    bool HandleEvent(const W::Event& event) override {
        switch (event.type) {
            case W::Event::Type::MouseDown:
                if (event.mouse_button == W::MouseButton::Left &&
                    InsideDisc(event.position)) {
                    dragging_ = true;
                    UpdateOffset(event.position);
                    return true;
                }
                return false;
            case W::Event::Type::MouseMove:
                if (dragging_) {
                    UpdateOffset(event.position);
                    return true;
                }
                return false;
            case W::Event::Type::MouseUp:
                if (dragging_) {
                    dragging_ = false;
                    if (on_commit_) on_commit_();
                    return true;
                }
                return false;
            default:
                return false;
        }
    }

    Layout::Size GetMinSize() const override {
        return {48.0f, 48.0f};
    }
    Layout::Size GetPreferredSize() const override {
        return {kWheelPx, kWheelPx};
    }
    Layout::Size GetMaxSize() const override {
        return {kWheelPx * 2.0f, kWheelPx * 2.0f};
    }

private:
    Layout::Point DiscCenter() const {
        const Layout::Rect b = GetBounds();
        return {b.x + b.width / 2.0f, b.y + b.height / 2.0f};
    }
    float DiscRadius() const {
        const Layout::Rect b = GetBounds();
        return std::min(b.width, b.height) / 2.0f;
    }
    bool InsideDisc(Layout::Point p) const {
        const Layout::Point c = DiscCenter();
        const float dx = p.x - c.x, dy = p.y - c.y;
        return dx * dx + dy * dy <= DiscRadius() * DiscRadius();
    }
    void UpdateOffset(Layout::Point p) {
        const Layout::Point c = DiscCenter();
        const float r = std::max(1.0f, DiscRadius());
        float nx = (p.x - c.x) / r;
        float ny = (c.y - p.y) / r;   // +y up = magenta side
        const float len = std::hypot(nx, ny);
        if (len > 1.0f) {
            nx /= len;
            ny /= len;
        }
        SetOffset(nx, ny);
        if (on_change_) on_change_(offset_x_, offset_y_);
    }

    // Rasterize the hue disc once per pixel size. +x -> red (warm
    // axis), +y -> magenta, matching the param-axis docs; chroma
    // grows to the rim, the disc fades to transparent outside r=1.
    void EnsureDisc(int px) {
        if (px == disc_px_ && !disc_.empty()) return;
        disc_px_ = px;
        disc_.assign(static_cast<std::size_t>(px) * px * 4, 0);
        const float half = px / 2.0f;
        for (int y = 0; y < px; ++y) {
            for (int x = 0; x < px; ++x) {
                const float dx = (x + 0.5f - half) / half;
                const float dy = (half - y - 0.5f) / half;
                const float rad = std::hypot(dx, dy);
                if (rad > 1.0f) continue;
                const float hue =
                    std::fmod(std::atan2(dy, dx) * 57.2957795f +
                                  360.0f,
                              360.0f);
                // HSV->RGB, S = radius, V = 1 — cheap wheel look.
                const int sector = static_cast<int>(hue / 60.0f) % 6;
                const float f = hue / 60.0f -
                                static_cast<float>(
                                    static_cast<int>(hue / 60.0f));
                const float q = 1.0f - rad * f;
                const float t = 1.0f - rad * (1.0f - f);
                const float v = 1.0f;
                float rr = 0.0f, gg = 0.0f, bb = 0.0f;
                switch (sector) {
                    case 0: rr = v; gg = t; bb = 1.0f - rad; break;
                    case 1: rr = q; gg = v; bb = 1.0f - rad; break;
                    case 2: rr = 1.0f - rad; gg = v; bb = t; break;
                    case 3: rr = 1.0f - rad; gg = q; bb = v; break;
                    case 4: rr = t; gg = 1.0f - rad; bb = v; break;
                    default: rr = v; gg = 1.0f - rad; bb = q; break;
                }
                const float edge =
                    std::clamp((1.0f - rad) * half, 0.0f, 1.0f);
                auto* dst =
                    disc_.data() +
                    (static_cast<std::size_t>(y) * px + x) * 4;
                dst[0] = static_cast<std::uint8_t>(
                    std::clamp(bb, 0.0f, 1.0f) * 255.0f);
                dst[1] = static_cast<std::uint8_t>(
                    std::clamp(gg, 0.0f, 1.0f) * 255.0f);
                dst[2] = static_cast<std::uint8_t>(
                    std::clamp(rr, 0.0f, 1.0f) * 255.0f);
                dst[3] = static_cast<std::uint8_t>(edge * 255.0f);
            }
        }
    }

    float offset_x_ = 0.0f, offset_y_ = 0.0f;
    bool dragging_ = false;
    std::function<void(float, float)> on_change_;
    std::function<void()> on_commit_;
    std::vector<std::uint8_t> disc_;
    int disc_px_ = 0;
};

// Row labels in wheel order (shadows/midtones/highlights).
const char* kWheelNames[3] = {"Shadows", "Midtones", "Highlights"};

// Scope chip labels — ScopesPanel::Mode order.
const char* kScopeNames[4] = {"WFM", "VEC", "HST", "PAR"};

}  // namespace

std::shared_ptr<ColorPanel> ColorPanel::Create() {
    return std::shared_ptr<ColorPanel>(new ColorPanel());
}

ColorPanel::ColorPanel() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void ColorPanel::SetHooks(ColorHooks hooks) {
    hooks_ = std::move(hooks);
}

void ColorPanel::SetClipProvider(std::function<Clip()> provider) {
    clip_provider_ = std::move(provider);
    RefreshClip();
}

// ---- commit funnel ----------------------------------------------------

void ColorPanel::EmitChanged() {
    if (clip_id_.empty()) return;
    if (hooks_.on_color_changed) {
        hooks_.on_color_changed(clip_id_, params_);
    }
}

void ColorPanel::BeginGesture() {
    if (in_gesture_) return;
    in_gesture_ = true;
    if (hooks_.on_begin_macro) hooks_.on_begin_macro();
}

void ColorPanel::EndGesture() {
    if (!in_gesture_) return;
    in_gesture_ = false;
    if (hooks_.on_end_macro) hooks_.on_end_macro();
}

// ---- verbs --------------------------------------------------------------

void ColorPanel::SetWheelOffset(int wheel, float offset_x,
                                float offset_y) {
    if (wheel < 0 || wheel > 2) return;
    auto& w = params_.wheels[static_cast<std::size_t>(wheel)];
    w.offset_x = std::clamp(offset_x, -1.0f, 1.0f);
    w.offset_y = std::clamp(offset_y, -1.0f, 1.0f);
    EmitChanged();
}

void ColorPanel::SetWheelLevel(int wheel, float level) {
    if (wheel < 0 || wheel > 2) return;
    params_.wheels[static_cast<std::size_t>(wheel)].level =
        std::clamp(level, -1.0f, 1.0f);
    EmitChanged();
}

void ColorPanel::ResetWheel(int wheel) {
    if (wheel < 0 || wheel > 2) return;
    auto& w = params_.wheels[static_cast<std::size_t>(wheel)];
    if (w.offset_x == 0.0f && w.offset_y == 0.0f &&
        w.level == 0.0f) {
        return;   // already neutral — don't emit a no-op commit
    }
    w = ColorWheelState{};
    SyncControls();
    EmitChanged();
}

void ColorPanel::ResetAll() {
    params_ = ColorParams{};
    SyncControls();
    EmitChanged();
}

void ColorPanel::ApplyTemperatureTint(float temperature, float tint) {
    params_.temperature = std::clamp(temperature, -100.0f, 100.0f);
    params_.tint = std::clamp(tint, -100.0f, 100.0f);
    EmitChanged();
}

// The temperature/tint fx-param mapping shared by the OKLCH picker
// (picked color IS the cast) and the white-balance eyedropper
// (picked color should BECOME neutral — neutralize inverts it).
// Builtin kernels (builtin_fx.cc): temperature p0 -100..100 shifts
// R += p0*0.6, B -= p0*0.6 (in 0..255 units); tint p0 shifts
// R += d, G -= d, B += d with d = p0*0.5. Deriving the params for a
// desired cast (r,g,b) around neutral n = (r+b)/2:
//   temperature: R += (r-b)/2, B -= (r-b)/2  -> p0 = (r-b)*255/1.2
//   tint:        G += (g-n) -> -d = g-n      -> p0 = ((r+b)-2g)*255
std::pair<float, float> ColorPanel::TempTintFromColor(
    float r, float g, float b, bool neutralize) {
    const float sign = neutralize ? -1.0f : 1.0f;
    const float temp =
        sign * (r - b) * 255.0f / 1.2f;
    const float tint =
        sign * ((r + b) - 2.0f * g) * 255.0f;
    return {std::clamp(temp, -100.0f, 100.0f),
            std::clamp(tint, -100.0f, 100.0f)};
}

// ---- eyedropper seam -----------------------------------------------------

void ColorPanel::ArmWhiteBalance() {
    if (pick_armed_) return;
    if (clip_id_.empty()) {
        if (hooks_.on_status) {
            hooks_.on_status("Select a clip before picking white "
                             "balance",
                             true);
        }
        return;
    }
    pick_armed_ = true;
    if (hooks_.on_arm_pick) hooks_.on_arm_pick();
    if (hooks_.on_status) {
        hooks_.on_status("White balance: click a neutral region in "
                         "the program monitor",
                         false);
    }
}

void ColorPanel::ApplyPickedColor(XER::UI::Theme::Color color) {
    pick_armed_ = false;
    auto [temp, tint] =
        TempTintFromColor(color.r, color.g, color.b,
                          /*neutralize=*/true);
    ApplyTemperatureTint(temp, tint);
    if (hooks_.on_status) {
        hooks_.on_status("White balance applied", false);
    }
}

void ColorPanel::CancelPick() {
    pick_armed_ = false;
}

// ---- selection -----------------------------------------------------------

void ColorPanel::RefreshClip() {
    const Clip clip = clip_provider_ ? clip_provider_() : Clip{};
    const bool target_changed = clip.id != clip_id_;
    clip_id_ = clip.id;
    if (target_changed) {
        RebuildFromClip(clip);
        SyncControls();
    }
    if (title_ != nullptr) {
        title_->SetText(clip_id_.empty()
                            ? "Color — no clip selected"
                            : "Color — " + (clip.name.empty()
                                                ? clip.id
                                                : clip.name));
    }
    Invalidate();
}

void ColorPanel::RebuildFromClip(const Clip& clip) {
    // Read back the fields the seam owns. Levels + temp/tint are
    // invertible; the folded wheel offsets aren't (channel_mixer
    // gains can't be unmixed into a per-band vector) — wheels keep
    // the panel's view state, which is exactly what wrote those
    // gains for panel-graded clips.
    params_ = ColorParams{};
    for (const ClipEffect& fx : clip.effects) {
        if (fx.type == "builtin:xer.fx.temperature" &&
            !fx.params.empty()) {
            params_.temperature = fx.params[0];
        } else if (fx.type == "builtin:xer.fx.tint" &&
                   !fx.params.empty()) {
            params_.tint = fx.params[0];
        } else if (fx.type == "builtin:xer.fx.lift_gamma_gain" &&
                   fx.params.size() >= 3u) {
            params_.wheels[0].level =
                std::clamp(fx.params[0], -1.0f, 1.0f);
            // gamma = 2^level -> level = log2(gamma)
            params_.wheels[1].level = std::clamp(
                std::log2(std::max(0.05f, fx.params[1])), -1.0f,
                1.0f);
            params_.wheels[2].level = std::clamp(fx.params[2] - 1.0f,
                                                 -1.0f, 1.0f);
        }
    }
}

// ---- controls ---------------------------------------------------------

void ColorPanel::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    title_ = std::make_shared<W::Label>("Color — no clip selected",
                                        true);
    title_->SetTooltip("The clip the color controls commit to");

    auto column = LinearBox::Create(LinearBox::Direction::Vertical);
    column->SetSpacing(Theme::kSpaceSm);
    column_ = column;
    scroll_ = std::make_shared<W::ScrollArea>(column_);

    const auto make_card = [](const char* title) {
        auto card = std::make_shared<W::Card>();
        card->SetTitle(title);
        card->SetCollapsible(true);
        card->SetDensity(W::Card::Density::Compact);
        return card;
    };

    // ---- TINT card: OKLCH picker + white-balance eyedropper ----
    tint_card_ = make_card("Tint & White Balance");
    {
        auto col =
            LinearBox::Create(LinearBox::Direction::Vertical);
        col->SetSpacing(Theme::kSpaceXs);
        picker_ = std::make_shared<W::ColorPicker>();
        picker_->SetMode(W::ColorPicker::Mode::Oklch);
        picker_->SetShowAlpha(false);
        picker_->SetTooltip(
            "Color cast applied to the clip (temperature/tint axes)");
        picker_->SetOnColorChange([this](XER::UI::Theme::Color c) {
            if (syncing_) return;
            BeginGesture();
            auto [t, ti] = TempTintFromColor(c.r, c.g, c.b,
                                           /*neutralize=*/false);
            ApplyTemperatureTint(t, ti);
        });
        picker_->SetOnColorCommitted(
            [this](XER::UI::Theme::Color) { EndGesture(); });
        pick_btn_ = std::make_shared<W::Button>(
            "Pick white balance", W::ButtonStyle::Secondary);
        pick_btn_->SetSize(W::ButtonSize::Small);
        pick_btn_->SetTooltip(
            "Arm the program monitor — click a neutral region to "
            "neutralize its cast");
        pick_btn_->SetOnClick([this] { ArmWhiteBalance(); });
        col->Add(picker_, LinearBox::Slot{0.0f, 0.0f});
        col->Add(pick_btn_, LinearBox::Slot{0.0f, 0.0f});
        tint_card_->SetContent(col);
    }

    // ---- WHEELS card: three wheel+slider+reset triples ----
    wheels_card_ = make_card("Lift / Gamma / Gain");
    {
        auto col =
            LinearBox::Create(LinearBox::Direction::Vertical);
        col->SetSpacing(Theme::kSpaceSm);
        for (int i = 0; i < 3; ++i) {
            auto wheel = std::make_shared<ColorWheel>();
            wheels_[static_cast<std::size_t>(i)] = wheel;
            const int index = i;
            wheel->SetOnChange([this, index](float x, float y) {
                if (syncing_) return;
                BeginGesture();
                SetWheelOffset(index, x, y);
            });
            wheel->SetOnCommitted(
                [this] { EndGesture(); });

            levels_[static_cast<std::size_t>(i)] =
                std::make_shared<W::Slider>();
            auto* slider = levels_[static_cast<std::size_t>(i)].get();
            slider->SetRange(-1.0f, 1.0f);
            slider->SetTooltip(
                "Band luminance (lift/gamma/gain)");
            slider->SetOnValueChanged([this, index](float v) {
                if (syncing_) return;
                BeginGesture();
                SetWheelLevel(index, v);
                EndGesture();   // Slider has no committed hook —
                                // ReplaceClip coalescing covers it
            });

            resets_[static_cast<std::size_t>(i)] =
                std::make_shared<W::Button>(
                    "Reset", W::ButtonStyle::Ghost);
            auto* reset = resets_[static_cast<std::size_t>(i)].get();
            reset->SetSize(W::ButtonSize::Small);
            reset->SetTooltip("Reset this wheel");
            reset->SetOnClick([this, index] { ResetWheel(index); });

            auto name = std::make_shared<W::Label>(
                kWheelNames[i], true);

            auto side = LinearBox::Create(
                LinearBox::Direction::Vertical);
            side->SetSpacing(Theme::kSpaceXxs);
            auto name_row = LinearBox::Create(
                LinearBox::Direction::Horizontal);
            name_row->SetSpacing(Theme::kSpaceXs);
            name_row->Add(name, LinearBox::Slot{0.0f, 1.0f});
            name_row->Add(
                resets_[static_cast<std::size_t>(i)],
                LinearBox::Slot{0.0f, 0.0f});
            side->Add(name_row, LinearBox::Slot{0.0f, 0.0f});
            side->Add(levels_[static_cast<std::size_t>(i)],
                      LinearBox::Slot{0.0f, 0.0f});

            auto row = LinearBox::Create(
                LinearBox::Direction::Horizontal);
            row->SetSpacing(Theme::kSpaceSm);
            row->Add(wheel, LinearBox::Slot{kWheelPx, 0.0f});
            row->Add(side, LinearBox::Slot{0.0f, 1.0f});
            col->Add(row, LinearBox::Slot{kWheelPx, 0.0f});
        }
        wheels_card_->SetContent(col);
    }

    // ---- SCOPES card: mode chips -> shell seam ----
    scopes_card_ = make_card("Scopes");
    {
        auto row =
            LinearBox::Create(LinearBox::Direction::Horizontal);
        row->SetSpacing(Theme::kSpaceXs);
        for (int i = 0; i < 4; ++i) {
            scope_btns_[static_cast<std::size_t>(i)] =
                std::make_shared<W::Button>(
                    kScopeNames[i], W::ButtonStyle::Secondary);
            auto* btn = scope_btns_[static_cast<std::size_t>(i)].get();
            btn->SetSize(W::ButtonSize::Small);
            btn->SetTooltip(
                "Show this scope in the Scopes dock tab");
            const int index = i;
            btn->SetOnClick([this, index] {
                if (hooks_.on_scope_mode) {
                    hooks_.on_scope_mode(index);
                }
            });
            row->Add(scope_btns_[static_cast<std::size_t>(i)],
                     LinearBox::Slot{0.0f, 1.0f});
        }
        scopes_card_->SetContent(row);
    }

    column->Add(tint_card_, LinearBox::Slot{0.0f, 0.0f});
    column->Add(wheels_card_, LinearBox::Slot{0.0f, 0.0f});
    column->Add(scopes_card_, LinearBox::Slot{0.0f, 0.0f});

    AddChild(title_);
    AddChild(scroll_);
}

void ColorPanel::SyncControls() {
    if (!children_attached_) return;
    syncing_ = true;
    const auto restore = [this] { syncing_ = false; };
    for (int i = 0; i < 3; ++i) {
        const auto& w = params_.wheels[static_cast<std::size_t>(i)];
        if (auto* wheel =
                static_cast<ColorWheel*>(wheels_[static_cast<std::size_t>(i)].get())) {
            wheel->SetOffset(w.offset_x, w.offset_y);
        }
        if (levels_[static_cast<std::size_t>(i)] != nullptr) {
            levels_[static_cast<std::size_t>(i)]->SetValue(w.level);
        }
    }
    // The picker previews the cast the params describe — rebuild an
    // approximate sRGB color from temp/tint by inverting the axis
    // mapping (half-scale on each axis — the axes overlap so an
    // exact inverse is impossible; documented approximation).
    if (picker_ != nullptr) {
        const float d_r =
            params_.temperature * 1.2f / 255.0f / 2.0f +
            params_.tint * 0.5f / 255.0f;
        const float d_g = -params_.tint * 0.5f / 255.0f;
        const float d_b =
            -params_.temperature * 1.2f / 255.0f / 2.0f +
            params_.tint * 0.5f / 255.0f;
        XER::UI::Theme::Color c{
            std::clamp(0.5f + d_r, 0.0f, 1.0f),
            std::clamp(0.5f + d_g, 0.0f, 1.0f),
            std::clamp(0.5f + d_b, 0.0f, 1.0f), 1.0f};
        picker_->SetColor(c);
    }
    restore();
}

W::Widget* ColorPanel::WheelSurface(int wheel) const {
    if (wheel < 0 || wheel > 2) return nullptr;
    return wheels_[static_cast<std::size_t>(wheel)].get();
}

// ---- widget contract ----------------------------------------------------

void ColorPanel::Render(Rendering::Renderer& renderer,
                        const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
}

void ColorPanel::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float x = bounds.x + Theme::kSpaceSm;
    const float w = bounds.width - Theme::kSpaceSm * 2.0f;
    float y = bounds.y + Theme::kSpaceSm;
    title_->Layout({x, y, w, 20.0f}, tokens);
    y += 20.0f + Theme::kSpaceXs;
    scroll_->Layout(
        {bounds.x + Theme::kSpaceXs, y,
         bounds.width - Theme::kSpaceSm,
         bounds.y + bounds.height - Theme::kSpaceSm - y},
        tokens);
}

void ColorPanel::Tick(double /*dt_seconds*/) {
    // Nothing to drain — commits are synchronous through hooks and
    // refresh arrives via RefreshClip (provider re-pull).
}

}  // namespace XER::VideoEditor::Ui2
