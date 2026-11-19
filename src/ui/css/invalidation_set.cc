#include "ui/css/invalidation_set.h"

#include "ui/css/css_selector.h"

namespace XER::UI::CSS {

bool InvalidationSet::Matches(const CSSElement& el) const {
    if (focus_only_ && !el.IsFocused()) return false;
    bool any = false;
    for (const auto& c : classes_) if (el.HasClass(c)) any = true;
    for (const auto& t : tags_) if (el.TagName() == t) any = true;
    for (const auto& i : ids_) if (el.Id() == i) any = true;
    if (classes_.empty() && tags_.empty() && ids_.empty())
        return whole_subtree_;
    return any || whole_subtree_;
}

InvalidationSet InvalidationSet::FromSelector(
    const Selector& sel, int* ancestor_key_class_index) {
    InvalidationSet set;
    if (ancestor_key_class_index) *ancestor_key_class_index = -1;
    if (sel.compounds.empty()) return set;
    // Right-most compound (index 0) → self features.
    for (const auto& s : sel.compounds[0].simples) {
        switch (s.type) {
            case SimpleSelector::Type::kClass: set.AddClass(s.value); break;
            case SimpleSelector::Type::kTag:   set.AddTag(s.value); break;
            case SimpleSelector::Type::kId:    set.AddId(s.value); break;
            case SimpleSelector::Type::kFocus: set.SetFocusOnly(); break;
            default: break;
        }
    }
    // Left compounds → ancestor keys (only descendant combinators
    // produce invalidation sets; child combinator treated the same).
    for (std::size_t i = 1; i < sel.compounds.size(); ++i)
        for (const auto& s : sel.compounds[i].simples)
            if (s.type == SimpleSelector::Type::kClass &&
                ancestor_key_class_index && *ancestor_key_class_index < 0)
                *ancestor_key_class_index = static_cast<int>(i);
    if (sel.compounds.size() == 1) set.SetWholeSubtree();
    return set;
}

}  // namespace XER::UI::CSS
