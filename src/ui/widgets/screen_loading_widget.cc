#include "screen_loading_widget.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "async_io/hardware_abstraction/system_info.h"
#include "cache/cache_manager.h"
#include "node_graph/node_graph_project_store.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

namespace NG = XER::NodeGraph;
namespace Cache = XER::Cache;
namespace HW = XER::AsyncIO::IO::Hardware;

constexpr std::size_t kStepDatabase = 0;
constexpr std::size_t kStepProjects = 1;
constexpr std::size_t kStepCache = 2;
constexpr std::size_t kStepDrivers = 3;
constexpr std::size_t kStepCount = 4;

std::string FormatBytes(std::uint64_t bytes) {
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        return std::to_string(bytes / (1024ull * 1024ull * 1024ull)) + " GB";
    }
    if (bytes >= 1024ull * 1024ull) {
        return std::to_string(bytes / (1024ull * 1024ull)) + " MB";
    }
    if (bytes >= 1024ull) {
        return std::to_string(bytes / 1024ull) + " KB";
    }
    return std::to_string(bytes) + " B";
}

}  // namespace

ScreenLoadingWidget::ScreenLoadingWidget(CompleteCallback on_complete)
    : on_complete_(std::move(on_complete)) {
    auto shared = std::make_shared<Shared>();
    shared->steps = {
        {"Database", "Opening project database…"},
        {"Projects", "Reading project list…"},
        {"Cache", "Verifying cache files…"},
        {"Drivers", "Probing hardware drivers…"},
    };
    shared_ = std::move(shared);
}

ScreenLoadingWidget::~ScreenLoadingWidget() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void ScreenLoadingWidget::Start() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return;
    }
    started_at_ = std::chrono::steady_clock::now();
    worker_ = std::thread([this] { RunBootSequence(); });
}

void ScreenLoadingWidget::UpdateStep(std::size_t index, StepStatus status,
                                     const std::string& detail,
                                     double fraction) {
    std::lock_guard lock(shared_->mutex);
    if (index >= shared_->steps.size()) {
        return;
    }
    Step& step = shared_->steps[index];
    step.status = status;
    if (!detail.empty()) {
        step.detail = detail;
    }
    if (fraction >= 0.0) {
        step.fraction = std::clamp(fraction, 0.0, 1.0);
    }
}

void ScreenLoadingWidget::RunBootSequence() {
    auto& cache = Cache::CacheManager::Instance();

    // ── 1. Database ──────────────────────────────────────────────────────
    UpdateStep(kStepDatabase, StepStatus::kRunning);
    try {
        const auto db_path = cache.Persistent().root() / "node_graph" /
                             "projects.db";
        NG::ProjectStore store(db_path.string());
        std::string err;
        if (!store.Open(&err)) {
            UpdateStep(kStepDatabase, StepStatus::kError,
                       "Cannot open " + db_path.filename().string() +
                           ": " + err);
        } else {
            UpdateStep(kStepDatabase, StepStatus::kRunning, {}, 0.6);
            const auto projects = store.ListProjects();
            UpdateStep(kStepDatabase, StepStatus::kDone,
                       std::to_string(projects.size()) + " project(s) · " +
                           db_path.filename().string(),
                       1.0);
        }
    } catch (const std::exception& e) {
        UpdateStep(kStepDatabase, StepStatus::kError, e.what());
    }

    // ── 2. Projects ──────────────────────────────────────────────────────
    UpdateStep(kStepProjects, StepStatus::kRunning);
    try {
        const auto db_path = cache.Persistent().root() / "node_graph" /
                             "projects.db";
        NG::ProjectStore store(db_path.string());
        std::string err;
        std::uint64_t graph_count = 0;
        std::size_t project_count = 0;
        if (store.Open(&err)) {
            const auto projects = store.ListProjects();
            project_count = projects.size();
            for (const auto& p : projects) {
                graph_count += store.ListGraphs(p.id).size();
            }
            // Autosave graph presence is the useful health signal.
            std::string autosave_json;
            for (const auto& p : projects) {
                if (p.name == "autosave" &&
                    store.LoadGraph(p.id, "main", &autosave_json, nullptr)) {
                    break;
                }
            }
            UpdateStep(kStepProjects, StepStatus::kDone,
                       std::to_string(project_count) + " project(s), " +
                           std::to_string(graph_count) + " graph(s)" +
                           (autosave_json.empty() ? "" : ", autosave ok"),
                       1.0);
        } else {
            UpdateStep(kStepProjects, StepStatus::kError, err);
        }
    } catch (const std::exception& e) {
        UpdateStep(kStepProjects, StepStatus::kError, e.what());
    }

    // ── 3. Cache files ───────────────────────────────────────────────────
    // TemporaryStorage is in-memory only; the on-disk cache is the
    // persistent root (== CacheManager::RootDirectory()).
    UpdateStep(kStepCache, StepStatus::kRunning);
    try {
        const auto& persistent_root = cache.Persistent().root();
        const std::vector<std::filesystem::path> roots{persistent_root};

        // Count entries first so progress has a denominator.
        std::uint64_t total_entries = 0;
        for (const auto& root : roots) {
            std::error_code ec;
            if (!std::filesystem::exists(root, ec)) continue;
            for (std::filesystem::recursive_directory_iterator it(
                     root, std::filesystem::directory_options::skip_permission_denied,
                     ec),
                 end;
                 !ec && it != end; it.increment(ec)) {
                std::error_code fec;
                if (it->is_regular_file(fec)) ++total_entries;
            }
        }
        total_entries = std::max<std::uint64_t>(total_entries, 1);

        struct {
            std::uint64_t files = 0;
            std::uint64_t bytes = 0;
            std::uint64_t suspicious = 0;
        } combined;
        std::uint64_t seen = 0;
        double last_reported = -1.0;
        for (const auto& root : roots) {
            std::error_code ec;
            if (!std::filesystem::exists(root, ec)) continue;
            for (std::filesystem::recursive_directory_iterator it(
                     root, std::filesystem::directory_options::skip_permission_denied,
                     ec),
                 end;
                 !ec && it != end; it.increment(ec)) {
                std::error_code fec;
                if (!it->is_regular_file(fec)) continue;
                const auto size = it->file_size(fec);
                ++combined.files;
                combined.bytes += fec ? 0 : size;
                const std::string name = it->path().filename().string();
                const bool dot_tmp = name.size() > 4 &&
                                     name.compare(name.size() - 4, 4, ".tmp") == 0;
                const bool dot_lock = name.size() > 5 &&
                                      name.compare(name.size() - 5, 5, ".lock") == 0;
                if (dot_tmp || dot_lock || (!fec && size == 0)) {
                    ++combined.suspicious;
                }
                const double frac = static_cast<double>(++seen) /
                                    static_cast<double>(total_entries);
                if (frac - last_reported >= 0.05) {
                    last_reported = frac;
                    UpdateStep(kStepCache, StepStatus::kRunning, {}, frac);
                }
            }
        }
        UpdateStep(kStepCache, StepStatus::kDone,
                   std::to_string(combined.files) + " files, " +
                       FormatBytes(combined.bytes) +
                       (combined.suspicious
                            ? " · " + std::to_string(combined.suspicious) +
                                  " leftover(s)"
                            : " · clean"),
                   1.0);
    } catch (const std::exception& e) {
        UpdateStep(kStepCache, StepStatus::kError, e.what());
    }

    // ── 4. Drivers / hardware ────────────────────────────────────────────
    UpdateStep(kStepDrivers, StepStatus::kRunning);
    try {
        HW::SystemInfoProvider provider;
        if (!provider.Initialize()) {
            UpdateStep(kStepDrivers, StepStatus::kError,
                       "hardware provider failed to initialize");
        } else {
            UpdateStep(kStepDrivers, StepStatus::kRunning, {}, 0.5);
            const auto snap = provider.Refresh();
            std::string detail;
            if (snap.gpus.empty()) {
                detail = "no discrete GPU · " + snap.cpu_description;
            } else {
                detail = snap.gpus.front().name;
                if (!snap.gpus.front().driver_version.empty()) {
                    detail += " · driver " + snap.gpus.front().driver_version;
                }
                if (snap.gpus.size() > 1) {
                    detail += " +" + std::to_string(snap.gpus.size() - 1);
                }
            }
            if (snap.physical_cores > 0) {
                detail += " · " + std::to_string(snap.physical_cores) +
                          " cores";
            }
            UpdateStep(kStepDrivers, StepStatus::kDone, detail, 1.0);
        }
    } catch (const std::exception& e) {
        UpdateStep(kStepDrivers, StepStatus::kError, e.what());
    }

    {
        std::lock_guard lock(shared_->mutex);
        shared_->all_done = true;
    }
}

bool ScreenLoadingWidget::IsFinished() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->all_done;
}

void ScreenLoadingWidget::Tick() {
    if (!started_) {
        Start();
    }
    spinner_phase_ += 1.0 / 60.0;

    // Snapshot progress for rendering-independent completion check.
    bool done = false;
    {
        std::lock_guard lock(shared_->mutex);
        done = shared_->all_done;
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started_at_).count();
    // Keep the splash visible briefly even when the machine is fast —
    // an instant flash reads as a glitch, not a loading state.
    if (done && !completion_fired_ && elapsed >= 0.7 && on_complete_) {
        completion_fired_ = true;
        on_complete_();
        return;
    }
    if (!completion_fired_) {
        Invalidate();  // spinner/progress keep animating
    }
}

void ScreenLoadingWidget::Layout(const Layout::Rect& bounds) {
    Layout(bounds, tokens_valid_ ? tokens_ : Theme::Tokens{});
}

void ScreenLoadingWidget::Layout(const Layout::Rect& bounds,
                                 const Theme::Tokens& tokens) {
    bounds_ = bounds;
    SetBounds(bounds);
    if (tokens.spacing_medium > 0.0f) {
        tokens_ = tokens;
        tokens_valid_ = true;
    }
}

void ScreenLoadingWidget::Render(Rendering::Renderer& renderer,
                                 const Theme::Tokens& tokens) {
    if (tokens.spacing_medium > 0.0f) {
        tokens_ = tokens;
        tokens_valid_ = true;
    }
    const auto& t = tokens_valid_ ? tokens_ : tokens;

    renderer.FillRect(bounds_, t.background, t.radius_large);

    // Centered card.
    const float card_w = std::min(440.0f, static_cast<float>(bounds_.width) - 48.0f);
    const float card_h = 300.0f;
    const float card_x = static_cast<float>(bounds_.x) +
                         (static_cast<float>(bounds_.width) - card_w) * 0.5f;
    const float card_y = static_cast<float>(bounds_.y) +
                         (static_cast<float>(bounds_.height) - card_h) * 0.5f;
    const Layout::Rect card{card_x, card_y, card_w, card_h};

    renderer.FillRect(card, t.surface, t.radius_large);
    renderer.StrokeRect(card, t.border, 1.0f, t.radius_large);

    Rendering::TextStyle title_style;
    title_style.color = t.foreground;
    title_style.size = t.font_title;
    title_style.weight = 600;
    Rendering::TextStyle body_style;
    body_style.color = t.foreground;
    body_style.size = t.font_body;
    Rendering::TextStyle small_style;
    small_style.color = t.muted;
    small_style.size = t.font_small;

    // Header.
    {
        const auto title_size = renderer.MeasureText("XER", title_style);
        Layout::Rect r{card_x + (card_w - title_size.width) * 0.5f,
                       card_y + 22.0f, title_size.width + 8.0f,
                       title_size.height + 6.0f};
        renderer.DrawText("XER", r, title_style);
    }
    {
        const char* subtitle = "Loading workspace";
        const auto sub_size = renderer.MeasureText(subtitle, small_style);
        Layout::Rect r{card_x + (card_w - sub_size.width) * 0.5f,
                       card_y + 22.0f + t.font_title + 8.0f,
                       sub_size.width + 8.0f, sub_size.height + 4.0f};
        renderer.DrawText(subtitle, r, small_style);
    }

    // Snapshot step states.
    std::vector<Step> steps;
    double overall = 0.0;
    {
        std::lock_guard lock(shared_->mutex);
        steps = shared_->steps;
        double acc = 0.0;
        for (const auto& s : steps) {
            acc += s.status == StepStatus::kDone   ? 1.0
                   : s.status == StepStatus::kError ? 1.0
                   : s.fraction;
        }
        overall = steps.empty() ? 0.0 : acc / steps.size();
    }

    // Progress bar.
    const float bar_w = card_w - 56.0f;
    const float bar_h = 6.0f;
    const float bar_x = card_x + 28.0f;
    const float bar_y = card_y + 96.0f;
    renderer.FillRect({bar_x, bar_y, bar_w, bar_h}, t.secondary, bar_h * 0.5f);
    if (overall > 0.0) {
        renderer.FillRect({bar_x, bar_y, bar_w * static_cast<float>(overall),
                           bar_h},
                          t.accent, bar_h * 0.5f);
    }

    // Step rows.
    const float row_h = 34.0f;
    const float row_y0 = bar_y + bar_h + 18.0f;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const Step& s = steps[i];
        const float ry = row_y0 + static_cast<float>(i) * row_h;
        const float icon_x = card_x + 36.0f;
        const float icon_y = ry + (row_h - 14.0f) * 0.5f;

        // Status icon: pending = hollow square, running = spinner arc,
        // done = accent disc + check, error = danger disc + '!'.
        switch (s.status) {
            case StepStatus::kPending:
                renderer.StrokeRect({icon_x, icon_y, 14.0f, 14.0f}, t.border,
                                    1.5f, 4.0f);
                break;
            case StepStatus::kRunning: {
                // 8-spoke spinner; spoke alpha falls off behind the phase.
                const float cx = icon_x + 7.0f;
                const float cy = icon_y + 7.0f;
                const float radius = 6.0f;
                for (int k = 0; k < 8; ++k) {
                    const double ang = spinner_phase_ * 6.0 +
                                       static_cast<double>(k) *
                                           (2.0 * 3.141592653589793 / 8.0);
                    const float a = static_cast<float>(
                        0.25 + 0.75 * (0.5 + 0.5 *
                             std::cos(ang - spinner_phase_ * 6.0 +
                                      3.141592653589793)));
                    const Layout::Point p1{cx + static_cast<float>(std::cos(ang)) *
                                               (radius - 3.0f),
                                           cy + static_cast<float>(std::sin(ang)) *
                                               (radius - 3.0f)};
                    const Layout::Point p2{cx + static_cast<float>(std::cos(ang)) * radius,
                                           cy + static_cast<float>(std::sin(ang)) * radius};
                    Theme::Color sc = t.accent;
                    sc.a = std::clamp(sc.a * a, 0.15f, 1.0f);
                    renderer.DrawLine(p1, p2, sc, 1.6f);
                }
                break;
            }
            case StepStatus::kDone:
                renderer.FillRect({icon_x, icon_y, 14.0f, 14.0f}, t.accent, 7.0f);
                renderer.DrawLine({icon_x + 3.5f, icon_y + 7.0f},
                                  {icon_x + 6.0f, icon_y + 9.5f},
                                  t.accent_foreground, 1.8f);
                renderer.DrawLine({icon_x + 6.0f, icon_y + 9.5f},
                                  {icon_x + 10.5f, icon_y + 4.5f},
                                  t.accent_foreground, 1.8f);
                break;
            case StepStatus::kError:
                renderer.FillRect({icon_x, icon_y, 14.0f, 14.0f}, t.danger, 7.0f);
                renderer.DrawLine({icon_x + 7.0f, icon_y + 3.5f},
                                  {icon_x + 7.0f, icon_y + 8.5f},
                                  t.danger_foreground, 1.8f);
                renderer.FillRect({icon_x + 6.0f, icon_y + 10.5f, 2.0f, 2.0f},
                                  t.danger_foreground, 1.0f);
                break;
        }

        // Label + detail.
        const float text_x = icon_x + 22.0f;
        const float text_w = card_x + card_w - 28.0f - text_x;
        Rendering::TextStyle label_style = body_style;
        if (s.status == StepStatus::kPending) {
            label_style.color = t.muted;
        }
        Layout::Rect label_rect{text_x, ry + 2.0f, text_w * 0.55f, row_h - 4.0f};
        renderer.DrawText(s.label, label_rect, label_style);

        Rendering::TextStyle detail_style = small_style;
        if (s.status == StepStatus::kError) {
            detail_style.color = t.danger;
        } else if (s.status == StepStatus::kDone) {
            detail_style.color = t.foreground;
        }
        Layout::Rect detail_rect{text_x + text_w * 0.55f, ry + 2.0f,
                                 text_w * 0.45f, row_h - 4.0f};
        renderer.DrawText(s.detail, detail_rect, detail_style);
    }

    // Footer status line.
    {
        const bool any_error = std::any_of(
            steps.begin(), steps.end(),
            [](const Step& s) { return s.status == StepStatus::kError; });
        const char* footer = any_error
                                 ? "Finished with warnings — see steps above"
                                 : "All systems ready";
        const auto fs = renderer.MeasureText(footer, small_style);
        Layout::Rect r{card_x + (card_w - fs.width) * 0.5f,
                       card_y + card_h - 34.0f, fs.width + 8.0f,
                       fs.height + 4.0f};
        renderer.DrawText(footer, r, small_style);
    }
}

}  // namespace XER::UI::Widgets
