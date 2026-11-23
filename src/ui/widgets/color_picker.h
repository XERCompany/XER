#ifndef XER_UI_WIDGETS_COLOR_PICKER_H
#define XER_UI_WIDGETS_COLOR_PICKER_H

// OKLCH-canonical color picker — the Colorshot-style parts kit rendered as
// one compound widget (eq_editor self-draw precedent):
//
//   [HSV|HSL|RGB|OKLCH]   mode tabs — pick the editing space
//   +-----------------+   2D color field (axes depend on mode:
//   |                 |   HSV: SxV · HSL: SxL · RGB: GxB · OKLCH: CxL)
//   |     [x]         |[] channel slider (hue, or R in RGB mode)
//   +-----------------+   alpha slider over a checkerboard
//   [swatch][hex][ch0][ch1][ch2]   preview + numeric inputs
//   [palette swatches…]           fixed presets
//   [recent swatches…]            MRU list, fed on commit
//
// Canonical state is ColorSpace::Oklch — unbounded chroma survives
// round-trips; sRGB is derived on demand (Amplo architecture). SetColor()
// takes/returns gamma-encoded sRGB Theme::Color; SetColorOklch() speaks the
// canonical space directly.
//
// Callback semantics (Colorshot onChange vs onChangeComplete):
//   on_change    — every move while dragging / each accepted keystroke;
//                  use it to live-preview against the compositor.
//   on_committed — once, when the gesture ends (drag release, Enter in an
//                  input, swatch click); this is where the undo stack /
//                  document write belongs. Committed colors are pushed onto
//                  the recent list automatically (SetAutoRecentOnCommit).

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "colorspace/oklch.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

class TextInput;

class ColorPicker final : public Widget {
public:
    // Editing space for the field/tab strip. OKLCH is listed last to match
    // the tab order; it is still the canonical storage for every mode.
    enum class Mode { Hsv, Hsl, Rgb, Oklch };

    ColorPicker();

    // sRGB in/out. SetColor stores the color as OKLCH internally; hue is
    // preserved across achromatic edits (c ~ 0 leaves h undefined).
    void SetColor(Theme::Color color);
    Theme::Color GetColor() const;
    void SetColorOklch(const ColorSpace::Oklch& color);
    ColorSpace::Oklch GetColorOklch() const;

    void SetMode(Mode mode);
    Mode GetMode() const { return mode_; }
    // False hides the alpha slider and drops the AA byte from the hex
    // field (Colorshot `alpha={false}`).
    void SetShowAlpha(bool show);
    bool ShowAlpha() const { return show_alpha_; }

    // Live vs committed callbacks — see the file header.
    void SetOnColorChange(std::function<void(Theme::Color)> callback);
    void SetOnColorCommitted(std::function<void(Theme::Color)> callback);

    // Preset swatch strip (Colorshot `Swatches`/Amplo `Swatches`).
    void SetPalette(std::vector<Theme::Color> colors);
    const std::vector<Theme::Color>& Palette() const { return palette_; }
    // Most-recently-used strip: newest first, exact-duplicate collapsed,
    // capped at MaxRecent().
    void SetRecent(std::vector<Theme::Color> colors);
    void AddRecent(Theme::Color color);
    const std::vector<Theme::Color>& Recent() const { return recent_; }
    void SetMaxRecent(int max);
    int MaxRecent() const { return max_recent_; }
    // Auto-push committed colors into Recent() (default on).
    void SetAutoRecentOnCommit(bool enabled) { auto_recent_ = enabled; }

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    // Sub-region geometry derived from GetBounds() + current flags.
    // Recomputed on demand so Render and HandleEvent never drift apart.
    struct Chrome {
        Layout::Rect tabs;
        Layout::Rect field;
        Layout::Rect channel;      // vertical slider right of the field
        Layout::Rect alpha;        // empty when alpha hidden
        Layout::Rect preview;
        Layout::Rect hex_input;
        Layout::Rect ch_input[3];
        Layout::Rect palette;      // empty when palette_ is empty
        Layout::Rect recent;       // empty when recent_ is empty
    };
    enum class Drag { None, Field, Channel, Alpha };

    Chrome GetChrome() const;
    int HitTab(const Layout::Point& p) const;
    int HitSwatch(const Layout::Point& p, const Layout::Rect& row,
                  std::size_t count) const;

    // Mode-space mapping between the field axes / channel slider and color.
    Theme::Color ColorForFieldPoint(float fx, float fy) const;
    Layout::Point FieldPointForColor() const;
    Theme::Color ColorForChannelValue(float t) const;
    float ChannelValueForColor() const;
    Theme::Color ColorForAlphaValue(float t) const;
    // The three labeled numeric channels for the active mode
    // (HSV/HSV/RGB/LCH), values out written in display units.
    void ChannelLabels(const char* (&labels)[3], float (&ranges)[3]) const;
    void ChannelValues(float (&values)[3]) const;
    Theme::Color ColorForChannelValues(const float (&values)[3]) const;

    // Applies `color` (sRGB) as the new canonical state. `live` fires
    // on_change; the matching Commit() ends the gesture.
    void ApplyColor(Theme::Color color, bool live);
    void Commit();
    void SyncInputs();          // push current values into text inputs
    void MarkFieldDirty() { field_dirty_ = true; channel_dirty_ = true; }
    void EnsureChildren();

    // Raster helpers (BGRA, drawn via Renderer::DrawFrameBgra).
    void RasterizeField(int w, int h, std::vector<std::uint8_t>* out) const;
    void RasterizeChannel(int w, int h, std::vector<std::uint8_t>* out) const;
    void RasterizeAlphaRamp(int w, int h, std::vector<std::uint8_t>* out) const;

    ColorSpace::Oklch color_{};          // canonical state
    Mode mode_ = Mode::Hsv;
    bool show_alpha_ = true;
    bool auto_recent_ = true;
    int max_recent_ = 14;

    Drag drag_ = Drag::None;
    bool field_dirty_ = true;
    bool channel_dirty_ = true;
    std::vector<std::uint8_t> field_raster_;
    std::vector<std::uint8_t> channel_raster_;
    int field_raster_w_ = 0, field_raster_h_ = 0;
    int channel_raster_h_ = 0;

    std::vector<Theme::Color> palette_;
    std::vector<Theme::Color> recent_;

    std::shared_ptr<TextInput> hex_input_;
    std::shared_ptr<TextInput> ch_input_[3];
    bool inputs_ready_ = false;
    // Guards re-entrant SyncInputs while an input callback is running.
    bool updating_inputs_ = false;

    std::function<void(Theme::Color)> on_change_;
    std::function<void(Theme::Color)> on_committed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_COLOR_PICKER_H
