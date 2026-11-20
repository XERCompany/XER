#include "content/css/rule_set.h"

#include <set>

namespace XER::Content::CSS {

void RuleSet::AddRule(const StyleRule& rule, CascadeOrigin origin,
                      int position) {
    RuleData data;
    data.rule = &rule;
    data.origin = origin;
    data.position = position;
    int ids = 0, classes = 0, tags = 0;
    for (const auto& c : rule.selector.compounds)
        for (const auto& s : c.simples) {
            if (s.type == SimpleSelector::Type::kId) ++ids;
            else if (s.type == SimpleSelector::Type::kClass ||
                     s.type == SimpleSelector::Type::kAttribute ||
                     s.type == SimpleSelector::Type::kFocus)
                ++classes;
            else ++tags;
        }
    data.specificity = Specificity(ids, classes, tags);
    rules_.push_back(data);
    if (!rule.selector.compounds.empty())
        AddToIndex(rule.selector.compounds.front(), rules_.back());
    else
        universal_rules_.push_back(&rules_.back());
    CollectFeatures(rule);
    CollectInvalidation(rule);
}

const std::vector<InvalidationSet>& RuleSet::InvalidationSetsForClass(
    const std::string& ancestor_class) const {
    static const std::vector<InvalidationSet> kEmpty;
    const auto it = ancestor_sets_.find(ancestor_class);
    return it == ancestor_sets_.end() ? kEmpty : it->second;
}

void RuleSet::CollectInvalidation(const StyleRule& rule) {
    // ".c1 div.c2" → ancestor key c1 maps to a set matching self c2.
    const auto& sel = rule.selector;
    if (sel.compounds.size() < 2) return;
    auto set = InvalidationSet::FromSelector(sel, nullptr);
    for (std::size_t i = 1; i < sel.compounds.size(); ++i)
        for (const auto& s : sel.compounds[i].simples)
            if (s.type == SimpleSelector::Type::kClass)
                ancestor_sets_[s.value].push_back(set);
}

void RuleSet::AddToIndex(const CompoundSelector& rightmost,
                         const RuleData& data) {
    // FindBestRuleSetAndAdd: pick the most selective simple selector of
    // the right-most compound as the index key.
    const SimpleSelector* best = nullptr;
    auto rank = [](SimpleSelector::Type t) {
        using T = SimpleSelector::Type;
        switch (t) {
            case T::kClass: return 4;
            case T::kId: return 3;
            case T::kAttribute: return 2;
            case T::kTag: return 1;
            case T::kFocus: case T::kHover: case T::kActive:
            case T::kFirstChild: case T::kLastChild: case T::kNthChild:
                return 0;   // doc: fallback features when nothing better
            case T::kUniversal: return -1;  // never an index key
        }
        return 0;
    };
    for (const auto& s : rightmost.simples)
        if (!best || rank(s.type) > rank(best->type)) best = &s;
    if (!best) {
        universal_rules_.push_back(&rules_.back());
        return;
    }
    switch (best->type) {
        case SimpleSelector::Type::kClass:
            class_rules_[best->value].push_back(&rules_.back());
            break;
        case SimpleSelector::Type::kId:
            id_rules_[best->value].push_back(&rules_.back());
            break;
        case SimpleSelector::Type::kTag:
            tag_rules_[best->value].push_back(&rules_.back());
            break;
        default:
            universal_rules_.push_back(&rules_.back());
            break;
    }
}

void RuleSet::CollectCandidates(
    const CSSElement& el, std::vector<const RuleData*>* out) const {
    if (!el.Id().empty()) {
        if (auto it = id_rules_.find(std::string(el.Id()));
            it != id_rules_.end())
            out->insert(out->end(), it->second.begin(), it->second.end());
    }
    for (const auto& [cls, list] : class_rules_)
        if (el.HasClass(cls))
            out->insert(out->end(), list.begin(), list.end());
    if (auto it = tag_rules_.find(std::string(el.TagName()));
        it != tag_rules_.end())
        out->insert(out->end(), it->second.begin(), it->second.end());
    out->insert(out->end(), universal_rules_.begin(),
                universal_rules_.end());
}

void RuleSet::CollectFeatures(const StyleRule& rule) {
    // Design doc: CollectFeaturesFromRuleData — features accumulate
    // across all RuleData into the merged RuleFeatureSet.
    for (const auto& c : rule.selector.compounds)
        for (const auto& s : c.simples) {
            using T = SimpleSelector::Type;
            switch (s.type) {
                case T::kClass: feature_classes_.insert(s.value); break;
                case T::kId: feature_ids_.insert(s.value); break;
                case T::kTag: feature_tags_.insert(s.value); break;
                case T::kAttribute: feature_attrs_.insert(s.value); break;
                case T::kFocus: features_.has_focus_feature = true; break;
                case T::kHover: features_.has_hover_feature = true; break;
                case T::kActive: features_.has_active_feature = true; break;
                default: break;
            }
        }
    features_.class_keys.assign(feature_classes_.begin(),
                                feature_classes_.end());
    features_.id_keys.assign(feature_ids_.begin(), feature_ids_.end());
    features_.tag_keys.assign(feature_tags_.begin(), feature_tags_.end());
    features_.attribute_keys.assign(feature_attrs_.begin(),
                                    feature_attrs_.end());
}

}  // namespace XER::Content::CSS
