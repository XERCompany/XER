/**
 * InvalidationSet — collected from a selector's compounds; describes
 * which descendants must recalc style when a feature changes on an
 * ancestor (design doc: ".c1 div.c2" → set keyed by class c1 that
 * invalidates descendants with class c2).
 */
#pragma once

#include <string>
#include <vector>

#include "ui/css/css_element.h"

namespace XER::UI::CSS {

class InvalidationSet {
public:
    // Features this set matches on the element ITSELF (self-side).
    void AddClass(std::string cls) { classes_.push_back(std::move(cls)); }
    void AddTag(std::string tag) { tags_.push_back(std::move(tag)); }
    void AddId(std::string id) { ids_.push_back(std::move(id)); }
    void SetWholeSubtree() { whole_subtree_ = true; }
    void SetFocusOnly() { focus_only_ = true; }

    // Does this set's self-features match |el|? (c2-side matching.)
    bool Matches(const CSSElement& el) const;

    const std::vector<std::string>& classes() const { return classes_; }
    const std::vector<std::string>& tags() const { return tags_; }
    const std::vector<std::string>& ids() const { return ids_; }
    bool whole_subtree() const { return whole_subtree_; }
    bool focus_only() const { return focus_only_; }

    // Builds a set from a parsed selector: right-most compound becomes
    // self-features; left compounds become ancestor keys.
    static InvalidationSet FromSelector(const class Selector& sel,
                                        int* ancestor_key_class_index);

private:
    std::vector<std::string> classes_;
    std::vector<std::string> tags_;
    std::vector<std::string> ids_;
    bool whole_subtree_ = false;
    bool focus_only_ = false;
};

}  // namespace XER::UI::CSS
