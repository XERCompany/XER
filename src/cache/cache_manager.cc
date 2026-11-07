#include "cache/cache_manager.h"

#include <string>

#include "cache/cache_configuration.h"

namespace XER::Engine::Cache {
namespace {

std::filesystem::path ResolveCacheRoot() {
	return CacheConfigurationFromEnvironment().directory;
}

}  // namespace

CacheManager& CacheManager::Instance() {
	static CacheManager manager;
	return manager;
}

CacheManager::CacheManager()
	: persistent_(ResolveCacheRoot()) {
	std::string error;
	persistent_.EnsureRootDirectory(&error);
}

const std::filesystem::path& CacheManager::RootDirectory() const {
	return persistent_.root();
}

PersistentStorage& CacheManager::Persistent() {
	return persistent_;
}

const PersistentStorage& CacheManager::Persistent() const {
	return persistent_;
}

TemporaryStorage& CacheManager::Temporary() {
	return temporary_;
}

const TemporaryStorage& CacheManager::Temporary() const {
	return temporary_;
}

CacheEntry CacheManager::EntryFor(absl::string_view scope,
				  absl::string_view key,
				  absl::string_view name) const {
	return MakeEntry(scope, key, name);
}

std::filesystem::path CacheManager::DirectoryFor(absl::string_view scope,
					 absl::string_view key) const {
	return persistent_.DirectoryFor(scope, key);
}

std::filesystem::path CacheManager::PathFor(absl::string_view scope,
					absl::string_view key,
					absl::string_view name) const {
	return persistent_.PathFor(EntryFor(scope, key, name));
}

bool CacheManager::WritePersistentText(absl::string_view scope,
				       absl::string_view key,
				       absl::string_view name,
				       const std::string& text,
				       std::string* error_out) {
	return persistent_.WriteText(EntryFor(scope, key, name), text, error_out);
}

bool CacheManager::ReadPersistentText(absl::string_view scope,
				      absl::string_view key,
				      absl::string_view name,
				      std::string* text,
				      std::string* error_out) const {
	return persistent_.ReadText(EntryFor(scope, key, name), text, error_out);
}

bool CacheManager::WritePersistentBinary(absl::string_view scope,
					 absl::string_view key,
					 absl::string_view name,
					 const std::vector<std::uint8_t>& bytes,
					 std::string* error_out) {
	return persistent_.WriteBinary(EntryFor(scope, key, name), bytes, error_out);
}

bool CacheManager::ReadPersistentBinary(absl::string_view scope,
					absl::string_view key,
					absl::string_view name,
					std::vector<std::uint8_t>* bytes,
					std::string* error_out) const {
	return persistent_.ReadBinary(EntryFor(scope, key, name), bytes, error_out);
}

}  // namespace XER::Engine::Cache
