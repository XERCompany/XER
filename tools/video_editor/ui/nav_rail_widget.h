// CapCut-style left icon rail: a slim vertical strip of tool glyphs
// (media, audio, text, fx, transitions) with settings pinned to the
// bottom. Pure-draw widget — no child controls; icons are vector
// glyphs rendered from theme tokens and hit-tested in HandleEvent.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::VideoEditor::Ui {

class NavRailWidget final : public XER::UI::Widgets::Widget {
public:
    enum class Glyph { Media, Audio, Text, Fx, Trans, Settings };

    struct Item {
        Glyph glyph = Glyph::Media;
        std::string tooltip;
        std::function<void()> action;
        // Optional toggle state — the item renders accent-tinted when
        // the provider returns true (e.g. "media bin open").
        std::function<bool()> is_active;
    };

    NavRailWidget() = default;

    void SetItems(std::vector<Item> top, std::vector<Item> bottom);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double) override {}

    XER::UI::Layout::Size GetMinSize() const override { return {44, 200}; }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {46, 640};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {46, 100000};
    }

private:
    struct Slot {
        Item item;
        XER::UI::Layout::Rect rect;
    };

    // Glyph painters — small geometric marks inside `r`.
    void DrawGlyph(XER::UI::Rendering::Renderer& renderer,
                   const XER::UI::Theme::Tokens& tokens, Glyph glyph,
                   const XER::UI::Layout::Rect& r,
                   XER::UI::Theme::Color color) const;

    std::vector<Slot> top_;
    std::vector<Slot> bottom_;
    int hovered_ = -1;      // >=0 top_ index, <=-2 bottom index, -1 none
    int pressed_ = -1;
};

}  // namespace XER::VideoEditor::Ui
