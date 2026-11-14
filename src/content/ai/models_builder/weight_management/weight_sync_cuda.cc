#include "weight_sync_cuda.h"

#include "../utility/ai_runtime_features.h"
#include "async_io/async_task_queue.h"
#include "error_handler/err_monitor.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>

#if defined(XER_AI_HAS_CUDA_LIB) && __has_include(<cuda_runtime.h>)
#  include <cuda_runtime.h>
#  define XER_WSCUDA_HAS_CUDA 1
#else
#  define XER_WSCUDA_HAS_CUDA 0
#endif

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
#if XER_WSCUDA_HAS_CUDA
    if (cuda_available_) {
        int device_count = 0;
        cuda_available_ =
            (cudaGetDeviceCount(&device_count) == cudaSuccess &&
             device_count > 0);
        if (cuda_available_) {
            cudaSetDevice(0);
        }
    }
#endif
}

WeightSyncCuda::~WeightSyncCuda() {
    WaitAll();
#if XER_WSCUDA_HAS_CUDA
    std::lock_guard<std::mutex> lk(device_mutex_);
    for (auto& [name, buf] : device_buffers_) {
        (void)name;
        if (buf.ptr) {
            cudaFree(buf.ptr);
        }
    }
    device_buffers_.clear();
#endif
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
        bool ok = false;
        std::string err;
#if XER_WSCUDA_HAS_CUDA
        {
            std::lock_guard<std::mutex> lk(device_mutex_);
            auto& buf = device_buffers_[name_copy];
            const size_t bytes = data_copy.size() * sizeof(float);
            if (!buf.ptr || buf.floats < data_copy.size()) {
                if (buf.ptr) {
                    cudaFree(buf.ptr);
                    buf.ptr = nullptr;
                    buf.floats = 0;
                }
                if (data_copy.empty() ||
                    cudaMalloc(&buf.ptr, bytes) != cudaSuccess || !buf.ptr) {
                    err = "cudaMalloc failed for " + name_copy;
                    device_buffers_.erase(name_copy);
                } else {
                    buf.floats = data_copy.size();
                }
            }
            if (buf.ptr) {
                if (cudaMemcpy(buf.ptr, data_copy.data(), bytes,
                               cudaMemcpyHostToDevice) == cudaSuccess) {
                    ok = true;
                } else {
                    err = "cudaMemcpy H2D failed for " + name_copy + ": " +
                          cudaGetErrorString(cudaGetLastError());
                }
            }
        }
#else
        ok = !data_copy.empty();
        if (!ok) {
            err = "Empty tensor: " + name_copy;
        }
#endif
        --pending_tasks_;
        if (callback) callback(ok, err);
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
        bool ok = false;
        std::string err;
#if XER_WSCUDA_HAS_CUDA
        {
            std::lock_guard<std::mutex> lk(device_mutex_);
            auto it = device_buffers_.find(name_copy);
            if (it == device_buffers_.end() || !it->second.ptr) {
                err = "No device buffer for " + name_copy;
            } else {
                out_data.resize(it->second.floats);
                const size_t bytes = it->second.floats * sizeof(float);
                if (cudaMemcpy(out_data.data(), it->second.ptr, bytes,
                               cudaMemcpyDeviceToHost) == cudaSuccess) {
                    ok = true;
                } else {
                    err = "cudaMemcpy D2H failed for " + name_copy + ": " +
                          cudaGetErrorString(cudaGetLastError());
                    out_data.clear();
                }
            }
        }
#else
        ok = true;
#endif
        --pending_tasks_;
        if (callback) callback(ok, err);
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
