#pragma once

#include <string>

namespace XER::UI::Widgets {
class Widget;
}

namespace XER::UI::Debug {

/**
 * DebugMapUI — developer-facing widget tree inspector ("DevTools").
 *
 * Serializes a Widget subtree into an XML document describing every
 * node's type, geometry and interaction state. Intended for debugging
 * layout/hit-test issues, writing regression tests against the widget
 * hierarchy, and feeding tree dumps into external tooling.
 *
 * The inspector is strictly read-only: it never mutates the inspected
 * tree, and it tolerates null roots, empty child lists, excessive depth
 * and (defensively) cyclic child references.
 */
class DebugMapUI {
public:
    /// Options controlling the generated XML.
    struct Options {
        // Defined out-of-class below: Clang rejects a default argument
        // that instantiates the implicit ctor (with NSDMIs) while the
        // enclosing class body is still being parsed.
        Options();

        /// Maximum recursion depth. Deeper nodes are replaced by an
        /// empty element marked depth="limit". Guards against corrupted
        /// or pathologically deep trees.
        int max_depth = 128;
        /// When false, children of effectively-invisible widgets are
        /// skipped entirely instead of being marked hidden="true".
        bool include_hidden = true;
    };

    DebugMapUI() = default;
    explicit DebugMapUI(const Widgets::Widget* root) : root_(root) {}

    void SetRoot(const Widgets::Widget* root) { root_ = root; }
    const Widgets::Widget* GetRoot() const { return root_; }

    /// Serializes `override_root` (or the configured root when null) to
    /// an XML document string.
    std::string ToXml(const Widgets::Widget* override_root = nullptr,
                      const Options& options = Options()) const;

    /// One-shot convenience wrapper around ToXml.
    static std::string Dump(const Widgets::Widget* root,
                            const Options& options = Options());

private:
    const Widgets::Widget* root_ = nullptr;
};

inline DebugMapUI::Options::Options() = default;

} // namespace XER::UI::Debug
