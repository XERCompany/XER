#pragma once

#include <memory>

namespace XER::UI::Core {
class Window;
}

namespace XER::UI::Debug {

/**
 * DevTools — live widget-tree inspector window ("DevTools" panel).
 *
 * Opens a separate top-level window attached to the same UI application
 * that visualizes another Window's widget tree:
 *
 *  - Debug:    DebugMapUI XML dump of the whole widget tree (type,
 *    geometry, state of every node) in a scrollable text view.
 *  - Elements: collapsible tree of every widget (type, bounds, state)
 *    with a detail pane showing the selected subtree.
 *  - Render:   live frame stats from Window::GetRenderStats() plus the
 *    last native paint error.
 *  - Stats:    aggregate counters collected from the widget tree.
 *
 * The DevTools window closes itself when the inspected window closes,
 * so it never keeps the application alive as an orphan.
 *
 * Usage:
 *   auto devtools = Debug::DevTools::Inspect(*window);
 *
 * The returned object owns the DevTools window; it stays alive until the
 * window is closed (a process-wide registry also keeps it alive so the
 * caller does not have to store the pointer).
 */
class DevTools {
public:
    /// Creates and shows the DevTools window inspecting `target`.
    /// Returns nullptr only when the target has no content widget.
    static std::shared_ptr<DevTools> Inspect(Core::Window& target);

    ~DevTools();

    Core::Window& GetWindow() const;

private:
    class Impl;
    explicit DevTools(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Debug
