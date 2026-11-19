/**
 * RuleSet — partitioned, indexed rules (design doc: FindBestRuleSetAndAdd
 * inspects each selector's right-most compound and files the RuleData in
 * the matching map — ClassRules, IdRules, TagRules or Universal — so
 * element matching skips most irrelevant rules).
 */
#pragma once

#include <deque>
#include <map>
#include <string>
#include <vector>

#include "ui/css/invalidation_set.h"

#include "ui/css/css_element.h"
#include "ui/css/style_rule.h"

namespace XER::UI::CSS {

// Features an element-change can trigger invalidations on (design doc).
struct AncestorInvalidation;  // fwd

struct RuleFeatureSet {
    std::vector<std::string> class_keys;
    std::vector<std::string> id_keys;
    std::vector<std::string> tag_keys;
    bool has_focus_feature = false;
};

class RuleSet {
public:
    // Files |data| in the index chosen by the selector's right-most
    // simple selector (class > id > attribute > tag > universal).
    void AddRule(const StyleRule& rule, CascadeOrigin origin,
                 int position);

    // Rules possibly matching |el|, gathered from the indexed maps
    // (universal always included).
    void CollectCandidates(const CSSElement& el,
                           std::vector<const RuleData*>* out) const;

    const RuleFeatureSet& features() const { return features_; }
    std::size_t rule_count() const { return rules_.size(); }

    // Design doc: when an ancestor gains a class/id/tag feature, all
    // descendant self-features in the matching sets get invalidated.
    // Returns invalidation sets keyed by |ancestor_feature| (class).
    const std::vector<InvalidationSet>& InvalidationSetsForClass(
        const std::string& ancestor_class) const;

private:
    void AddToIndex(const CompoundSelector& rightmost,
                    const RuleData& data);
    void CollectFeatures(const StyleRule& rule);
    void CollectInvalidation(const StyleRule& rule);

    // deque: pointers stored in the index maps stay valid on push_back.
    std::deque<RuleData> rules_;
    std::map<std::string, std::vector<const RuleData*>> class_rules_;
    std::map<std::string, std::vector<const RuleData*>> id_rules_;
    std::map<std::string, std::vector<const RuleData*>> tag_rules_;
    std::vector<const RuleData*> universal_rules_;
    RuleFeatureSet features_;
    // ancestor class -> self-feature sets to apply to its subtree.
    std::map<std::string, std::vector<InvalidationSet>> ancestor_sets_;
};

}  // namespace XER::UI::CSS
