// QueuePanel — the M3 wave-2 export/render queue dock (spec §6
// "Queue" tab): Media-Encoder-style job rows on a VirtualList.
//
//   QUEUE                              [+ Enqueue]
//   ┌────────────────────────────────────────────────┐
//   │ interview.mp4          [Running]  ETA 00:12    │
//   │ H.264 MP4 · 8 Mbps     ▓▓▓▓▓▓░░░░░░░░  62%     │
//   └────────────────────────────────────────────────┘
//   [Pause] [Cancel] [Reveal] [Retry]   [Clear done]
//   3 jobs · 1 active
//
// ── engine seam (the honest split) ─────────────────────────────────
// ExportEngine (export_engine.h) is a real asynchronous worker but
// SINGLE-job and has NO pause/resume. The panel therefore owns the
// queue MODEL (rows below) while the shell supplies the DRIVER:
//
//   * on_submit(id) set  → real engine drive: the shell starts an
//     ExportEngine for the head queued job and pushes progress via
//     UpdateJob; pause/resume hooks return whether the driver
//     honored them (ExportEngine can't pause — VeApp reports that
//     honestly instead of faking a freeze).
//   * on_submit unset    → deterministic SIMULATED drive: Tick(dt)
//     promotes Queued→Running and advances progress at
//     QueueJob::sim_total_seconds per unit — the smoke's fake-job
//     lifecycle path. Nothing is painted on a wall clock, so the
//     check stays deterministic.
//
// Panel owns presentation + the job list; the shell owns engine
// lifetime — the panel never touches ExportEngine.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
class VirtualList;
}

namespace XER::VideoEditor::Ui2 {

enum class QueueJobState : int {
    Queued = 0,
    Running = 1,
    Paused = 2,
    Done = 3,
    Failed = 4,
    Cancelled = 5,
};

// One queue row — the shell composes `format`/`detail`; the driver
// advances `progress`/`eta_seconds`/`state`.
struct QueueJob {
    std::string id;            // caller-keyed ("job-7", uuid, path…)
    std::string name;          // job name (output basename)
    std::string format;        // "H.264 MP4 · 8 Mbps"
    std::string detail;        // range line ("00:00–00:42", "work area")
    std::string output_path;   // UTF-8 — the Reveal target
    double progress = 0.0;     // 0..1
    QueueJobState state = QueueJobState::Queued;
    double eta_seconds = -1.0; // <0 = unknown
    std::string error;         // Failed detail
    // Simulated drive only: seconds of Tick time for a full run.
    double sim_total_seconds = 8.0;
};

const char* QueueJobStateName(QueueJobState state);

class QueuePanel final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<QueuePanel> Create();

    struct Hooks {
        // Driver seams — return false to report "unsupported"
        // (e.g. pause on a single-job ExportEngine); the panel
        // surfaces that honestly instead of faking a freeze.
        std::function<bool(const std::string& id)> on_pause;
        std::function<bool(const std::string& id)> on_resume;
        std::function<void(const std::string& id)> on_cancel;
        std::function<void(const std::string& id)> on_reveal;
        std::function<void(const std::string& id)> on_retry;
        // When set, the shell owns the engine drive for real jobs —
        // fired once when a queued job reaches the head of the queue
        // under non-simulated drive. Unset → simulated drive.
        std::function<void(const std::string& id)> on_submit;
        // "+ Enqueue" toolbar verb — the shell composes the job
        // (current sequence → output path) and calls Enqueue().
        std::function<void()> on_enqueue;
        std::function<void(const std::string&, bool)> on_status;
    };
    void SetHooks(Hooks hooks);
    const Hooks& GetHooks() const { return hooks_; }

    // ---- queue model -------------------------------------------
    // Returns the assigned row index. Duplicate ids are rejected
    // (returns -1) — ids are the model's identity.
    int Enqueue(QueueJob job);
    // Partial-update seams for the driver (progress tick, terminal
    // state, error text). All no-op on unknown ids.
    void SetJobProgress(const std::string& id, double fraction,
                        double eta_seconds = -1.0);
    void SetJobState(const std::string& id, QueueJobState state,
                     const std::string& error = {});
    bool RemoveJob(const std::string& id);
    void ClearFinished();   // Done/Failed/Cancelled rows
    // Driver seam: takes a submitted head row back to Queued and
    // clears its submitted marker WITHOUT re-firing on_submit — the
    // shell calls this when the engine is still draining the previous
    // (cancelled) job and the row must wait for a free worker.
    bool WithdrawSubmission(const std::string& id);
    const QueueJob* JobById(const std::string& id) const;
    const QueueJob* JobAt(int index) const;
    int JobCount() const { return static_cast<int>(jobs_.size()); }
    int ActiveCount() const;   // Queued + Running + Paused

    // ---- simulated drive (stub seam — see the header comment) ----
    void SetSimulatedDrive(bool enabled) { simulated_ = enabled; }
    bool IsSimulated() const;
    // Advances the simulation: head queued job starts, running jobs
    // progress. Deterministic — the smoke drives it with fixed dt.
    void Tick(double dt_seconds) override;

    // ---- toolbar verbs (buttons + smoke drive these) -------------
    void PauseSelected();
    void ResumeSelected();
    void CancelSelected();
    void RevealSelected();
    void RetrySelected();
    bool PauseJob(const std::string& id);
    bool ResumeJob(const std::string& id);
    bool CancelJob(const std::string& id);
    bool RevealJob(const std::string& id);
    bool RetryJob(const std::string& id);

    int SelectedIndex() const { return selected_; }
    void SetSelectedIndex(int index);
    const QueueJob* SelectedJob() const;

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }

    XER::UI::Layout::Size GetMinSize() const override {
        return {260.0f, 160.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {380.0f, 300.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit QueuePanel();

    void EnsureChildren();
    void SyncToolbar();
    void SyncStatusText();
    // Promotes the head Queued job: simulated drive flips it
    // Running itself; real drive fires on_submit once.
    void MaybeStartHead();
    void SetStatus(const std::string& text, bool is_error = false);

    std::vector<QueueJob> jobs_;
    int selected_ = -1;
    bool simulated_ = true;   // until the shell installs on_submit
    std::string submitted_head_;   // real-drive "already submitted" id

    std::shared_ptr<XER::UI::Widgets::Button> enqueue_btn_;
    std::shared_ptr<XER::UI::Widgets::VirtualList> list_;
    std::shared_ptr<XER::UI::Widgets::Button> pause_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> resume_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> cancel_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> reveal_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> retry_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> clear_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;

    bool children_attached_ = false;
    Hooks hooks_;
};

}  // namespace XER::VideoEditor::Ui2
