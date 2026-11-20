#include "content/css/style_resolver.h"

#include <algorithm>
#include <vector>

#include "content/css/element_rule_collector.h"
#include "content/css/selector_checker.h"
#include "content/css/style_resolver_state.h"

namespace XER::Content::CSS {

namespace {

// Applies declarations + AffectedBy* flags of the matched rules in
// cascade order (origin asc, specificity asc, position asc — later wins).
void ApplyCascade(const std::vector<const RuleData*>& matched,
                  ComputedStyle* style) {
    std::vector<const RuleData*> sorted(matched.begin(), matched.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const RuleData* a, const RuleData* b) {
                  if (a->origin != b->origin)
                      return static_cast<int>(a->origin) <
                             static_cast<int>(b->origin);
                  if (a->specificity != b->specificity)
                      return a->specificity < b->specificity;
                  return a->position < b->position;
              });
    for (const auto* d : sorted) {
        for (const auto& decl : d->rule->declarations)
            style->Set(decl.property, decl.value);
        // Design doc: mark AffectedBy* flags so pseudo-state changes
        // recalc only elements whose style actually depends on them.
        for (const auto& comp : d->rule->selector.compounds)
            for (const auto& s : comp.simples) {
                using T = SimpleSelector::Type;
                if (s.type == T::kHover) style->SetAffectedByHover(true);
                if (s.type == T::kActive) style->SetAffectedByActive(true);
                if (s.type == T::kFocus) style->SetAffectedByFocus(true);
            }
    }
}

// Collects matching rules of one origin through the collector path
// (design doc: MatchRequest + CollectMatchingRulesForList).
void CollectForOrigin(const RuleSet& rules, CascadeOrigin origin,
                      StyleResolverState& state,
                      std::vector<const RuleData*>* out) {
    const CSSElement& el = state.element();
    std::vector<const RuleData*> candidates;
    rules.CollectCandidates(el, &candidates);
    ElementRuleCollector collector(
        ElementResolveContext{&el, &state.nth_cache(),
                              &state.dynamic_pseudo_mask()},
        MatchRequest{origin});
    collector.CollectMatchingRulesForList(candidates);
    for (const auto* d : collector.matched_rules())
        if (d->origin == origin) out->push_back(d);
}

}  // namespace

ComputedStyle StyleResolver::ResolveStyle(const CSSElement& el) const {
    StyleResolverState state(el, nullptr, nullptr);
    return ResolveStyle(state);
}

ComputedStyle StyleResolver::ResolveStyle(
    StyleResolverState& state) const {
    MatchAllRules(state);
    ComputedStyle style;
    ApplyCascade(state.matched_rules(), &style);
    // Design doc: inline style wins over every rule; inherited props
    // fall back to the parent's computed value.
    if (state.inline_declarations())
        style.ApplyInline(*state.inline_declarations());
    if (state.parent_style()) style.InheritFrom(*state.parent_style());
    // Design doc: rules that failed only on a dynamic pseudo flag the
    // element — later state changes recalc only flagged elements.
    const auto m = state.dynamic_pseudo_mask();
    if (m & kHoverBit) style.SetAffectedByHover(true);
    if (m & kActiveBit) style.SetAffectedByActive(true);
    if (m & kFocusBit) style.SetAffectedByFocus(true);
    return style;
}

void StyleResolver::MatchAllRules(StyleResolverState& state) const {
    // Design doc: origins ascend — UA, then User, then Author (the
    // document's own style via MatchAuthorRules).
    MatchUserAgentRules(state);
    MatchUserRules(state);
    MatchAuthorRules(state);
}

void StyleResolver::MatchUserAgentRules(
    StyleResolverState& state) const {
    CollectForOrigin(rules_, CascadeOrigin::kUserAgent, state,
                     &state.matched_rules());
}

void StyleResolver::MatchUserRules(StyleResolverState& state) const {
    CollectForOrigin(rules_, CascadeOrigin::kUser, state,
                     &state.matched_rules());
}

void StyleResolver::MatchAuthorRules(StyleResolverState& state) const {
    // Design doc order: host → slotted → element-scope → pseudo parts →
    // element rules. Only the element pass has rules in this engine.
    MatchHostRules(state);
    MatchSlottedRules(state);
    MatchElementScopeRules(state);
    MatchPseudoPartRules(state);
    CollectForOrigin(rules_, CascadeOrigin::kAuthor, state,
                     &state.matched_rules());
}

void StyleResolver::MatchHostRules(StyleResolverState&) const {}
void StyleResolver::MatchSlottedRules(StyleResolverState&) const {}
void StyleResolver::MatchElementScopeRules(StyleResolverState&) const {}
void StyleResolver::MatchPseudoPartRules(StyleResolverState&) const {}

}  // namespace XER::Content::CSS
