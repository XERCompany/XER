#ifndef XER_UI_WIDGETS_CHAIN_MAP_WIDGET_H
#define XER_UI_WIDGETS_CHAIN_MAP_WIDGET_H

#include <memory>
#include <vector>

#include "widgets/widget.h"
#include "widgets/fx_rack_state.h"

namespace XER::UI::Widgets {

// Read-only "signal flow" map of the FX chain: an Input block, one block
// per effect node in processing order, and an Output block, connected by
// arrows. Disabled nodes render muted. Purely informational — all editing
// stays in the chain card below.
class ChainMapWidget final : public Widget {
public:
    explicit ChainMapWidget(std::vector<FxNodeState>* nodes);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override { return {200.0f, 44.0f}; }
    Layout::Size GetPreferredSize() const override { return {480.0f, 56.0f}; }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

private:
    struct Block {
        Layout::Rect rect;
        std::string title;
        bool enabled = true;
        bool terminal = false; // Input/Output blocks
    };

    std::vector<Block> ComputeBlocks(const Layout::Rect& area) const;
    void DrawBlock(Rendering::Renderer& renderer, const Theme::Tokens& tokens,
                   const Block& block);

    std::vector<FxNodeState>* nodes_ = nullptr;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_CHAIN_MAP_WIDGET_H
