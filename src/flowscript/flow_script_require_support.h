#pragma once

#include <v8.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "helper/file_diff.h"

namespace XER {
namespace flow_script_detail::require_support {

struct CachedMetadataHashes {
    std::string script_sha256;
    std::string expanded_sha256;
    std::string cache_sha256;
};

struct ValidationCounters {
    std::uint64_t total = 0;
    std::uint64_t hash = 0;
    std::uint64_t diff = 0;
    std::uint64_t rebuild = 0;
};

struct ValidationEvent {
    std::string mode;
    std::string reason;
    std::string status;
    std::string timestamp_ticks;
};

using ValidationHistory = std::vector<ValidationEvent>;

struct ScriptExecutionInfo {
    std::filesystem::path executable_path;
    std::string source_kind;
    std::string compiler_used;
};

struct CacheMetadata {
    std::filesystem::path script_path;
    std::filesystem::path cache_path;
    std::filesystem::path entry_path;
    std::string parent_path;
    std::string script_source_kind;
    std::string typescript_compiler;
    std::vector<std::string> require_stack_paths;
    std::vector<std::string> cached_module_keys;

    std::string source_sha256;
    std::string compiled_sha256;
    std::string cache_binary_sha256;

    FileDiffReturn diff;

    bool cache_exists = false;
    bool cache_valid = false;
    bool cache_rebuilt = false;
    bool used_cache = false;

    std::string validation_mode;
    std::string validation_reason;
    ValidationCounters validation_counters;
    ValidationHistory validation_history;

    std::string status;
    std::string error_message;
};

class ScriptCacheLock {
public:
    explicit ScriptCacheLock(std::filesystem::path lock_path);
    ~ScriptCacheLock();

private:
    std::filesystem::path lock_path_;
    bool locked_ = true;
};

std::string ToUtf8(v8::Isolate* isolate, v8::Local<v8::Value> value);

std::filesystem::path CachePathForScript(const std::filesystem::path& script_path);
std::filesystem::path MetadataPathForScript(const std::filesystem::path& script_path);
std::filesystem::path DiffTextPathForScript(const std::filesystem::path& script_path);

std::unique_ptr<ScriptCacheLock> AcquireScriptCacheLock(const std::filesystem::path& script_path,
                                                        std::string* error_out);

bool PrepareScriptForExecution(const std::filesystem::path& source_path,
                               ScriptExecutionInfo* execution_info,
                               std::string* error_out);

std::string Sha256Hex(const std::string& text);
std::string Sha256Hex(const std::vector<std::uint8_t>& bytes);

bool ReadCacheMetadataHashes(const std::filesystem::path& metadata_path,
                             CachedMetadataHashes* out_hashes);
bool ReadValidationCounters(const std::filesystem::path& metadata_path,
                            ValidationCounters* counters);
bool ReadValidationHistory(const std::filesystem::path& metadata_path,
                           ValidationHistory* history);

void IncrementValidationCounter(ValidationCounters* counters, const std::string& mode);
void PushValidationEvent(ValidationHistory* history,
                         const std::string& mode,
                         const std::string& reason,
                         const std::string& status);

bool WriteCacheMetadataFile(const std::filesystem::path& meta_path,
                            const CacheMetadata& metadata,
                            std::string* error_out);
bool WriteDiffText(const std::filesystem::path& diff_path,
                   const FileDiffReturn& diff,
                   std::string* error_out);

bool ReadBinaryFile(const std::filesystem::path& path,
                    std::vector<std::uint8_t>* bytes,
                    std::string* error_out);
bool WriteBinaryFile(const std::filesystem::path& path,
                     const std::vector<std::uint8_t>& bytes,
                     std::string* error_out);
bool ReadExpandedTextFromCache(const std::filesystem::path& cache_path,
                               std::string* text,
                               std::string* error_out);
bool RebuildCacheFromExpandedText(const std::filesystem::path& cache_path,
                                  const std::string& expanded_text,
                                  std::string* error_out);

}  // namespace flow_script_detail::require_support

}  // namespace XER
