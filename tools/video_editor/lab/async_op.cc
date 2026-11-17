#include "async_op.h"

#include <exception>
#include <utility>

namespace XER::VideoEditor::Lab {

AsyncOp::~AsyncOp() {
    RequestCancel();
    Wait();
}

bool AsyncOp::Start(Work work) {
    if (!work) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_.load() || has_result_) return false;
        // A previous job finished and its result was popped: make sure
        // the thread handle was joined (PopResult/Wait normally did).
        cancel_requested_ = false;
        progress_ = 0.0f;
        result_error_.clear();
        result_ok_ = false;
        result_cancelled_ = false;
        running_ = true;
    }
    if (thread_.joinable()) thread_.join();

    thread_ = std::thread([this, work = std::move(work)] {
        JobControl ctl;
        ctl.should_cancel_ = [this] { return cancel_requested_.load(); };
        ctl.report_progress_ = [this](float f) {
            if (f < 0.0f) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            progress_ = f;
        };
        ctl.error_out_ = &result_error_;
        bool ok = false;
        try {
            ok = work(ctl);
        } catch (const std::exception& e) {
            result_error_ = e.what();
            ok = false;
        } catch (...) {
            result_error_ = "unknown exception in async op";
            ok = false;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            result_ok_ = ok;
            result_cancelled_ = cancel_requested_.load();
            has_result_ = true;
        }
        running_ = false;
    });
    return true;
}

void AsyncOp::RequestCancel() { cancel_requested_ = true; }

bool AsyncOp::IsRunning() const { return running_.load(); }

float AsyncOp::Progress() const { return progress_.load(); }

bool AsyncOp::HasResult() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_result_;
}

bool AsyncOp::PopResult(bool* ok, bool* cancelled,
                        std::string* error_out) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_result_) return false;
    }
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    if (ok != nullptr) *ok = result_ok_;
    if (cancelled != nullptr) *cancelled = result_cancelled_;
    if (error_out != nullptr) *error_out = result_error_;
    has_result_ = false;
    return true;
}

void AsyncOp::Wait() {
    if (thread_.joinable()) thread_.join();
    running_ = false;
}

}  // namespace XER::VideoEditor::Lab
