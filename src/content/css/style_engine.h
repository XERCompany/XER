/**
 * StyleEngine — per-tree-scope style engine (design doc): owns the
 * ScopedStyleResolver's RuleSet built by AppendActiveStyleSheets,
 * tracks invalidation state flags on nodes, and produces ComputedStyle
 * on demand. FooChangedForElement methods are the DOM-mutation entry
 * points that turn changes into immediate or pending invalidations.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "content/css/computed_style.h"
#include "content/css/rule_set.h"
#include "content/css/style_resolver.h"

namespace XER::Content::CSS {

// Node style-state bits (design doc: style invalidation marks).
enum StyleChange : std::uint8_t {
    kNoStyleChange = 0,
    kLocalStyleChange = 1 << 0,
    kSubtreeStyleChange = 1 << 1,
    // Design doc: kReattach — the element's whole subtree is rebuilt
    // (RecalcDescendantStylesForReattach path).
    kReattach = 1 << 2,
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

    // Full resolve with parent + inline declarations (design doc).
    ComputedStyle StyleForElement(StyleResolverState& state) const {
        return resolver_.ResolveStyle(state);
    }

    // Features driving invalidation decisions for DOM changes.
    const RuleFeatureSet& Features() const { return rules_.features(); }
    const RuleSet& rules() const { return rules_; }

    // Design doc: FooChangedForElement — DOM-mutation entry points.
    // Each returns true when the change forces an immediate recalc of
    // the element itself (the engine never searches up the tree at
    // mutation time — upward checks happen during recalculation).

    // A class was added/removed: immediate recalc when the class is a
    // self-feature of some rule; otherwise only ancestor-keyed
    // invalidation sets apply (see InvalidationSetsForClassChange).
    bool ClassChangedForElement(std::string_view class_name) const;
    // An attribute was added/removed/changed.
    bool AttributeChangedForElement(std::string_view attr) const;
    // The id was added/removed/changed — same semantics as class.
    bool IdChangedForElement(std::string_view id) const;
    // Design doc: focus changes always recalculate the element losing
    // and gaining focus unconditionally — invalidation sets keyed by
    // :focus handle the descendants.
    bool FocusedChangedForElement() const { return true; }
    // :hover / :active state change: recalc only when the element's
    // current style was resolved with the matching AffectedBy* flag
    // (design doc: non-matching rules may not have set the flag, so
    // pseudo changes rely on it rather than invalidation sets).
    bool PseudoStateChangedForElement(const ComputedStyle& style,
                                      std::string_view pseudo) const;

    // Invalidation sets to push when |class_name| is added to a node
    // (design doc: key = ancestor class, set = descendant self-features).
    const std::vector<InvalidationSet>& InvalidationSetsForClassChange(
        const std::string& class_name) const {
        return rules_.InvalidationSetsForClass(class_name);
    }

    // Swaps in a freshly built RuleSet (TreeScope rebuilds when its
    // TreeScopeStyleSheetCollection went dirty). The resolver's
    // reference points at the rules_ member, which stays valid.
    void SwapRules(RuleSet&& rules) { rules_ = std::move(rules); }

private:
    RuleSet rules_;
    StyleResolver resolver_{rules_};
    int next_position_ = 0;
};

}  // namespace XER::Content::CSS
