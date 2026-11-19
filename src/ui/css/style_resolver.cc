#include "ui/css/style_resolver.h"

#include <algorithm>
#include <vector>

#include "ui/css/selector_checker.h"

namespace XER::UI::CSS {

ComputedStyle StyleResolver::ResolveStyle(const CSSElement& el) const {
    std::vector<const RuleData*> candidates;
    rules_.CollectCandidates(el, &candidates);

    std::vector<const RuleData*> matched;
    for (const auto* d : candidates) {
        SelectorCheckingContext ctx{&el, &d->rule->selector};
        if (SelectorChecker::Match(ctx)) matched.push_back(d);
    }
    // Cascade: origin asc, specificity asc, source position asc — apply
    // in order so later rules override.
    std::sort(matched.begin(), matched.end(),
              [](const RuleData* a, const RuleData* b) {
                  if (a->origin != b->origin)
                      return static_cast<int>(a->origin) <
                             static_cast<int>(b->origin);
                  if (a->specificity != b->specificity)
                      return a->specificity < b->specificity;
                  return a->position < b->position;
              });

    ComputedStyle style;
    for (const auto* d : matched) {
        for (const auto& decl : d->rule->declarations)
            style.Set(decl.property, decl.value);
        // Design doc: mark AffectedBy* flags so pseudo-state changes
        // recalc only elements whose style actually depends on them.
        for (const auto& comp : d->rule->selector.compounds)
            for (const auto& s : comp.simples) {
                using T = SimpleSelector::Type;
                if (s.type == T::kHover) style.SetAffectedByHover(true);
                if (s.type == T::kActive) style.SetAffectedByActive(true);
                if (s.type == T::kFocus) style.SetAffectedByFocus(true);
            }
    }
    return style;
}

}  // namespace XER::UI::CSS
