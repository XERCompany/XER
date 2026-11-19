#include "ui/css/style_engine.h"

namespace XER::UI::CSS {

void StyleEngine::AppendActiveStyleSheets(const StyleSheet& sheet) {
    for (const auto& rule : sheet.rules)
        rules_.AddRule(rule, sheet.origin, next_position_++);
}

bool StyleEngine::ClassChangedForElement(
    std::string_view class_name) const {
    for (const auto& c : rules_.features().class_keys)
        if (c == class_name) return true;
    return false;
}

bool StyleEngine::AttributeChangedForElement(
    std::string_view attr) const {
    for (const auto& r : rules_.features().class_keys)
        if (r == attr) return true;
    return false;
}

}  // namespace XER::UI::CSS
