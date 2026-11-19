/**
 * ElementRuleCollector — gathers matching RuleData for one element
 * (design doc: CollectMatchingRulesForList is the core of all paths —
 * builds a SelectorCheckingContext and calls checker.Match per RuleData,
 * appending hits via DidMatchRule).
 */
#pragma once

#include <vector>

#include "ui/css/css_element.h"
#include "ui/css/style_rule.h"

namespace XER::UI::CSS {

// Resolve context for one element (design doc).
struct ElementResolveContext {
    const CSSElement* element = nullptr;
};

struct MatchRequest {
    CascadeOrigin origin = CascadeOrigin::kAuthor;
};

class ElementRuleCollector {
public:
    ElementRuleCollector(ElementResolveContext ctx, MatchRequest req)
        : ctx_(ctx), request_(req) {}

    // Tests each RuleData in |list| against the element; matches are
    // appended to |out| (design doc: DidMatchRule).
    void CollectMatchingRulesForList(
        const std::vector<const RuleData*>& list,
        std::vector<const RuleData*>* out) const;

    MatchRequest request() const { return request_; }

private:
    ElementResolveContext ctx_;
    MatchRequest request_;
};

}  // namespace XER::UI::CSS
