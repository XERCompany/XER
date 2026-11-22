// ui2 shell composite widgets — the small containers the rebuild spec
// §5 layout needs that the shared kit does not already provide. The
// shared kit supplies Resizable/Tabs/Sidebar/Surface/Command/Dialog;
// these fill the gaps:
//
//   LinearBox        — fixed/flex row & column stacking (title bar over
//                      the resizable body, dock header over content)
//   DockTabGroup     — Tabs strip + single visible panel (the left/right
//                      dock columns)
//   PlaceholderPanel — labeled M1 stand-in surface for panels that land
//                      in M2/M3 (media bin, inspector, monitors…)
//   StatusStrip      — bottom badge row (autosave / jobs / proxy state)
//   TimelineStrip    — tool row + placeholder tracks Surface
//
// Every widget here is a plain Widget subclass — same paint contract as
// the kit: draw inside GetBounds(), no document mutation in Render.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/tabs.h"
#include "widgets/widget.h"

#include "ve_theme.h"  // Ui2::Theme — also aliases Tokens/Color

namespace XER::VideoEditor::Ui2 {

namespace Widgets = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
// NOTE: `Theme` is intentionally NOT aliased here — inside Ui2 the name
// resolves to Ui2::Theme (ve_theme.h), which re-exports the shared
// Theme::Tokens/Color types plus the ui2 metric constants, so
// `Theme::Tokens` in signatures below IS the kit's token type.

// ── LinearBox ────────────────────────────────────────────────────────
// Row/column stacker: each slot is fixed-px, flex-weighted, or sized to
// the child's preferred extent along the main axis. Cross axis honors
// the child's Alignment when the slot is larger than its preferred
// size (default: fill).
class LinearBox final : public Widgets::Widget {
public:
    enum class Direction { Horizontal, Vertical };

    struct Slot {
        float fixed;   // px along main axis (wins over flex)
        float flex;    // weight into the remaining space (0+)
        // Both zero -> preferred main-axis extent. Explicit ctors
        // (not member initializers) so `Slot slot = {}` works as a
        // default argument on the enclosing class.
        constexpr Slot() : fixed(0.0f), flex(0.0f) {}
        constexpr Slot(float fixed_px, float flex_weight)
            : fixed(fixed_px), flex(flex_weight) {}
    };

    static std::shared_ptr<LinearBox> Create(
        Direction dir = Direction::Vertical);

    void SetDirection(Direction dir);
    void SetSpacing(float px);       // gap between slots
    void SetPadding(Layout::EdgeInsets insets);

    void Add(std::shared_ptr<Widgets::Widget> child, Slot slot = {});
    void Clear();

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double) override {}
    bool HandleEvent(const Widgets::Event&) override { return false; }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit LinearBox(Direction dir) : dir_(dir) {}

    Direction dir_;
    float spacing_ = 0.0f;
    Layout::EdgeInsets padding_;
    std::vector<Slot> slots_;   // parallel to GetChildren()
};

// ── DockTabGroup ─────────────────────────────────────────────────────
// One dock column: a Tabs strip (underline style) above a content area
// that shows exactly one panel at a time. AddTab(label, tooltip, panel)
// keeps the panel as a hidden child until activated, so panels keep
// their per-panel view state across switches.
class DockTabGroup final : public Widgets::Widget {
public:
    static std::shared_ptr<DockTabGroup> Create();

    void AddTab(std::string label, std::string tooltip,
                std::shared_ptr<Widgets::Widget> panel);
    int TabCount() const { return static_cast<int>(panels_.size()); }
    int ActiveIndex() const;
    void SetActiveIndex(int index);
    std::shared_ptr<Widgets::Widget> PanelAt(int index) const;
    void SetOnChanged(std::function<void(int)> cb);
    void SetTabStripHeight(float px) { strip_height_ = px; }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double) override {}
    bool HandleEvent(const Widgets::Event&) override { return false; }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    DockTabGroup() = default;
    void SyncVisible();

    std::shared_ptr<Widgets::Tabs> tabs_;
    std::vector<std::shared_ptr<Widgets::Widget>> panels_;
    std::function<void(int)> on_changed_;
    float strip_height_ = 30.0f;
};

// ── PlaceholderPanel ─────────────────────────────────────────────────
// M1 stand-in for a real surface: paints the panel name + a muted
// description on the raised surface color so dock structure is visible
// before M2/M3 land real content. Keeps a stable `panel_id` for
// headless discovery (smoke asserts on ids, not pixels).
class PlaceholderPanel final : public Widgets::Widget {
public:
    static std::shared_ptr<PlaceholderPanel> Create(
        std::string panel_id, std::string title,
        std::string description = {});

    const std::string& PanelId() const { return panel_id_; }
    void SetDescription(std::string text);

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double) override {}
    bool HandleEvent(const Widgets::Event&) override { return false; }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    PlaceholderPanel(std::string panel_id, std::string title,
                     std::string description);

    std::string panel_id_;
    std::string title_;
    std::string description_;
};

// ── StatusStrip ──────────────────────────────────────────────────────
// Bottom status band: left status text + right-aligned placeholder
// badges (autosave / jobs / proxy). Badge text is data-driven so the
// real subsystems can feed it in M2+ without widget changes.
class StatusStrip final : public Widgets::Widget {
public:
    struct Badge {
        std::string id;
        std::string text;
        bool warning = false;   // amber chip vs muted chip
    };

    static std::shared_ptr<StatusStrip> Create();

    void SetStatusText(std::string text);
    const std::string& StatusText() const { return status_text_; }
    void SetBadges(std::vector<Badge> badges);
    const std::vector<Badge>& GetBadges() const { return badges_; }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double) override {}
    bool HandleEvent(const Widgets::Event&) override { return false; }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    StatusStrip() = default;
    std::string status_text_ = "Ready";
    std::vector<Badge> badges_;
};

// TimelineStrip moved to ui2/timeline_strip.{h,cc} — the M2 build
// hosts the real TimelineSurface + tool row there.

}  // namespace XER::VideoEditor::Ui2
