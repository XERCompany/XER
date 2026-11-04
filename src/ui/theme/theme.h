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

private:
    Variant variant_ = Variant::Light;
};

} // namespace XER::UI::Theme

#endif // XER_UI_THEME_THEME_H
