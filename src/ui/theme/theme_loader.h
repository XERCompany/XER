#ifndef XER_UI_THEME_THEME_LOADER_H
#define XER_UI_THEME_THEME_LOADER_H

#include <string>

#include "theme.h"
#include "tokens.h"

namespace XER::UI::Theme {

// Loads UI design tokens from a JSON theme file, e.g.
//
//   {
//     "variant": "dark",                  // optional, default "dark"
//     "colors": {
//       "background": "#09090b",
//       "accent":     "#7c5cff",
//       "danger":     "rgba(185,28,28,0.9)"
//     },
//     "metrics": {
//       "radius_small": 4, "font_body": 15
//     }
//   }
//
// Colors accept "#rgb", "#rrggbb", "#rrggbbaa",
// "rgb(r,g,b)", "rgba(r,g,b,a)" (0-255 / 0-1 alpha). Unspecified keys
// inherit from the chosen base variant, so a theme file only overrides
// what it customizes.
class ThemeLoader {
public:
    // Parses `json` into a full Tokens set merged over the base variant.
    // Returns false with a human-readable error on malformed input.
    static bool Parse(const std::string& json, Tokens* tokens_out,
                      Variant* variant_out, std::string* error_out);
    static bool LoadFile(const std::string& path, Tokens* tokens_out,
                         Variant* variant_out,
                         std::string* error_out);

    // Convenience: apply a theme file straight onto a Provider
    // (custom tokens + matching variant).
    static bool ApplyToProvider(Provider& provider,
                                const std::string& path,
                                std::string* error_out);
};

}  // namespace XER::UI::Theme

#endif  // XER_UI_THEME_THEME_LOADER_H
