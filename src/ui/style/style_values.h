#ifndef XER_UI_STYLE_STYLE_VALUES_H
#define XER_UI_STYLE_STYLE_VALUES_H

#include <optional>
#include <string_view>

#include "theme/color.h"
#include "theme/tokens.h"

namespace XER::Content::CSS {
class ComputedStyle;
}

namespace XER::UI::CSS {

// CSS value parsers + theme mapping used by WidgetStyleHost consumers.
// Supported:
//   colors  — #rgb, #rgba, #rrggbb, #rrggbbaa, rgb()/rgba(), and the
//             common named colors (transparent, white, black, red...).
//   lengths — plain number (px), px, pt (×96/72), em/rem (×em_base).
std::optional<Theme::Color> ParseColor(std::string_view value);
std::optional<float> ParseLength(std::string_view value,
                                 float em_base = 16.0f);

// Maps design-token CSS properties onto Theme::Tokens (call from a
// WidgetStyleHost apply callback on the root widget, or on any widget
// overriding theme metrics). Recognized properties:
//   background-color, color|foreground, surface, muted, panel,
//   border-color, accent-color, accent-foreground, focus-ring-color,
//   secondary, hover, ring, danger-color, danger-foreground, input,
//   pressed, disabled, selection, radius, radius-small, radius-large,
//   spacing-small|medium|large, font-size|font-body, font-small,
//   font-title.
void ApplyComputedToTokens(const Content::CSS::ComputedStyle& style,
                           Theme::Tokens* tokens);

}  // namespace XER::UI::CSS

#endif  // XER_UI_STYLE_STYLE_VALUES_H
