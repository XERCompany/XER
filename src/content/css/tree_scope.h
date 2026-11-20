/**
 * TreeScope — a tree of elements for a document (design doc): holds the
 * ScopedStyleResolver with this scope's TreeScopeStyleSheetCollection
 * (partitioned into the RuleSet) plus the pending invalidations
 * accumulated for its nodes.
 */
#pragma once

#include <vector>

#include "content/css/css_node.h"
#include "content/css/pending_invalidations.h"
#include "content/css/style_engine.h"
#include "content/css/style_rule.h"

namespace XER::Content::CSS {

// ScopedStyleResolver — the StyleEngine for one scope (design doc).
using ScopedStyleResolver = StyleEngine;

// TreeScopeStyleSheetCollection — the scope's active sheets; appends
// mark the collection dirty so the resolver rebuilds once, lazily, at
// UpdateActiveStyle time (design doc: sheets are collected per scope
// and partitioned by FindBestRuleSetAndAdd on append).
class TreeScopeStyleSheetCollection {
public:
    void Append(const StyleSheet& sheet) {
        sheets_.push_back(sheet);
        dirty_ = true;
    }
    void Clear() {
        sheets_.clear();
        dirty_ = true;
    }
    bool dirty() const { return dirty_; }
    void MarkClean() { dirty_ = false; }
    const std::vector<StyleSheet>& sheets() const { return sheets_; }

private:
    std::vector<StyleSheet> sheets_;
    bool dirty_ = false;
};

class TreeScope {
public:
    explicit TreeScope(CSSNode* root = nullptr) : root_(root) {}

    ScopedStyleResolver& Resolver() { return resolver_; }
    PendingInvalidations& Pending() { return pending_; }
    TreeScopeStyleSheetCollection& StyleSheets() { return sheets_; }
    CSSNode* root() const { return root_; }
    void SetRoot(CSSNode* root) { root_ = root; }

    // Design doc: appending a sheet adds it to the scope's collection;
    // the resolver's RuleSet is rebuilt at UpdateActiveStyle time.
    void AppendActiveStyleSheets(const StyleSheet& sheet) {
        sheets_.Append(sheet);
    }

    // Rebuilds the resolver's RuleSet when the collection changed
    // (called by DocumentStyle::UpdateActiveStyle).
    void RebuildActiveStyleSheetsIfNeeded() {
        if (!sheets_.dirty()) return;
        RuleSet fresh;
        for (const auto& sheet : sheets_.sheets())
            for (const auto& rule : sheet.rules)
                fresh.AddRule(rule, sheet.origin, position_++);
        resolver_.SwapRules(std::move(fresh));
        sheets_.MarkClean();
    }

private:
    ScopedStyleResolver resolver_;
    PendingInvalidations pending_;
    TreeScopeStyleSheetCollection sheets_;
    CSSNode* root_ = nullptr;
    int position_ = 0;
};

}  // namespace XER::Content::CSS
