#include "tensor_manager.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

#include <absl/hash/hash.h>
#include <absl/strings/str_cat.h>

#include "async_io/io_cache_manager.h"
#include "async_io/io_thread_pool.h"
#include "cache/cache_manager.h"
#include "cache/cache_entry.h"
#include "flux/core/logger.h"

namespace XER::ML::Tensors {
namespace {

constexpr std::uint32_t kTensorRegistryMagic = 0x31524754;  // TGR1
constexpr std::uint32_t kTensorRegistryVersion = 1;

template <typename T>
void AppendPod(std::vector<std::uint8_t>* out, const T& value) {
    const auto* begin = reinterpret_cast<const std::uint8_t*>(&value);
    out->insert(out->end(), begin, begin + sizeof(T));
}

template <typename T>
bool ReadPod(const std::vector<std::uint8_t>& src, std::size_t* offset, T* value) {
    if (*offset + sizeof(T) > src.size()) {
        return false;
    }
    std::memcpy(value, src.data() + *offset, sizeof(T));
    *offset += sizeof(T);
    return true;
}

std::uint64_t UnixNowSeconds() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<seconds>(
        system_clock::now().time_since_epoch()).count());
}

std::string BuildCacheKey(const std::string& key) {
    return absl::StrCat("tensor.manager:", key, ":", absl::HashOf(key));
}

}  // namespace

void TensorManager::Register(const std::string& name, std::shared_ptr<Tensor> tensor) {
    registry_[name] = std::move(tensor);
}

void TensorManager::RegisterAsync(const std::string& name, std::shared_ptr<Tensor> tensor) {
    XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue(
        [this, name, tensor = std::move(tensor)]() mutable {
            Register(name, std::move(tensor));
        });
}

std::shared_ptr<Tensor> TensorManager::Get(const std::string& name) const {
    auto it = registry_.find(name);
    return (it != registry_.end()) ? it->second : nullptr;
}

bool TensorManager::Has(const std::string& name) const {
    return registry_.count(name) > 0;
}

void TensorManager::Remove(const std::string& name) {
    registry_.erase(name);
}

void TensorManager::Clear() {
    registry_.clear();
}

void TensorManager::LogInfo(const std::string& msg) const {
    if (logger_) {
        logger_->Info("ML.Tensors", msg);
    }
}

void TensorManager::LogError(const std::string& msg) const {
    if (logger_) {
        logger_->Error("ML.Tensors", msg);
    }
}

bool TensorManager::SaveToCache(const std::string& key) const {
    const std::string cache_key = BuildCacheKey(key);

    std::vector<std::uint8_t> bytes;
    AppendPod(&bytes, kTensorRegistryMagic);
    AppendPod(&bytes, kTensorRegistryVersion);
    AppendPod(&bytes, UnixNowSeconds());
    AppendPod(&bytes, cache_ttl_seconds_);

    const std::uint64_t count = static_cast<std::uint64_t>(registry_.size());
    AppendPod(&bytes, count);

    for (const auto& [name, tensor] : registry_) {
        const std::uint64_t name_len = static_cast<std::uint64_t>(name.size());
        AppendPod(&bytes, name_len);
        bytes.insert(bytes.end(), name.begin(), name.end());

        const Eigen::MatrixXf& mat = tensor->Data();
        const std::uint64_t rows = static_cast<std::uint64_t>(mat.rows());
        const std::uint64_t cols = static_cast<std::uint64_t>(mat.cols());
        AppendPod(&bytes, rows);
        AppendPod(&bytes, cols);

        const std::size_t data_bytes = static_cast<std::size_t>(rows * cols) * sizeof(float);
        const auto* ptr = reinterpret_cast<const std::uint8_t*>(mat.data());
        bytes.insert(bytes.end(), ptr, ptr + data_bytes);
    }

    if (cache_) {
        cache_->Put(cache_key, bytes);
    }

    std::string error;
    if (!XER::Cache::CacheManager::Instance().WritePersistentBinary(
            "ml.tensors", cache_key, "registry.bin", bytes, &error)) {
        LogError(absl::StrCat("Tensor cache write failed: ", error));
        return false;
    }

    LogInfo(absl::StrCat("Tensor registry cached with key: ", key));
    return true;
}

bool TensorManager::RestoreFromCache(const std::string& key) {
    const std::string cache_key = BuildCacheKey(key);

    std::vector<std::uint8_t> bytes;
    if (cache_) {
        cache_->Get(cache_key, bytes);
    }
    if (bytes.empty()) {
        std::string error;
        XER::Cache::CacheManager::Instance().ReadPersistentBinary(
            "ml.tensors", cache_key, "registry.bin", &bytes, &error);
    }
    if (bytes.empty()) {
        return false;
    }

    std::size_t off = 0;
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t created_at = 0;
    std::uint64_t ttl = 0;
    std::uint64_t count = 0;
    if (!ReadPod(bytes, &off, &magic) || !ReadPod(bytes, &off, &version) ||
        !ReadPod(bytes, &off, &created_at) || !ReadPod(bytes, &off, &ttl) ||
        !ReadPod(bytes, &off, &count) ||
        magic != kTensorRegistryMagic || version != kTensorRegistryVersion) {
        InvalidateCache(key);
        return false;
    }

    if (ttl == 0 || UnixNowSeconds() > created_at + ttl) {
        InvalidateCache(key);
        return false;
    }

    registry_.clear();
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint64_t name_len = 0;
        std::uint64_t rows = 0;
        std::uint64_t cols = 0;
        if (!ReadPod(bytes, &off, &name_len) || off + name_len > bytes.size()) {
            InvalidateCache(key);
            return false;
        }

        std::string name(reinterpret_cast<const char*>(bytes.data() + off),
                         static_cast<std::size_t>(name_len));
        off += static_cast<std::size_t>(name_len);

        if (!ReadPod(bytes, &off, &rows) || !ReadPod(bytes, &off, &cols)) {
            InvalidateCache(key);
            return false;
        }

        const std::size_t elem_count = static_cast<std::size_t>(rows * cols);
        const std::size_t data_bytes = elem_count * sizeof(float);
        if (off + data_bytes > bytes.size()) {
            InvalidateCache(key);
            return false;
        }

        Eigen::MatrixXf mat(static_cast<int>(rows), static_cast<int>(cols));
        if (elem_count > 0) {
            std::memcpy(mat.data(), bytes.data() + off, data_bytes);
        }
        off += data_bytes;

        registry_[name] = std::make_shared<Tensor>(mat);
    }

    LogInfo(absl::StrCat("Tensor registry restored from cache key: ", key));
    return true;
}

void TensorManager::InvalidateCache(const std::string& key) const {
    const std::string cache_key = BuildCacheKey(key);
    if (cache_) {
        cache_->RemoveByPrefix(cache_key);
    }

    std::string error;
    XER::Cache::CacheManager::Instance().Persistent().Remove(
        XER::Cache::MakeEntry("ml.tensors", cache_key, "registry.bin"), &error);
}

}  // namespace XER::ML::Tensors
