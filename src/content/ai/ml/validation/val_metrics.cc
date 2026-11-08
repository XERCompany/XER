#include "val_metrics.h"
#include "val_accuracy.h"
#include "val_f1score.h"
#include "val_precision.h"
#include "val_recall.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#include <absl/hash/hash.h>
#include <absl/strings/str_cat.h>

#include "async_io/io_cache_manager.h"
#include "cache/cache_entry.h"
#include "cache/cache_manager.h"
#include "flux/core/logger.h"

namespace XER::ML::Validation {
namespace {

constexpr std::uint32_t kMetricsCacheMagic = 0x3156434D;  // MCV1
constexpr std::uint32_t kMetricsCacheVersion = 1;

flux::core::Logger* g_logger = nullptr;
IO::AsyncIO::IOCacheManager* g_cache = nullptr;
std::uint64_t g_cache_ttl_seconds = 600;

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

std::size_t HashVector(const std::vector<int>& values) {
    std::size_t h = 1469598103934665603ull;
    for (int v : values) {
        h ^= static_cast<std::size_t>(v) + 0x9e3779b97f4a7c15ull;
        h *= 1099511628211ull;
    }
    h ^= values.size();
    return h;
}

std::string BuildCacheKey(const std::vector<int>& predictions,
                          const std::vector<int>& ground_truth,
                          int positive_class) {
    return absl::StrCat("metrics:", positive_class,
                        ":", HashVector(predictions),
                        ":", HashVector(ground_truth));
}

std::vector<std::uint8_t> SerializeMetrics(const ValidationMetrics& m,
                                           std::uint64_t created_at,
                                           std::uint64_t ttl) {
    std::vector<std::uint8_t> out;
    AppendPod(&out, kMetricsCacheMagic);
    AppendPod(&out, kMetricsCacheVersion);
    AppendPod(&out, created_at);
    AppendPod(&out, ttl);
    AppendPod(&out, m.accuracy);
    AppendPod(&out, m.precision);
    AppendPod(&out, m.recall);
    AppendPod(&out, m.f1_score);
    AppendPod(&out, m.macro_f1);
    AppendPod(&out, m.weighted_f1);
    const std::uint64_t samples = static_cast<std::uint64_t>(m.n_samples);
    AppendPod(&out, samples);
    return out;
}

bool DeserializeMetrics(const std::vector<std::uint8_t>& bytes,
                        ValidationMetrics* m,
                        std::uint64_t* created_at,
                        std::uint64_t* ttl) {
    std::size_t off = 0;
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t samples = 0;
    if (!ReadPod(bytes, &off, &magic) || !ReadPod(bytes, &off, &version) ||
        !ReadPod(bytes, &off, created_at) || !ReadPod(bytes, &off, ttl) ||
        !ReadPod(bytes, &off, &m->accuracy) || !ReadPod(bytes, &off, &m->precision) ||
        !ReadPod(bytes, &off, &m->recall) || !ReadPod(bytes, &off, &m->f1_score) ||
        !ReadPod(bytes, &off, &m->macro_f1) || !ReadPod(bytes, &off, &m->weighted_f1) ||
        !ReadPod(bytes, &off, &samples)) {
        return false;
    }
    if (magic != kMetricsCacheMagic || version != kMetricsCacheVersion || off != bytes.size()) {
        return false;
    }
    m->n_samples = static_cast<std::size_t>(samples);
    return true;
}

void LogInfo(const std::string& msg) {
    if (g_logger) {
        g_logger->Info("ML.Validation", msg);
    }
}

void LogError(const std::string& msg) {
    if (g_logger) {
        g_logger->Error("ML.Validation", msg);
    }
}

}  // namespace

std::string ValidationMetrics::ToString() const {
    std::ostringstream os;
    os << std::fixed << std::setprecision(4)
       << "Accuracy="   << accuracy
       << " Precision=" << precision
       << " Recall="    << recall
       << " F1="        << f1_score
       << " MacroF1="   << macro_f1
       << " WeightedF1=" << weighted_f1
       << " N="         << n_samples;
    return os.str();
}

void ValMetrics::SetLogger(flux::core::Logger* logger) {
    g_logger = logger;
}

void ValMetrics::EnableCache(IO::AsyncIO::IOCacheManager* cache) {
    g_cache = cache;
}

void ValMetrics::SetCacheTtlSeconds(std::uint64_t ttl_seconds) {
    g_cache_ttl_seconds = ttl_seconds;
}

void ValMetrics::InvalidateCache(const std::vector<int>& predictions,
                                 const std::vector<int>& ground_truth,
                                 int positive_class) {
    const std::string key = BuildCacheKey(predictions, ground_truth, positive_class);
    if (g_cache) {
        g_cache->RemoveByPrefix(key);
    }
    std::string error;
    XER::Cache::CacheManager::Instance().Persistent().Remove(
        XER::Cache::MakeEntry("ml.validation", key, "metrics.bin"), &error);
}

ValidationMetrics ValMetrics::Compute(const std::vector<int>& predictions,
                                       const std::vector<int>& ground_truth,
                                       int positive_class) {
    const std::string key = BuildCacheKey(predictions, ground_truth, positive_class);
    std::vector<std::uint8_t> bytes;
    if (g_cache) {
        g_cache->Get(key, bytes);
    }
    if (bytes.empty()) {
        std::string error;
        XER::Cache::CacheManager::Instance().ReadPersistentBinary(
            "ml.validation", key, "metrics.bin", &bytes, &error);
    }

    if (!bytes.empty()) {
        ValidationMetrics cached;
        std::uint64_t created = 0;
        std::uint64_t ttl = 0;
        if (DeserializeMetrics(bytes, &cached, &created, &ttl) &&
            ttl > 0 && UnixNowSeconds() <= created + ttl) {
            LogInfo("Validation metrics loaded from cache.");
            return cached;
        }
        InvalidateCache(predictions, ground_truth, positive_class);
        LogError("Validation cache entry invalid or expired; invalidated.");
    }

    ValidationMetrics m;
    m.n_samples    = ground_truth.size();
    m.accuracy     = ValAccuracy::Compute(predictions, ground_truth);
    m.precision    = ValPrecision::Compute(predictions, ground_truth, positive_class);
    m.recall       = ValRecall::Compute(predictions, ground_truth, positive_class);
    m.f1_score     = ValF1Score::Compute(predictions, ground_truth, positive_class);
    m.macro_f1     = ValF1Score::MacroAverage(predictions, ground_truth);
    m.weighted_f1  = ValF1Score::WeightedAverage(predictions, ground_truth);

    const auto serialized = SerializeMetrics(m, UnixNowSeconds(), g_cache_ttl_seconds);
    if (g_cache) {
        g_cache->Put(key, serialized);
    }
    std::string error;
    if (!XER::Cache::CacheManager::Instance().WritePersistentBinary(
            "ml.validation", key, "metrics.bin", serialized, &error)) {
        LogError(absl::StrCat("Validation metrics cache write failed: ", error));
    }

    return m;
}

}  // namespace XER::ML::Validation
