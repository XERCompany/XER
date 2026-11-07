#include "cache/persistent_storage.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <absl/strings/str_cat.h>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>

#include "cache/cache_utils.h"

namespace XER::Engine::Cache {

// ---------------------------------------------------------------------------
// TTL sidecar helpers
// ---------------------------------------------------------------------------
namespace {

// Returns the path of the TTL sidecar file for a given cache file.
// e.g. /cache/scope/key/name  →  /cache/scope/key/name.ttl
std::filesystem::path TtlSidecarPath(const std::filesystem::path& data_path) {
	return std::filesystem::path(data_path.string() + ".ttl");
}

// Returns current Unix time as int64_t seconds.
std::int64_t NowUnixSeconds() {
	return std::chrono::duration_cast<std::chrono::seconds>(
		       std::chrono::system_clock::now().time_since_epoch())
	    .count();
}

// Writes a TTL sidecar file containing the expiry Unix timestamp (text).
// Does nothing if ttl_seconds == 0.
void WriteTtlSidecar(const std::filesystem::path& data_path,
		     std::uint32_t ttl_seconds) {
	if (ttl_seconds == 0) {
		return;
	}
	const std::int64_t expiry = NowUnixSeconds() + ttl_seconds;
	const std::string expiry_str = std::to_string(expiry) + "\n";
	const std::filesystem::path sidecar = TtlSidecarPath(data_path);
	std::ofstream f(sidecar, std::ios::trunc);
	if (f.is_open()) {
		f << expiry_str;
	}
}

// Returns true if the cache entry at data_path is expired.
// If expired, removes the data file and its sidecar.
// Returns false (not expired) when no sidecar exists.
bool CheckAndEvictIfExpired(const std::filesystem::path& data_path) {
	const std::filesystem::path sidecar = TtlSidecarPath(data_path);
	std::ifstream f(sidecar);
	if (!f.is_open()) {
		return false;  // No TTL recorded — treat as never-expiring.
	}
	std::int64_t expiry = 0;
	f >> expiry;
	if (expiry <= 0 || NowUnixSeconds() < expiry) {
		return false;  // Not yet expired.
	}
	// Expired — remove data file and sidecar (best-effort).
	std::error_code ec;
	std::filesystem::remove(data_path, ec);
	std::filesystem::remove(sidecar, ec);
	return true;
}

}  // namespace

PersistentStorage::PersistentStorage(std::filesystem::path root)
	: root_(std::move(root)) {}

const std::filesystem::path& PersistentStorage::root() const {
	return root_;
}

bool PersistentStorage::EnsureRootDirectory(std::string* error_out) const {
	std::error_code error;
	std::filesystem::create_directories(root_, error);
	if (!error) {
		return true;
	}
	if (error_out != nullptr) {
		*error_out = absl::StrCat("Failed to create cache root directory: ", root_.string(), ", reason: ", error.message());
	}
	return false;
}

std::filesystem::path PersistentStorage::DirectoryFor(absl::string_view scope,
					      absl::string_view key) const {
	return BuildScopedDirectory(root_, scope, key);
}

std::filesystem::path PersistentStorage::PathFor(const CacheEntry& entry) const {
	return DirectoryFor(entry.scope, entry.key) / SanitizeFileName(entry.name);
}

std::filesystem::path PersistentStorage::ResolveRelative(const std::filesystem::path& relative_path) const {
	return root_ / relative_path;
}

bool PersistentStorage::Exists(const CacheEntry& entry) const {
	const std::filesystem::path path = PathFor(entry);
	std::error_code error;
	if (!std::filesystem::exists(path, error) || error) {
		return false;
	}
	// Evict and report as missing if the entry has expired.
	if (CheckAndEvictIfExpired(path)) {
		return false;
	}
	return true;
}

bool PersistentStorage::Remove(const CacheEntry& entry, std::string* error_out) {
	return RemoveRelative(PathFor(entry).lexically_relative(root_), error_out);
}

bool PersistentStorage::ReadBinary(const CacheEntry& entry,
				 std::vector<std::uint8_t>* bytes,
				 std::string* error_out) const {
	return ReadBinaryRelative(PathFor(entry).lexically_relative(root_), bytes, error_out);
}

bool PersistentStorage::WriteBinary(const CacheEntry& entry,
				  const std::vector<std::uint8_t>& bytes,
				  std::string* error_out) {
	if (!WriteBinaryRelative(PathFor(entry).lexically_relative(root_), bytes, error_out)) {
		return false;
	}
	if (entry.ttl_seconds > 0) {
		WriteTtlSidecar(PathFor(entry), entry.ttl_seconds);
	}
	return true;
}

bool PersistentStorage::ExistsRelative(const std::filesystem::path& relative_path) const {
	std::error_code error;
	return std::filesystem::exists(ResolveRelative(relative_path), error) && !error;
}

bool PersistentStorage::RemoveRelative(const std::filesystem::path& relative_path,
				  std::string* error_out) {
	std::error_code error;
	std::filesystem::remove(ResolveRelative(relative_path), error);
	if (!error) {
		return true;
	}
	if (error_out != nullptr) {
		*error_out = absl::StrCat("Failed to remove cache file: ", ResolveRelative(relative_path).string(), ", reason: ", error.message());
	}
	return false;
}

bool PersistentStorage::ReadBinaryRelative(const std::filesystem::path& relative_path,
				       std::vector<std::uint8_t>* bytes,
				       std::string* error_out) const {
	if (bytes == nullptr) {
		if (error_out != nullptr) {
			*error_out = "Persistent cache bytes target is null";
		}
		return false;
	}

	const std::filesystem::path full_path = ResolveRelative(relative_path);
	std::ifstream input(full_path, std::ios::binary);
	if (!input.is_open()) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat("Failed to open cache file: ", full_path.string());
		}
		return false;
	}

	input.seekg(0, std::ios::end);
	const std::streamsize size = input.tellg();
	input.seekg(0, std::ios::beg);
	if (size < 0) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat("Failed to read cache file size: ", full_path.string());
		}
		return false;
	}

	bytes->assign(static_cast<std::size_t>(size), 0);
	if (size > 0 && !input.read(reinterpret_cast<char*>(bytes->data()), size)) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat("Failed to read cache file bytes: ", full_path.string());
		}
		return false;
	}
	return true;
}

bool PersistentStorage::WriteBinaryRelative(const std::filesystem::path& relative_path,
					const std::vector<std::uint8_t>& bytes,
					std::string* error_out) {
	if (!EnsureRootDirectory(error_out)) {
		return false;
	}

	const std::filesystem::path full_path = ResolveRelative(relative_path);
	std::error_code error;
	std::filesystem::create_directories(full_path.parent_path(), error);
	if (error) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat("Failed to create cache subdirectory: ", full_path.parent_path().string(), ", reason: ", error.message());
		}
		return false;
	}

	const std::filesystem::path temp_path = TempPathForTarget(full_path, "bin");
	std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
	if (!output.is_open()) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat("Failed to create cache temp file: ", temp_path.string());
		}
		return false;
	}

	if (!bytes.empty()) {
		output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!output.good()) {
			if (error_out != nullptr) {
				*error_out = absl::StrCat("Failed to write cache temp file: ", temp_path.string());
			}
			return false;
		}
	}

	output.flush();
	if (!output.good()) {
		if (error_out != nullptr) {
			*error_out = absl::StrCat("Failed to flush cache temp file: ", temp_path.string());
		}
		return false;
	}
	output.close();
	return CommitTempFile(temp_path, full_path, error_out);
}

bool PersistentStorage::ReadTextRelative(const std::filesystem::path& relative_path,
				     std::string* text,
				     std::string* error_out) const {
	if (text == nullptr) {
		if (error_out != nullptr) {
			*error_out = "Persistent cache text target is null";
		}
		return false;
	}

	std::vector<std::uint8_t> bytes;
	if (!ReadBinaryRelative(relative_path, &bytes, error_out)) {
		return false;
	}
	*text = bytes
		| ranges::views::transform([](std::uint8_t byte) {
			return static_cast<char>(byte);
		})
		| ranges::to<std::string>();
	return true;
}

bool PersistentStorage::WriteTextRelative(const std::filesystem::path& relative_path,
				      const std::string& text,
				      std::string* error_out) {
	const auto bytes = text
		| ranges::views::transform([](char ch) {
			return static_cast<std::uint8_t>(ch);
		})
		| ranges::to<std::vector<std::uint8_t>>();
	return WriteBinaryRelative(relative_path, bytes, error_out);
}

}  // namespace XER::Engine::Cache
