#include "loader_base.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <absl/hash/hash.h>
#include <absl/strings/str_cat.h>
#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>
#include <string_view>

#include "async_io/io_cache_manager.h"
#include "cache/cache_entry.h"
#include "cache/cache_manager.h"
#include "cache/cache_storage.h"
#include "flux/core/logger.h"

namespace XER::ML::Loaders {
namespace {

constexpr std::uint32_t kDatasetCacheMagic = 0x31444358;  // XCD1
constexpr std::uint32_t kDatasetCacheVersion = 1;

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

std::string BuildCacheKey(absl::string_view loader_name,
						  absl::string_view source_path) {
	const std::size_t digest = absl::HashOf(loader_name, source_path);
	return absl::StrCat(loader_name, ":", source_path, ":", digest);
}

std::uint64_t UnixNowSeconds() {
	using namespace std::chrono;
	return static_cast<std::uint64_t>(duration_cast<seconds>(
		system_clock::now().time_since_epoch()).count());
}

std::vector<std::uint8_t> SerializeDataset(const Dataset& ds,
										   std::uint64_t created_at,
										   std::uint64_t ttl_seconds) {
	std::vector<std::uint8_t> out;
	out.reserve(64 + ds.X.size() * 8);

	AppendPod(&out, kDatasetCacheMagic);
	AppendPod(&out, kDatasetCacheVersion);
	AppendPod(&out, created_at);
	AppendPod(&out, ttl_seconds);

	const std::uint64_t rows = static_cast<std::uint64_t>(ds.X.size());
	AppendPod(&out, rows);
	for (const auto& row : ds.X) {
		const std::uint64_t cols = static_cast<std::uint64_t>(row.size());
		AppendPod(&out, cols);
		if (!row.empty()) {
			const auto* ptr = reinterpret_cast<const std::uint8_t*>(row.data());
			out.insert(out.end(), ptr, ptr + row.size() * sizeof(float));
		}
	}

	const std::uint64_t labels = static_cast<std::uint64_t>(ds.y.size());
	AppendPod(&out, labels);
	if (!ds.y.empty()) {
		const auto* ptr = reinterpret_cast<const std::uint8_t*>(ds.y.data());
		out.insert(out.end(), ptr, ptr + ds.y.size() * sizeof(int));
	}

	return out;
}

bool DeserializeDataset(const std::vector<std::uint8_t>& bytes,
						Dataset* ds,
						std::uint64_t* created_at,
						std::uint64_t* ttl_seconds) {
	if (!ds || !created_at || !ttl_seconds) {
		return false;
	}

	std::size_t off = 0;
	std::uint32_t magic = 0;
	std::uint32_t version = 0;
	if (!ReadPod(bytes, &off, &magic) || !ReadPod(bytes, &off, &version)) {
		return false;
	}
	if (magic != kDatasetCacheMagic || version != kDatasetCacheVersion) {
		return false;
	}
	if (!ReadPod(bytes, &off, created_at) || !ReadPod(bytes, &off, ttl_seconds)) {
		return false;
	}

	std::uint64_t rows = 0;
	if (!ReadPod(bytes, &off, &rows)) {
		return false;
	}
	ds->X.clear();
	ds->X.reserve(static_cast<std::size_t>(rows));

	for (std::uint64_t i = 0; i < rows; ++i) {
		std::uint64_t cols = 0;
		if (!ReadPod(bytes, &off, &cols)) {
			return false;
		}
		std::vector<float> row(static_cast<std::size_t>(cols));
		const std::size_t byte_count = row.size() * sizeof(float);
		if (off + byte_count > bytes.size()) {
			return false;
		}
		if (!row.empty()) {
			std::memcpy(row.data(), bytes.data() + off, byte_count);
		}
		off += byte_count;
		ds->X.push_back(std::move(row));
	}

	std::uint64_t labels = 0;
	if (!ReadPod(bytes, &off, &labels)) {
		return false;
	}
	ds->y.resize(static_cast<std::size_t>(labels));
	const std::size_t y_bytes = ds->y.size() * sizeof(int);
	if (off + y_bytes > bytes.size()) {
		return false;
	}
	if (!ds->y.empty()) {
		std::memcpy(ds->y.data(), bytes.data() + off, y_bytes);
	}
	off += y_bytes;

	return off == bytes.size();
}

bool IsExpired(std::uint64_t created_at, std::uint64_t ttl_seconds) {
	if (ttl_seconds == 0) {
		return true;
	}
	const std::uint64_t now = UnixNowSeconds();
	return now > created_at + ttl_seconds;
}

}  // namespace

void LoaderBase::LogInfo(absl::string_view msg) const {
	if (logger_) {
		logger_->Info("ML.Loaders", std::string_view(msg.data(), msg.size()));
	}
}

void LoaderBase::LogError(absl::string_view msg) const {
	if (logger_) {
		logger_->Error("ML.Loaders", std::string_view(msg.data(), msg.size()));
	}
}

bool LoaderBase::TryLoadCachedDataset(absl::string_view loader_name,
									  absl::string_view source_path,
									  Dataset* out) const {
	if (!cache_enabled_ || !out) {
		return false;
	}

	const std::string key = BuildCacheKey(loader_name, source_path);
	std::vector<std::uint8_t> bytes;

	if (cache_) {
		cache_->Get(key, bytes);
	}
	if (bytes.empty()) {
		std::string error;
		XER::Cache::CacheManager::Instance().ReadPersistentBinary(
			"ml.loaders", key, "dataset.bin", &bytes, &error);
	}
	if (bytes.empty()) {
		return false;
	}

	Dataset ds;
	std::uint64_t created_at = 0;
	std::uint64_t ttl = 0;
	if (!DeserializeDataset(bytes, &ds, &created_at, &ttl)) {
		InvalidateCachedDataset(loader_name, source_path);
		LogError("Cache payload parse failed; entry invalidated.");
		return false;
	}
	if (IsExpired(created_at, ttl)) {
		InvalidateCachedDataset(loader_name, source_path);
		LogInfo("Cache entry expired; entry invalidated.");
		return false;
	}

	*out = std::move(ds);
	return true;
}

void LoaderBase::StoreCachedDataset(absl::string_view loader_name,
									absl::string_view source_path,
									const Dataset& ds) const {
	if (!cache_enabled_) {
		return;
	}

	const std::string key = BuildCacheKey(loader_name, source_path);
	const auto bytes = SerializeDataset(ds, UnixNowSeconds(), cache_ttl_seconds_);

	if (cache_) {
		cache_->Put(key, bytes);
	}

	std::string error;
	if (!XER::Cache::CacheManager::Instance().WritePersistentBinary(
			"ml.loaders", key, "dataset.bin", bytes, &error)) {
		LogError(absl::StrCat("Persistent cache write failed: ", error));
	}
}

void LoaderBase::InvalidateCachedDataset(absl::string_view loader_name,
										 absl::string_view source_path) const {
	const std::string key = BuildCacheKey(loader_name, source_path);
	if (cache_) {
		cache_->RemoveByPrefix(key);
	}

	std::string error;
	XER::Cache::CacheManager::Instance().Persistent().Remove(
		XER::Cache::MakeEntry("ml.loaders", key, "dataset.bin"), &error);
}

}  // namespace XER::ML::Loaders
