#pragma once

#include "tensor.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace flux::core { class Logger; }
namespace IO::AsyncIO { class IOCacheManager; }

namespace XER::ML::Tensors {

/// Registry for named tensors (activations, weights, etc.)
class TensorManager {
public:
    void Register(const std::string& name, std::shared_ptr<Tensor> tensor);
    void RegisterAsync(const std::string& name, std::shared_ptr<Tensor> tensor);
    std::shared_ptr<Tensor> Get(const std::string& name) const;
    bool Has(const std::string& name) const;
    void Remove(const std::string& name);
    void Clear();

    void SetLogger(flux::core::Logger* logger) { logger_ = logger; }
    void EnableCache(IO::AsyncIO::IOCacheManager* cache) { cache_ = cache; }
    void SetCacheTtlSeconds(std::uint64_t ttl_seconds) { cache_ttl_seconds_ = ttl_seconds; }

    bool SaveToCache(const std::string& key) const;
    bool RestoreFromCache(const std::string& key);
    void InvalidateCache(const std::string& key) const;

    size_t Count() const { return registry_.size(); }

private:
    void LogInfo(const std::string& msg) const;
    void LogError(const std::string& msg) const;

    std::unordered_map<std::string, std::shared_ptr<Tensor>> registry_;
    flux::core::Logger* logger_ = nullptr;
    IO::AsyncIO::IOCacheManager* cache_ = nullptr;
    std::uint64_t cache_ttl_seconds_ = 600;
};

}  // namespace XER::ML::Tensors
