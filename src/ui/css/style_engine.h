/**
 * StyleEngine — per-tree-scope style engine (design doc): owns the
 * ScopedStyleResolver's RuleSet built by AppendActiveStyleSheets,
 * tracks invalidation state flags on nodes, and produces ComputedStyle
 * on demand.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ui/css/computed_style.h"
#include "ui/css/rule_set.h"
#include "ui/css/style_resolver.h"

namespace XER::UI::CSS {

// Node style-state bits (design doc: style invalidation marks).
enum StyleChange : std::uint8_t {
    kNoStyleChange = 0,
    kLocalStyleChange = 1 << 0,
    kSubtreeStyleChange = 1 << 1,
};

class StyleEngine {
public:
    // Appends a stylesheet (parses via host-supplied rule list),
    // partitioned+indexed into the RuleSet.
    void AppendActiveStyleSheets(const StyleSheet& sheet);

    // Resolves |el|'s computed style through the full cascade.
    ComputedStyle StyleForElement(const CSSElement& el) const {
        return resolver_.ResolveStyle(el);
    }

    // Features driving invalidation decisions for DOM changes.
    const RuleFeatureSet& Features() const { return rules_.features(); }
    const RuleSet& rules() const { return rules_; }

    // Decides whether a class/attribute change on a node requires style
    // recalculation (immediate) for that node.
    bool ClassChangedForElement(std::string_view class_name) const;
    bool AttributeChangedForElement(std::string_view attr) const;

    // Invalidation sets to push when |class_name| is added to a node
    // (design doc: key = ancestor class, set = descendant self-features).
    const std::vector<InvalidationSet>& InvalidationSetsForClassChange(
        const std::string& class_name) const {
        return rules_.InvalidationSetsForClass(class_name);
    }

private:
    RuleSet rules_;
    StyleResolver resolver_{rules_};
    int next_position_ = 0;
};

}  // namespace XER::UI::CSS
