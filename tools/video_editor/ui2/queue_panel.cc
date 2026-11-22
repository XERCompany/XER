// QueuePanel — see queue_panel.h for the model/driver split. Rows
// are drawn inside one VirtualList (name + state chip + progress bar
// + ETA + format line); the footer buttons act on the selected row.
//
// Drive rules (queue_panel.h): with hooks_.on_submit installed the
// panel promotes at most ONE head job and hands it to the shell —
// ExportEngine is single-job so concurrency stays 1. Without it the
// deterministic simulation advances: Queued → Running → progress
// += dt / sim_total_seconds → Done at 1.0.
#include "queue_panel.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/virtual_list.h"

#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;
using XER::UI::Theme::Color;

constexpr float kToolbarH = 30.0f;
constexpr float kToolbarGap = 6.0f;
constexpr float kStatusH = 18.0f;
constexpr float kRowH = 48.0f;

std::string FormatEta(double seconds) {
    if (seconds < 0.0 || seconds > 359999.0) return "--:--";
    const int s = static_cast<int>(seconds + 0.5);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

Color StateColor(QueueJobState state,
                 const XER::UI::Theme::Tokens& tokens) {
    switch (state) {
        case QueueJobState::Queued:    return tokens.muted;
        case QueueJobState::Running:   return tokens.accent;
        case QueueJobState::Paused:    return Theme::kWarningChip;
        case QueueJobState::Done:      return Theme::kClipAudio;
        case QueueJobState::Failed:    return tokens.danger;
        case QueueJobState::Cancelled: return tokens.disabled;
    }
    return tokens.muted;
}

bool IsTerminal(QueueJobState s) {
    return s == QueueJobState::Done || s == QueueJobState::Failed ||
           s == QueueJobState::Cancelled;
}

}  // namespace

const char* QueueJobStateName(QueueJobState state) {
    switch (state) {
        case QueueJobState::Queued:    return "Queued";
        case QueueJobState::Running:   return "Running";
        case QueueJobState::Paused:    return "Paused";
        case QueueJobState::Done:      return "Done";
        case QueueJobState::Failed:    return "Failed";
        case QueueJobState::Cancelled: return "Cancelled";
    }
    return "?";
}

std::shared_ptr<QueuePanel> QueuePanel::Create() {
    return std::shared_ptr<QueuePanel>(new QueuePanel());
}

QueuePanel::QueuePanel() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void QueuePanel::SetHooks(Hooks hooks) {
    hooks_ = std::move(hooks);
    // A real driver replaces the simulation the moment the shell
    // installs the submit seam.
    if (hooks_.on_submit) simulated_ = false;
}

void QueuePanel::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    enqueue_btn_ = std::make_shared<W::Button>(
        "+ Enqueue", W::ButtonStyle::Secondary);
    enqueue_btn_->SetSize(W::ButtonSize::Small);
    enqueue_btn_->SetTooltip(
        "Enqueue an export of the current sequence");
    enqueue_btn_->SetOnClick([this] {
        if (hooks_.on_enqueue) hooks_.on_enqueue();
    });
    enqueue_btn_->SetEnabled(false);   // until the shell wires it

    list_ = W::VirtualList::Create();
    list_->SetItemHeight(kRowH);
    list_->SetSelectionMode(W::VirtualList::SelectionMode::Single);
    list_->SetItemRenderer(
        [this](XER::UI::Rendering::Renderer& renderer,
               Layout::Rect row, int index, bool /*selected*/) {
            const QueueJob* job = JobAt(index);
            if (job == nullptr) return;
            const auto& tokens = CachedLayoutTokens();
            const float pad = Theme::kSpaceXs;
            const Color state_color =
                StateColor(job->state, tokens);

            // Line 1: name (flex) + state chip + ETA.
            const float chip_w = 64.0f;
            const float eta_w = 52.0f;
            renderer.DrawText(
                job->name,
                {row.x + pad, row.y + 5.0f,
                 row.width - chip_w - eta_w - pad * 4.0f, 16.0f},
                {IsTerminal(job->state) ? tokens.muted
                                        : tokens.foreground,
                 Theme::kFontBody, 500});
            const Layout::Rect chip{
                row.Right() - eta_w - chip_w - pad * 2.0f,
                row.y + 4.0f, chip_w, 16.0f};
            renderer.FillRect(
                chip, Theme::WithAlpha(state_color, 0.22f),
                tokens.radius_small);
            renderer.DrawText(QueueJobStateName(job->state), chip,
                              {state_color, Theme::kFontCaption, 600});
            const std::string eta =
                job->state == QueueJobState::Running
                    ? FormatEta(job->eta_seconds)
                    : job->state == QueueJobState::Done ? "done"
                    : job->state == QueueJobState::Failed ? "failed"
                                                          : "--:--";
            renderer.DrawText(
                eta,
                {row.Right() - eta_w - pad, row.y + 4.0f, eta_w,
                 16.0f},
                {tokens.muted, Theme::kFontCaption, 500, true});

            // Line 2: format/detail, then the progress bar + percent.
            const std::string meta =
                job->state == QueueJobState::Failed &&
                        !job->error.empty()
                    ? job->format + " — " + job->error
                    : job->format +
                          (job->detail.empty()
                               ? ""
                               : " · " + job->detail);
            renderer.DrawText(
                meta,
                {row.x + pad, row.y + 21.0f,
                 row.width - pad * 2.0f, 12.0f},
                {tokens.muted, Theme::kFontCaption, 400});
            const Layout::Rect bar{row.x + pad, row.y + 36.0f,
                                   row.width - 52.0f - pad * 2.0f,
                                   5.0f};
            renderer.FillRect(bar, tokens.secondary, 2.5f);
            const double frac = std::clamp(job->progress, 0.0, 1.0);
            if (frac > 0.0) {
                renderer.FillRect(
                    {bar.x, bar.y, bar.width * static_cast<float>(frac),
                     bar.height},
                    state_color, 2.5f);
            }
            char pct[8];
            std::snprintf(pct, sizeof(pct), "%3d%%",
                          static_cast<int>(frac * 100.0 + 0.5));
            renderer.DrawText(
                pct,
                {row.Right() - 48.0f - pad, row.y + 32.0f, 48.0f,
                 14.0f},
                {tokens.muted, Theme::kFontCaption, 500, true});
        });
    list_->SetOnSelectionChanged([this] {
        if (list_ == nullptr) return;
        selected_ = list_->GetSelectedIndex();
        SyncToolbar();
    });

    const auto mk = [](const char* text, const char* tip) {
        auto b = std::make_shared<W::Button>(text,
                                             W::ButtonStyle::Ghost);
        b->SetSize(W::ButtonSize::Small);
        b->SetTooltip(tip);
        return b;
    };
    pause_btn_ = mk("Pause", "Pause the selected job");
    pause_btn_->SetOnClick([this] { PauseSelected(); });
    resume_btn_ = mk("Resume", "Resume the paused job");
    resume_btn_->SetOnClick([this] { ResumeSelected(); });
    cancel_btn_ = mk("Cancel", "Cancel the selected job");
    cancel_btn_->SetOnClick([this] { CancelSelected(); });
    reveal_btn_ = mk("Reveal", "Reveal the output file");
    reveal_btn_->SetOnClick([this] { RevealSelected(); });
    retry_btn_ = mk("Retry", "Requeue a failed job");
    retry_btn_->SetOnClick([this] { RetrySelected(); });
    clear_btn_ = mk("Clear", "Clear finished rows");
    clear_btn_->SetOnClick([this] { ClearFinished(); });

    status_ = std::make_shared<W::Label>("Queue empty", true);

    AddChild(enqueue_btn_);
    AddChild(list_);
    AddChild(pause_btn_);
    AddChild(resume_btn_);
    AddChild(cancel_btn_);
    AddChild(reveal_btn_);
    AddChild(retry_btn_);
    AddChild(clear_btn_);
    AddChild(status_);
    SyncToolbar();
    SyncStatusText();
}

// ---- queue model ------------------------------------------------------

int QueuePanel::Enqueue(QueueJob job) {
    if (job.id.empty()) {
        job.id = "job-" + std::to_string(jobs_.size() + 1);
    }
    for (const QueueJob& j : jobs_) {
        if (j.id == job.id) return -1;
    }
    jobs_.push_back(std::move(job));
    if (list_ != nullptr) {
        list_->SetItemCount(static_cast<int>(jobs_.size()));
    }
    SyncStatusText();
    MaybeStartHead();
    Invalidate();
    return static_cast<int>(jobs_.size()) - 1;
}

void QueuePanel::SetJobProgress(const std::string& id, double fraction,
                                double eta_seconds) {
    for (QueueJob& j : jobs_) {
        if (j.id == id) {
            j.progress = std::clamp(fraction, 0.0, 1.0);
            j.eta_seconds = eta_seconds;
            Invalidate();
            return;
        }
    }
}

void QueuePanel::SetJobState(const std::string& id, QueueJobState state,
                             const std::string& error) {
    for (QueueJob& j : jobs_) {
        if (j.id == id) {
            j.state = state;
            if (!error.empty()) j.error = error;
            if (state == QueueJobState::Done) {
                j.progress = 1.0;
                j.eta_seconds = 0.0;
            }
            SyncToolbar();
            SyncStatusText();
            Invalidate();
            MaybeStartHead();   // a terminal head frees the next job
            return;
        }
    }
}

bool QueuePanel::RemoveJob(const std::string& id) {
    const auto it =
        std::find_if(jobs_.begin(), jobs_.end(),
                     [&](const QueueJob& j) { return j.id == id; });
    if (it == jobs_.end()) return false;
    const int index =
        static_cast<int>(std::distance(jobs_.begin(), it));
    if (it->id == submitted_head_) submitted_head_.clear();
    jobs_.erase(it);
    if (selected_ == index) selected_ = -1;
    else if (selected_ > index) --selected_;
    if (list_ != nullptr) {
        list_->SetItemCount(static_cast<int>(jobs_.size()));
    }
    SyncToolbar();
    SyncStatusText();
    Invalidate();
    return true;
}

bool QueuePanel::WithdrawSubmission(const std::string& id) {
    for (QueueJob& j : jobs_) {
        if (j.id != id) continue;
        if (j.id == submitted_head_) submitted_head_.clear();
        if (j.state == QueueJobState::Running) {
            j.state = QueueJobState::Queued;
        }
        SyncToolbar();
        SyncStatusText();
        Invalidate();
        return true;
    }
    return false;
}

void QueuePanel::ClearFinished() {
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (IsTerminal(it->state)) {
            if (it->id == submitted_head_) submitted_head_.clear();
            it = jobs_.erase(it);
        } else {
            ++it;
        }
    }
    selected_ = -1;
    if (list_ != nullptr) {
        list_->SetItemCount(static_cast<int>(jobs_.size()));
        list_->SetSelectedIndex(-1);
    }
    SyncToolbar();
    SyncStatusText();
    Invalidate();
}

const QueueJob* QueuePanel::JobById(const std::string& id) const {
    for (const QueueJob& j : jobs_) {
        if (j.id == id) return &j;
    }
    return nullptr;
}

const QueueJob* QueuePanel::JobAt(int index) const {
    if (index < 0 || index >= static_cast<int>(jobs_.size())) {
        return nullptr;
    }
    return &jobs_[static_cast<std::size_t>(index)];
}

int QueuePanel::ActiveCount() const {
    int n = 0;
    for (const QueueJob& j : jobs_) {
        if (!IsTerminal(j.state)) ++n;
    }
    return n;
}

bool QueuePanel::IsSimulated() const {
    return simulated_ || hooks_.on_submit == nullptr;
}

// ---- drive ----------------------------------------------------------

void QueuePanel::MaybeStartHead() {
    // Single-worker policy: only the head queued job ever runs —
    // matches ExportEngine's one-worker contract. Simulated drive
    // flips it Running; the real seam hands it to the shell exactly
    // once per id.
    for (QueueJob& j : jobs_) {
        if (j.state == QueueJobState::Running ||
            j.state == QueueJobState::Paused) {
            return;   // worker busy
        }
    }
    for (QueueJob& j : jobs_) {
        if (j.state != QueueJobState::Queued) continue;
        if (IsSimulated()) {
            j.state = QueueJobState::Running;
            j.progress = 0.0;
            j.eta_seconds = j.sim_total_seconds;
        } else if (submitted_head_ != j.id &&
                   hooks_.on_submit) {
            submitted_head_ = j.id;
            j.state = QueueJobState::Running;
            hooks_.on_submit(j.id);
        }
        break;   // head only
    }
    SyncStatusText();
}

void QueuePanel::Tick(double dt_seconds) {
    if (dt_seconds <= 0.0) return;
    if (!IsSimulated()) return;
    bool changed = false;
    for (QueueJob& j : jobs_) {
        if (j.state != QueueJobState::Running) continue;
        const double total =
            j.sim_total_seconds > 0.0 ? j.sim_total_seconds : 1.0;
        j.progress = std::clamp(
            j.progress + dt_seconds / total, 0.0, 1.0);
        j.eta_seconds =
            std::max(0.0, total * (1.0 - j.progress));
        if (j.progress >= 1.0) {
            j.state = QueueJobState::Done;
            j.eta_seconds = 0.0;
        }
        changed = true;
        break;   // single-worker policy
    }
    if (changed) {
        // A just-finished job frees the head — start the next one.
        MaybeStartHead();
        SyncToolbar();
        SyncStatusText();
        Invalidate();
    }
}

// ---- verbs ------------------------------------------------------------

const QueueJob* QueuePanel::SelectedJob() const {
    return JobAt(selected_);
}

void QueuePanel::SetSelectedIndex(int index) {
    selected_ = index;
    if (list_ != nullptr) list_->SetSelectedIndex(index);
    SyncToolbar();
}

void QueuePanel::PauseSelected() {
    if (const QueueJob* j = SelectedJob()) PauseJob(j->id);
}

void QueuePanel::ResumeSelected() {
    if (const QueueJob* j = SelectedJob()) ResumeJob(j->id);
}

void QueuePanel::CancelSelected() {
    if (const QueueJob* j = SelectedJob()) CancelJob(j->id);
}

void QueuePanel::RevealSelected() {
    if (const QueueJob* j = SelectedJob()) RevealJob(j->id);
}

void QueuePanel::RetrySelected() {
    if (const QueueJob* j = SelectedJob()) RetryJob(j->id);
}

bool QueuePanel::PauseJob(const std::string& id) {
    QueueJob* job = nullptr;
    for (QueueJob& j : jobs_) {
        if (j.id == id) job = &j;
    }
    if (job == nullptr || job->state != QueueJobState::Running) {
        return false;
    }
    if (IsSimulated()) {
        job->state = QueueJobState::Paused;
        Invalidate();
        SyncToolbar();
        return true;
    }
    // Real driver: ask the shell — ExportEngine can't pause, so the
    // honest answer is usually false; the row stays Running.
    const bool ok = hooks_.on_pause && hooks_.on_pause(id);
    if (ok) {
        job->state = QueueJobState::Paused;
        SyncToolbar();
        Invalidate();
    } else {
        SetStatus("Pause unsupported by the export engine — cancel "
                  "instead",
                  false);
    }
    return ok;
}

bool QueuePanel::ResumeJob(const std::string& id) {
    QueueJob* job = nullptr;
    for (QueueJob& j : jobs_) {
        if (j.id == id) job = &j;
    }
    if (job == nullptr || job->state != QueueJobState::Paused) {
        return false;
    }
    if (IsSimulated()) {
        job->state = QueueJobState::Running;
        Invalidate();
        SyncToolbar();
        return true;
    }
    const bool ok = hooks_.on_resume && hooks_.on_resume(id);
    if (ok) {
        job->state = QueueJobState::Running;
        SyncToolbar();
        Invalidate();
    }
    return ok;
}

bool QueuePanel::CancelJob(const std::string& id) {
    for (QueueJob& j : jobs_) {
        if (j.id != id) continue;
        if (IsTerminal(j.state)) {
            // Terminal rows dismiss instead of cancel — the legacy
            // [x] affordance.
            RemoveJob(id);
            return true;
        }
        j.state = QueueJobState::Cancelled;
        if (j.id == submitted_head_) submitted_head_.clear();
        if (hooks_.on_cancel) hooks_.on_cancel(id);
        SyncToolbar();
        SyncStatusText();
        Invalidate();
        MaybeStartHead();
        return true;
    }
    return false;
}

bool QueuePanel::RevealJob(const std::string& id) {
    if (hooks_.on_reveal == nullptr) return false;
    if (JobById(id) == nullptr) return false;
    hooks_.on_reveal(id);
    return true;
}

bool QueuePanel::RetryJob(const std::string& id) {
    for (QueueJob& j : jobs_) {
        if (j.id != id || j.state != QueueJobState::Failed) continue;
        j.state = QueueJobState::Queued;
        j.progress = 0.0;
        j.eta_seconds = -1.0;
        j.error.clear();
        if (j.id == submitted_head_) submitted_head_.clear();
        if (hooks_.on_retry) hooks_.on_retry(id);
        SyncToolbar();
        SyncStatusText();
        Invalidate();
        MaybeStartHead();
        return true;
    }
    return false;
}

// ---- sync --------------------------------------------------------------

void QueuePanel::SyncToolbar() {
    if (!children_attached_) return;
    const QueueJob* j = SelectedJob();
    pause_btn_->SetEnabled(j != nullptr &&
                           j->state == QueueJobState::Running);
    resume_btn_->SetEnabled(j != nullptr &&
                            j->state == QueueJobState::Paused);
    cancel_btn_->SetEnabled(j != nullptr);
    reveal_btn_->SetEnabled(
        j != nullptr && j->state == QueueJobState::Done &&
        !j->output_path.empty());
    retry_btn_->SetEnabled(j != nullptr &&
                           j->state == QueueJobState::Failed);
    clear_btn_->SetEnabled(std::any_of(
        jobs_.begin(), jobs_.end(),
        [](const QueueJob& q) { return IsTerminal(q.state); }));
    if (enqueue_btn_ != nullptr) {
        enqueue_btn_->SetEnabled(hooks_.on_enqueue != nullptr);
    }
}

void QueuePanel::SyncStatusText() {
    if (status_ == nullptr) return;
    if (jobs_.empty()) {
        status_->SetText(IsSimulated()
                             ? "Queue empty — enqueue runs "
                               "simulated (engine seam unwired)"
                             : "Queue empty");
        return;
    }
    status_->SetText(std::to_string(jobs_.size()) + " job(s) · " +
                     std::to_string(ActiveCount()) + " active");
}

void QueuePanel::SetStatus(const std::string& text, bool is_error) {
    if (status_ != nullptr) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

// ---- widget contract ------------------------------------------------------

void QueuePanel::Render(XER::UI::Rendering::Renderer& renderer,
                        const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
}

void QueuePanel::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const float x = bounds.x + Theme::kSpaceSm;
    float y = bounds.y + Theme::kSpaceSm;
    const float w = bounds.width - Theme::kSpaceSm * 2.0f;
    const auto& tokens = CachedLayoutTokens();

    // Row 1: title-ish status + enqueue (right).
    const float enq_w = 84.0f;
    enqueue_btn_->Layout({x + w - enq_w, y, enq_w, kToolbarH},
                         tokens);
    y += kToolbarH + kToolbarGap;

    // Footer heights: status line + action row.
    const float footer_h = kStatusH + kToolbarH + kToolbarGap;
    const float content_h =
        bounds.y + bounds.height - Theme::kSpaceSm - footer_h - y;
    list_->Layout({x, y, w, std::max(0.0f, content_h)}, tokens);
    y += std::max(0.0f, content_h) + kToolbarGap;

    // Action row: pause/resume/cancel | reveal/retry | clear.
    const float bw = 58.0f;
    const float gap = 4.0f;
    float bx = x;
    pause_btn_->Layout({bx, y, bw, kToolbarH}, tokens);
    bx += bw + gap;
    resume_btn_->Layout({bx, y, bw, kToolbarH}, tokens);
    bx += bw + gap;
    cancel_btn_->Layout({bx, y, bw, kToolbarH}, tokens);
    bx += bw + gap;
    reveal_btn_->Layout({bx, y, bw, kToolbarH}, tokens);
    bx += bw + gap;
    retry_btn_->Layout({bx, y, bw, kToolbarH}, tokens);
    clear_btn_->Layout(
        {x + w - bw, y, bw, kToolbarH}, tokens);

    status_->Layout({x, y + kToolbarH + 2.0f, w, kStatusH}, tokens);
}

}  // namespace XER::VideoEditor::Ui2
