#include "content/css/style_engine.h"

namespace XER::Content::CSS {

namespace {

bool Contains(const std::vector<std::string>& keys,
              std::string_view value) {
    for (const auto& k : keys)
        if (k == value) return true;
    return false;
}

}  // namespace

void StyleEngine::AppendActiveStyleSheets(const StyleSheet& sheet) {
    for (const auto& rule : sheet.rules)
        rules_.AddRule(rule, sheet.origin, next_position_++);
}

bool StyleEngine::ClassChangedForElement(
    std::string_view class_name) const {
    return Contains(rules_.features().class_keys, class_name);
}

bool StyleEngine::AttributeChangedForElement(
    std::string_view attr) const {
    return Contains(rules_.features().attribute_keys, attr);
}

bool StyleEngine::IdChangedForElement(std::string_view id) const {
    return Contains(rules_.features().id_keys, id);
}

bool StyleEngine::PseudoStateChangedForElement(
    const ComputedStyle& style, std::string_view pseudo) const {
    // Design doc: hover/active/focus-within recalc only elements whose
    // ComputedStyle carries the matching AffectedBy* flag.
    if (pseudo == "hover") return style.AffectedByHover();
    if (pseudo == "active") return style.AffectedByActive();
    if (pseudo == "focus" || pseudo == "focus-visible" ||
        pseudo == "focus-within")
        return style.AffectedByFocus();
    return false;
}

}  // namespace XER::Content::CSS
