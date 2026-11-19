/**
 * TreeScope — a tree of elements for a document (design doc): holds the
 * ScopedStyleResolver with this scope's active style sheets (partitioned
 * into the RuleSet) plus the pending invalidations accumulated for its
 * nodes.
 */
#pragma once

#include "ui/css/css_node.h"
#include "ui/css/pending_invalidations.h"
#include "ui/css/style_engine.h"

namespace XER::UI::CSS {

// ScopedStyleResolver — the StyleEngine for one scope (design doc).
using ScopedStyleResolver = StyleEngine;

class TreeScope {
public:
    explicit TreeScope(CSSNode* root = nullptr) : root_(root) {}

    ScopedStyleResolver& Resolver() { return resolver_; }
    PendingInvalidations& Pending() { return pending_; }
    CSSNode* root() const { return root_; }
    void SetRoot(CSSNode* root) { root_ = root; }

private:
    ScopedStyleResolver resolver_;
    PendingInvalidations pending_;
    CSSNode* root_ = nullptr;
};

}  // namespace XER::UI::CSS
