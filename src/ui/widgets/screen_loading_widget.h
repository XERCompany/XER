#ifndef XER_UI_WIDGETS_SCREEN_LOADING_WIDGET_H
#define XER_UI_WIDGETS_SCREEN_LOADING_WIDGET_H

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "theme/theme.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Startup splash shown before the main editor content. Runs the boot
// checklist on a worker thread — project database, project list, cache
// files and hardware drivers — and reports per-step progress. When every
// step has finished (and a minimum visible time has elapsed) the completion
// callback fires on the UI thread so the host can swap in the real content.
class ScreenLoadingWidget final : public Widget {
public:
    using CompleteCallback = std::function<void()>;

    explicit ScreenLoadingWidget(CompleteCallback on_complete = {});
    ~ScreenLoadingWidget() override;

    ScreenLoadingWidget(const ScreenLoadingWidget&) = delete;
    ScreenLoadingWidget& operator=(const ScreenLoadingWidget&) = delete;

    // Idempotent; safe to call once after construction.
    void Start();
    // UI-thread tick: polls worker progress, advances animation and fires
    // the completion callback exactly once.
    void Tick();
    bool IsFinished() const;

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event&) override { return false; }

    Layout::Size GetMinSize() const override { return {320.0f, 240.0f}; }
    Layout::Size GetPreferredSize() const override { return {480.0f, 360.0f}; }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

private:
    enum class StepStatus { kPending, kRunning, kDone, kError };

    struct Step {
        std::string label;
        std::string detail;
        StepStatus status = StepStatus::kPending;
        double fraction = 0.0;  // 0..1 inside the running step
    };

    struct Shared {
        std::mutex mutex;
        std::vector<Step> steps;
        bool all_done = false;
    };

    void RunBootSequence();
    void UpdateStep(std::size_t index, StepStatus status,
                    const std::string& detail = {},
                    double fraction = -1.0);

    CompleteCallback on_complete_;
    std::shared_ptr<Shared> shared_;
    std::thread worker_;
    std::atomic<bool> started_{false};
    bool completion_fired_ = false;
    double spinner_phase_ = 0.0;
    std::chrono::steady_clock::time_point started_at_{};

    Layout::Rect bounds_{};
    Theme::Tokens tokens_{};
    bool tokens_valid_ = false;
};

}  // namespace XER::UI::Widgets

#endif  // XER_UI_WIDGETS_SCREEN_LOADING_WIDGET_H
