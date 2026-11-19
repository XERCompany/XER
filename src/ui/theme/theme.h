#ifndef XER_UI_THEME_THEME_H
#define XER_UI_THEME_THEME_H

#include "tokens.h"

namespace XER::UI::Theme {

enum class Variant {
    Light,
    Dark
};

class Provider {
public:
    void SetVariant(Variant variant) noexcept;
    Variant GetVariant() const noexcept;
    const Tokens& GetTokens() const noexcept;

    // Custom design tokens (e.g. loaded from a JSON theme file). While
    // set they take precedence over Light/Dark variants; ClearTokens
    // restores variant behavior. SetTokens also records the variant the
    // custom set was derived from for GetVariant reporting.
    void SetTokens(Tokens tokens, Variant variant);
    void ClearTokens() noexcept;
    bool HasCustomTokens() const noexcept { return has_custom_; }

private:
    Variant variant_ = Variant::Light;
    Tokens custom_{};
    bool has_custom_ = false;
};

} // namespace XER::UI::Theme

#endif // XER_UI_THEME_THEME_H
