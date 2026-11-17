// Single-job background worker for the Object Lab (Phase 2). Long ops
// like inpainting can take seconds — AsyncOp runs one std::thread at a
// time and exposes a poll-friendly API so UI widgets (lab_panel) can
// drive progress/cancel from their per-frame Tick() without callbacks
// crossing threads.
//
//   AsyncOp op;
//   op.Start([](const Lab::JobControl& ctl) {
//       while (...) {
//           if (ctl.ShouldCancel()) return false;
//           ctl.ReportProgress(0.5f);
//       }
//       return true;
//   });
//   // UI thread, each tick:
//   op.Progress();                       // 0..1
//   if (cancel_clicked) op.RequestCancel();
//   bool ok, cancelled; std::string err;
//   if (op.PopResult(&ok, &cancelled, &err)) { /* done */ }
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace XER::VideoEditor::Lab {

// Controls handed to the Work function on the worker thread. Cheap to
// copy; all members are valid for the lifetime of the call.
class JobControl {
public:
    // Poll inside long loops. True once RequestCancel() was issued.
    bool ShouldCancel() const {
        return should_cancel_ ? should_cancel_() : false;
    }
    // Report 0..1; values are clamped by the owner.
    void ReportProgress(float fraction) const {
        if (report_progress_) report_progress_(fraction);
    }
    // Optional human-readable failure detail, readable from PopResult
    // after the job finishes.
    void Fail(std::string message) const {
        if (error_out_ != nullptr) *error_out_ = std::move(message);
    }

private:
    friend class AsyncOp;
    std::function<bool()> should_cancel_;
    std::function<void(float)> report_progress_;
    std::string* error_out_ = nullptr;
};

class AsyncOp {
public:
    // Return true on success, false on failure or cancel.
    using Work = std::function<bool(const JobControl&)>;

    AsyncOp() = default;
    ~AsyncOp();  // RequestCancel + join — never destroys mid-job.
    AsyncOp(const AsyncOp&) = delete;
    AsyncOp& operator=(const AsyncOp&) = delete;

    // False when a job is still running (or a finished result was not
    // PopResult()ed yet — pop first, then start the next job).
    bool Start(Work work);
    void RequestCancel();
    bool IsRunning() const;
    float Progress() const;  // last reported value, 0..1
    bool HasResult() const;

    // Returns true once after the job finished. `ok` is the Work return
    // value; `cancelled` reports whether a cancel was requested
    // (cancelled jobs usually also return ok=false). Joins the worker.
    bool PopResult(bool* ok, bool* cancelled, std::string* error_out);
    void Wait();  // join without cancelling

private:
    std::thread thread_;
    std::atomic<bool> cancel_requested_{false};
    std::atomic<float> progress_{0.0f};
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;   // guards the result fields below
    bool has_result_ = false;
    bool result_ok_ = false;
    bool result_cancelled_ = false;
    std::string result_error_;
};

}  // namespace XER::VideoEditor::Lab
