#include "ui_events.h"

#include <algorithm>

namespace XER::UI::Core {

std::uint64_t UiEventBus::Subscribe(Widgets::Event::Type type,
                                    Listener listener) {
    const std::uint64_t id = next_id_++;
    subs_.push_back(
        {id, static_cast<int>(type), std::move(listener)});
    return id;
}

std::uint64_t UiEventBus::SubscribeAll(Listener listener) {
    const std::uint64_t id = next_id_++;
    subs_.push_back({id, -1, std::move(listener)});
    return id;
}

void UiEventBus::Unsubscribe(std::uint64_t id) {
    std::erase_if(subs_, [id](const Sub& s) { return s.id == id; });
}

std::uint64_t UiEventBus::AddEventFilter(
    std::function<bool(Widgets::Event&)> filter) {
    const std::uint64_t id = next_id_++;
    filters_.push_back({id, std::move(filter)});
    return id;
}

void UiEventBus::RemoveEventFilter(std::uint64_t id) {
    std::erase_if(filters_,
                  [id](const FilterSub& s) { return s.id == id; });
}

std::uint64_t UiEventBus::AddRenderHook(RenderPhase phase,
                                        RenderHook hook) {
    const std::uint64_t id = next_id_++;
    render_hooks_.push_back({id, phase, std::move(hook)});
    return id;
}

void UiEventBus::RemoveRenderHook(std::uint64_t id) {
    std::erase_if(render_hooks_,
                  [id](const RenderSub& s) { return s.id == id; });
}

bool UiEventBus::Dispatch(Widgets::Event& event) {
    // Copy the filter list: a filter may add/remove filters while it
    // runs, and we must not iterate a mutated vector.
    const auto filters = filters_;
    for (const auto& f : filters) {
        if (f.fn && !f.fn(event)) {
            return false;
        }
    }
    // Same for listeners.
    const int ti = static_cast<int>(event.type);
    const auto subs = subs_;
    for (const auto& s : subs) {
        if (s.fn && (s.type_index < 0 || s.type_index == ti)) {
            s.fn(event);
        }
    }
    return true;
}

void UiEventBus::NotifyRender(RenderPhase phase,
                              const UiRenderFrame& frame) {
    const auto hooks = render_hooks_;
    for (const auto& h : hooks) {
        if (h.fn && h.phase == phase) {
            h.fn(frame);
        }
    }
}

bool UiEventBus::HasFilters() const { return !filters_.empty(); }

bool UiEventBus::HasRenderHooks(RenderPhase phase) const {
    return std::any_of(render_hooks_.begin(), render_hooks_.end(),
                       [phase](const RenderSub& s) {
                           return s.phase == phase;
                       });
}

bool UiEventBus::HasListeners() const { return !subs_.empty(); }

}  // namespace XER::UI::Core
