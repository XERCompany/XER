#include "chain_map_widget.h"

#include <algorithm>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

Theme::Color WithAlpha(Theme::Color color, float alpha) {
    color.a *= alpha;
    return color;
}

} // namespace

ChainMapWidget::ChainMapWidget(std::vector<FxNodeState>* nodes)
    : nodes_(nodes) {}

void ChainMapWidget::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

std::vector<ChainMapWidget::Block> ChainMapWidget::ComputeBlocks(
    const Layout::Rect& area) const {
    std::vector<Block> blocks;
    const std::size_t count = nodes_ != nullptr ? nodes_->size() : 0;
    const std::size_t slots = count + 2; // In + nodes + Out

    constexpr float kBlockH = 34.0f;
    constexpr float kGap = 26.0f;
    constexpr float kMinBlockW = 74.0f;
    const float avail = area.width - kGap * static_cast<float>(slots - 1);
    float block_w = avail / static_cast<float>(slots);
    block_w = std::max(block_w, kMinBlockW);
    // If blocks would overflow, shrink the gaps instead of the blocks.
    const float total = block_w * static_cast<float>(slots) +
                        kGap * static_cast<float>(slots - 1);
    const float start_x = area.x + std::max(0.0f, (area.width - total) * 0.5f);
    const float y = area.y + (area.height - kBlockH) * 0.5f;

    auto push = [&](const std::string& title, bool enabled, bool terminal) {
        const float x = start_x +
            static_cast<float>(blocks.size()) * (block_w + kGap);
        blocks.push_back(Block{Layout::Rect{x, y, block_w, kBlockH},
                               title, enabled, terminal});
    };

    push("In", true, true);
    if (nodes_ != nullptr) {
        for (const auto& node : *nodes_) {
            push(FxRackState::AlgorithmDisplayName(node.algorithm),
                 node.enabled, false);
        }
    }
    push("Out", true, true);
    return blocks;
}

void ChainMapWidget::DrawBlock(Rendering::Renderer& renderer,
                               const Theme::Tokens& tokens,
                               const Block& block) {
    const auto& r = block.rect;
    if (block.terminal) {
        renderer.FillRect(r, WithAlpha(tokens.secondary, 0.8f), tokens.radius_medium);
        renderer.StrokeRect(r, WithAlpha(tokens.border, 0.8f), 1.0f,
                            tokens.radius_medium);
        renderer.DrawText(block.title,
                          Layout::Rect{r.x, r.y + r.height * 0.5f - 7.0f, r.width, 14.0f},
                          Rendering::TextStyle{tokens.muted, tokens.font_small, 600});
        return;
    }

    const auto fill = block.enabled ? tokens.surface
                                    : WithAlpha(tokens.secondary, 0.5f);
    const auto edge = block.enabled ? tokens.accent
                                    : WithAlpha(tokens.border, 0.9f);
    renderer.FillRect(r, fill, tokens.radius_medium);
    renderer.StrokeRect(r, edge, block.enabled ? 1.5f : 1.0f, tokens.radius_medium);
    // Accent tick on the left edge for enabled nodes.
    if (block.enabled) {
        renderer.FillRect(Layout::Rect{r.x, r.y + 4.0f, 3.0f, r.height - 8.0f},
                          tokens.accent, 1.5f);
    }
    const auto text_color = block.enabled ? tokens.foreground : tokens.muted;
    renderer.DrawText(block.title,
                      Layout::Rect{r.x + 8.0f, r.y + r.height * 0.5f - 7.0f,
                                   r.width - 10.0f, 14.0f},
                      Rendering::TextStyle{text_color, tokens.font_small, 500});
}

void ChainMapWidget::Render(Rendering::Renderer& renderer,
                            const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    if (bounds.width < 60.0f || bounds.height < 30.0f) {
        return;
    }

    const auto blocks = ComputeBlocks(bounds);
    if (blocks.empty()) {
        return;
    }

    // Arrows between consecutive blocks.
    for (std::size_t i = 1; i < blocks.size(); ++i) {
        const float x0 = blocks[i - 1].rect.x + blocks[i - 1].rect.width;
        const float x1 = blocks[i].rect.x;
        const float mid_y = blocks[i].rect.y + blocks[i].rect.height * 0.5f;
        if (x1 - x0 < 4.0f) {
            continue;
        }
        renderer.DrawLine({x0 + 2.0f, mid_y}, {x1 - 2.0f, mid_y},
                          WithAlpha(tokens.border, 0.9f), 1.5f);
        // Arrowhead chevron.
        renderer.DrawLine({x1 - 7.0f, mid_y - 4.0f}, {x1 - 2.0f, mid_y},
                          WithAlpha(tokens.border, 0.9f), 1.5f);
        renderer.DrawLine({x1 - 7.0f, mid_y + 4.0f}, {x1 - 2.0f, mid_y},
                          WithAlpha(tokens.border, 0.9f), 1.5f);
    }

    for (const auto& block : blocks) {
        DrawBlock(renderer, tokens, block);
    }

    if (nodes_ != nullptr && nodes_->empty()) {
        renderer.DrawText("no effects — signal passes through",
                          Layout::Rect{bounds.x, bounds.y + bounds.height - 14.0f,
                                       bounds.width, 12.0f},
                          Rendering::TextStyle{WithAlpha(tokens.muted, 0.7f),
                                               tokens.font_small, 400});
    }
}

} // namespace XER::UI::Widgets
