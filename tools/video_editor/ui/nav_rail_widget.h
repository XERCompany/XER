// CapCut-style left icon rail: a slim vertical strip of tool buttons
// (media, audio, text, fx, transitions) with settings pinned to the
// bottom. Each slot is a Widgets::Button — Ghost style, icon-only via
// SetIcon with the pack's nav.* names — while the rail itself draws
// the panel chrome and owns the slot geometry. The active slot's
// button flips to Primary (accent) through Item::is_active.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
}

namespace XER::VideoEditor::Ui {

class NavRailWidget final : public XER::UI::Widgets::Widget {
public:
    enum class Glyph { Media, Audio, Text, Fx, Trans, Settings };

    struct Item {
        Glyph glyph = Glyph::Media;
        std::string tooltip;
        std::function<void()> action;
        // Optional toggle state — the slot's button renders Primary
        // when the provider returns true (e.g. "media bin open").
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
        std::shared_ptr<XER::UI::Widgets::Button> button;
    };

    std::vector<Slot> top_;
    std::vector<Slot> bottom_;
};

}  // namespace XER::VideoEditor::Ui
