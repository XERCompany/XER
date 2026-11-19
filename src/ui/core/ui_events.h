#ifndef XER_UI_CORE_UI_EVENTS_H
#define XER_UI_CORE_UI_EVENTS_H

#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Core {

// UI event system: a lightweight bus plus hook chains that sit between
// the platform message translator and the WidgetTree, and around each
// render pass.
//
//   - UiEventBus       fan-out of dispatched Events to any number of
//                      listeners (telemetry, automation, remote render
//                      processes, scripting).
//   - UiHooks          ordered interception points:
//                      * event filters   — observe/mutate/swallow an
//                        Event before WidgetTree::HandleEvent sees it
//                      * render hooks    — run before/after the widget
//                        tree paints a frame (frame counters, damage
//                        inspection, screenshot capture, frame
//                        streaming to xer_engine_ui_render.exe)
//
// Both live on the UI thread (same thread as WidgetTree); subscribe and
// publish freely re-enter. Subscriptions return ids for removal.

struct UiRenderFrame {
    // Client bounds of the frame being painted.
    Layout::Rect bounds{};
    // Damage rect actually repainted (valid=false means full repaint).
    Layout::Rect damage{};
    bool damage_valid = false;
    // Wall-clock duration of the previous completed frame, ms.
    double last_frame_ms = 0.0;
};

class UiEventBus {
public:
    using Listener = std::function<void(const Widgets::Event&)>;
    using RenderHook = std::function<void(const UiRenderFrame&)>;

    enum class RenderPhase {
        kBeforeFrame,   // tree about to paint
        kAfterFrame,    // paint + present completed
    };

    // type == nullptr (via SubscribeAll) subscribes to every event type.
    std::uint64_t Subscribe(Widgets::Event::Type type, Listener listener);
    std::uint64_t SubscribeAll(Listener listener);
    void Unsubscribe(std::uint64_t id);

    std::uint64_t AddEventFilter(
        std::function<bool(Widgets::Event&)> filter);
    void RemoveEventFilter(std::uint64_t id);

    std::uint64_t AddRenderHook(RenderPhase phase, RenderHook hook);
    void RemoveRenderHook(std::uint64_t id);

    // Runs filters then publishes to listeners. Returns false when a
    // filter swallowed the event (WidgetTree should not dispatch it).
    bool Dispatch(Widgets::Event& event);

    void NotifyRender(RenderPhase phase, const UiRenderFrame& frame);

    bool HasFilters() const;
    bool HasRenderHooks(RenderPhase phase) const;
    bool HasListeners() const;

private:
    struct Sub {
        std::uint64_t id;
        int type_index;  // -1 = all events, else Event::Type ordinal
        Listener fn;
    };
    struct FilterSub {
        std::uint64_t id;
        std::function<bool(Widgets::Event&)> fn;
    };
    struct RenderSub {
        std::uint64_t id;
        RenderPhase phase;
        RenderHook fn;
    };

    std::uint64_t next_id_ = 1;
    std::vector<Sub> subs_;
    std::vector<FilterSub> filters_;
    std::vector<RenderSub> render_hooks_;
};

}  // namespace XER::UI::Core

#endif  // XER_UI_CORE_UI_EVENTS_H
