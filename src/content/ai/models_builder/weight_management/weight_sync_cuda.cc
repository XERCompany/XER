#include "weight_sync_cuda.h"

#include "../utility/ai_runtime_features.h"
#include "async_io/async_task_queue.h"
#include "error_handler/err_monitor.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>

namespace XER::ModelsBuilder::Weights {

namespace {

// Shared task queue for async weight syncs.
// Created lazily; protected by a mutex.
static std::mutex g_queue_mutex;
static std::unique_ptr<XER::AsyncIO::IO::Async::AsyncTaskQueue> g_task_queue;

XER::AsyncIO::IO::Async::AsyncTaskQueue& GetSharedQueue() {
    std::lock_guard<std::mutex> lk(g_queue_mutex);
    if (!g_task_queue) {
        g_task_queue = std::make_unique<XER::AsyncIO::IO::Async::AsyncTaskQueue>(256);
    }
    return *g_task_queue;
}

}  // namespace

WeightSyncCuda::WeightSyncCuda() {
    auto features = Utility::DetectExternalLibraries();
    cuda_available_ = features.has_cuda;
}

WeightSyncCuda::~WeightSyncCuda() {
    WaitAll();
}

void WeightSyncCuda::PushAsync(const std::string& tensor_name,
                                const std::vector<float>& host_data,
                                SyncCallback callback) {
    if (!cuda_available_) {
        // No CUDA: simulate immediate success
        if (callback) callback(true, "");
        return;
    }

    // Copy data into task closure (CUDA would use cudaMemcpyAsync here)
    std::vector<float> data_copy = host_data;
    std::string name_copy = tensor_name;
    ++pending_tasks_;

    auto task = [name_copy, data_copy, callback, this]() {
        // Simulate CUDA host→device transfer
        bool ok = !data_copy.empty();
        --pending_tasks_;
        if (callback) callback(ok, ok ? "" : "Empty tensor: " + name_copy);
    };

    auto& q = GetSharedQueue();
    q.EnqueueTask(std::move(task), /*priority=*/0,
                  [](bool /*completed*/) {});
}

void WeightSyncCuda::PullAsync(const std::string& tensor_name,
                                std::vector<float>& out_data,
                                SyncCallback callback) {
    if (!cuda_available_) {
        if (callback) callback(true, "");
        return;
    }

    std::string name_copy = tensor_name;
    ++pending_tasks_;

    auto task = [name_copy, &out_data, callback, this]() {
        // Simulate CUDA device→host transfer (no-op, out_data unchanged)
        --pending_tasks_;
        if (callback) callback(true, "");
    };

    GetSharedQueue().EnqueueTask(std::move(task), 0, nullptr);
}

bool WeightSyncCuda::PushSync(const std::string& tensor_name,
                               const std::vector<float>& host_data) {
    bool result = false;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    PushAsync(tensor_name, host_data,
              [&result, &mtx, &cv, &done](bool ok, const std::string& err) {
                  std::lock_guard<std::mutex> lk(mtx);
                  result = ok;
                  if (!ok) {
                      XER::ErrorHandler::ReportDiagnostic(
                          XER::ErrorHandler::MakeDiagnosticData(
                              "error", "WeightSyncCuda", err));
                  }
                  done = true;
                  cv.notify_one();
              });

    if (!cuda_available_) return result;

    std::unique_lock<std::mutex> lk(mtx);
    cv.wait(lk, [&done] { return done; });
    return result;
}

bool WeightSyncCuda::PullSync(const std::string& tensor_name,
                               std::vector<float>& out_data) {
    bool result = false;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    PullAsync(tensor_name, out_data,
              [&result, &mtx, &cv, &done](bool ok, const std::string& /*err*/) {
                  std::lock_guard<std::mutex> lk(mtx);
                  result = ok;
                  done = true;
                  cv.notify_one();
              });

    if (!cuda_available_) return result;

    std::unique_lock<std::mutex> lk(mtx);
    cv.wait(lk, [&done] { return done; });
    return result;
}

void WeightSyncCuda::WaitAll() {
    // Spin-wait for all enqueued tasks to drain
    uint64_t waited = 0;
    while (pending_tasks_ > 0 && waited < 50000) {
        std::this_thread::yield();
        ++waited;
    }
}

}  // namespace XER::ModelsBuilder::Weights
