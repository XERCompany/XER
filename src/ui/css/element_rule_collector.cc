#include "ui/css/element_rule_collector.h"

#include "ui/css/selector_checker.h"

namespace XER::UI::CSS {

void ElementRuleCollector::CollectMatchingRulesForList(
    const std::vector<const RuleData*>& list,
    std::vector<const RuleData*>* out) const {
    if (!ctx_.element || !out) return;
    for (const auto* d : list) {
        if (!d || !d->rule) continue;
        if (d->origin != request_.origin &&
            static_cast<int>(d->origin) > static_cast<int>(request_.origin))
            continue;  // UA rules don't participate when author asked.
        SelectorCheckingContext ctx{ctx_.element, &d->rule->selector};
        if (SelectorChecker::Match(ctx)) out->push_back(d);  // DidMatchRule
    }
}

}  // namespace XER::UI::CSS
