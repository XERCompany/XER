// Modal "New Color Matte" dialog for the XER video editor —
// Premiere's File > New > Color Matte prompt. Same overlay pattern as
// SpeedDialog/SettingsDialog: dimmed backdrop, centered card,
// Escape/backdrop cancel, Enter applies.
//
// The dialog owns no project state: the shell pushes defaults through
// SetDefaults() before showing it and reads the validated result out
// of the on_apply hook (name, #RRGGBB[AA] color, duration). The name
// lands on Clip::name; the color hex on Clip::source.param — the same
// payload generated_source.cc's ParseMatteColor renders.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
class TextInput;
}

namespace XER::VideoEditor::Ui {

// Validated dialog output — all three fields are already sanitized
// when on_apply fires (color normalized to "#RRGGBB[AA]", duration
// inside a sane positive range).
struct NewLayerSpec {
    std::string name;           // Clip::name ("" = kind default)
    std::string color_hex;      // "#RRGGBB" or "#RRGGBBAA"
    double duration_seconds = 5.0;
};

class NewLayerDialog final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Fired by Add with the validated spec. The host creates the
        // clip and hides the overlay.
        std::function<void(const NewLayerSpec&)> on_apply;
        // Fired by Cancel / Escape / backdrop click. Optional — the
        // host usually just hides the overlay.
        std::function<void()> on_cancel;
    };

    explicit NewLayerDialog(Hooks hooks = {});

    // Fills the fields — call before showing. `default_hex` may be
    // "#RRGGBB" or "#RRGGBBAA" (anything else falls back to the
    // standard matte blue).
    void SetDefaults(const std::string& name,
                     const std::string& default_hex,
                     double duration_seconds);

    // Current field values (validated/clamped; never throws).
    NewLayerSpec GetSpec() const;
    // True while the color parses as #RRGGBB[AA] and the duration is
    // a positive finite number. Invalid fields show a red hint and
    // disable the Add button.
    bool Valid() const;

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    void EnsureChildren();
    void Apply();
    void Cancel();
    // Recomputes the swatch preview, the error lines and Add
    // enablement from the current field contents.
    void RefreshValidation();
    // Strict validator matching ParseMatteColor's accepted grammar
    // (#-optional, 6 or 8 hex digits) — the swatch still previews via
    // the lenient parser while the input is malformed.
    static bool IsMatteHex(const std::string& text);
    // "#RRGGBB" / "#RRGGBBAA" for a hex input that IsMatteHex()
    // accepts (adds the '#', uppercases); "" otherwise.
    static std::string NormalizeHex(const std::string& text);

    XER::UI::Layout::Rect PanelRect() const;
    // Color swatch preview next to the hex input — drawn by Render().
    XER::UI::Layout::Rect SwatchRect() const;

    Hooks hooks_;
    bool children_attached_ = false;
    // Re-entrancy guard for SetDefaults-driven SetValue callbacks.
    bool syncing_fields_ = false;
    std::string hint_text_;

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::TextInput> name_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> color_input_;
    std::shared_ptr<XER::UI::Widgets::TextInput> duration_input_;
    std::shared_ptr<XER::UI::Widgets::Button> apply_button_;
    std::shared_ptr<XER::UI::Widgets::Button> cancel_button_;
};

}  // namespace XER::VideoEditor::Ui
