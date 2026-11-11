#pragma once

#include "weight_initialization.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Weights {

using SyncCallback = std::function<void(bool /*success*/, const std::string& /*error*/)>;

struct SyncTask {
    std::string tensor_name;
    bool host_to_device = true;  // true: CPU→GPU, false: GPU→CPU
};

// Async weight synchronization between host and device memory.
// Uses XER::AsyncIO::IO::Async::AsyncTaskQueue when available; falls back to
// synchronous copy otherwise.
class WeightSyncCuda {
public:
    WeightSyncCuda();
    ~WeightSyncCuda();

    // Schedule host→device sync; callback fired on completion (may be sync).
    void PushAsync(const std::string& tensor_name,
                   const std::vector<float>& host_data,
                   SyncCallback callback = nullptr);

    // Schedule device→host pull; populates out_data on completion.
    void PullAsync(const std::string& tensor_name,
                   std::vector<float>& out_data,
                   SyncCallback callback = nullptr);

    // Synchronous convenience wrappers (blocks until done).
    bool PushSync(const std::string& tensor_name,
                  const std::vector<float>& host_data);
    bool PullSync(const std::string& tensor_name,
                  std::vector<float>& out_data);

    // Drain all pending async tasks.
    void WaitAll();

    bool IsCudaAvailable() const { return cuda_available_; }

private:
    bool cuda_available_ = false;
    uint64_t pending_tasks_ = 0;
};

}  // namespace XER::ModelsBuilder::Weights
