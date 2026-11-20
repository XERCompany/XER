#include "content/css/element_rule_collector.h"

#include "content/css/selector_checker.h"

namespace XER::Content::CSS {

void ElementRuleCollector::CollectMatchingRulesForList(
    const std::vector<const RuleData*>& list) {
    if (!ctx_.element) return;
    for (const auto* d : list) {
        if (!d || !d->rule) continue;
        if (d->origin != request_.origin &&
            static_cast<int>(d->origin) > static_cast<int>(request_.origin))
            continue;  // UA rules don't participate when author asked.
        SelectorCheckingContext ctx{ctx_.element, &d->rule->selector,
                                    ctx_.nth_cache};
        if (SelectorChecker::Match(ctx)) {
            DidMatchRule(d);
            continue;
        }
        // Design doc: a rule rejected only on a dynamic pseudo still
        // flags the element — later hover/active changes recalc only
        // elements carrying the AffectedBy* flag.
        std::uint8_t ignored = kNoDynamic;
        if (ctx_.dynamic_pseudo_out &&
            SelectorChecker::MatchIgnoringDynamicPseudos(ctx, &ignored))
            *ctx_.dynamic_pseudo_out |= ignored;
    }
}

}  // namespace XER::Content::CSS
