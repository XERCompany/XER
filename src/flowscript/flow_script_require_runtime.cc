#include "flow_script_require.h"

#include <chrono>
#include <sstream>
#include <utility>

#include <absl/strings/str_cat.h>

#include "cache/persistent_storage.h"
#include "engine_params.h"
#include "flux/terminal/terminal_output_renderer.h"
#include "flow_script_require_support.h"
#include "helper/tool_to.h"
#include "v8/v8_script_runner.h"

namespace flow_script_detail {

using require_support::CacheMetadata;
using require_support::CachedMetadataHashes;
using require_support::ValidationCounters;
using require_support::ValidationHistory;

namespace {

void WriteRequireLine(flux::terminal::OutputStream stream, const std::string& text) {
    flux::terminal::WriteLine(stream, text);
}

// Log a debug/verbose message to stderr when ENGINE_DEBUG or ENGINE_VERBOSE is set.
void DebugLog(const EngineParams& params, const std::string& msg) {
    if (params.is_verbose() || params.debug) {
        const std::string prefix = params.timestamps
            ? "[" + std::to_string(
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count()
              ) + "] [require] "
            : "[require] ";
        WriteRequireLine(flux::terminal::OutputStream::kStderr, absl::StrCat(prefix, msg));
    }
}

bool RemoveCacheArtifact(const std::filesystem::path& artifact_path, std::string* error_out) {
    Engine::Cache::PersistentStorage storage(artifact_path.parent_path());
    if (!storage.ExistsRelative(artifact_path.filename())) {
        return true;
    }
    return storage.RemoveRelative(artifact_path.filename(), error_out);
}

}  // namespace

FlowScriptRequireRuntime::FlowScriptRequireRuntime(v8::Isolate* isolate,
                                                   v8::Local<v8::Context> context,
                                                   std::string entry_script_path,
                                                   bool sandboxed)
    : isolate_(isolate), entry_script_path_(std::move(entry_script_path)), sandboxed_(sandboxed) {
    context_.Reset(isolate_, context);
    if (sandboxed_) {
        std::error_code error;
        sandbox_root_ = std::filesystem::weakly_canonical(entry_script_path_.parent_path(), error);
        if (error) sandbox_root_.clear();
    }
}

bool FlowScriptRequireRuntime::RunEntry(std::string* error_out) {
    v8::HandleScope handle_scope(isolate_);
    auto context = context_.Get(isolate_);
    v8::Context::Scope context_scope(context);

    require_support::ScriptExecutionInfo execution_info;
    if (!require_support::PrepareScriptForExecution(entry_script_path_, &execution_info, error_out)) {
        return false;
    }

    v8::Local<v8::Value> ignored;
    return ExecuteScriptFile(execution_info.executable_path,
                             execution_info.source_kind,
                             execution_info.compiler_used,
                             &ignored,
                             error_out);
}

bool FlowScriptRequireRuntime::Require(const std::string& request,
                                       v8::Local<v8::Value>* export_value,
                                       std::string* error_out) {
    std::filesystem::path resolved = ResolveRequestPath(request);
    if (sandboxed_ && !IsWithinSandboxRoot(resolved)) {
        if (error_out != nullptr) {
            *error_out = "RequireFile blocked by sandbox: path outside entry script directory";
        }
        return false;
    }

    require_support::ScriptExecutionInfo execution_info;
    if (!require_support::PrepareScriptForExecution(resolved, &execution_info, error_out)) {
        return false;
    }

    std::filesystem::path executable_path = execution_info.executable_path;
    std::string key = executable_path.string();
    auto cache_it = module_cache_.find(key);
    if (cache_it != module_cache_.end()) {
        *export_value = cache_it->second.Get(isolate_);
        return true;
    }

    if (loading_modules_.find(key) != loading_modules_.end()) {
        if (error_out != nullptr) {
            *error_out = "Circular RequireFile detected: " + key;
        }
        return false;
    }

    loading_modules_.insert(key);

    if (!ExecuteScriptFile(executable_path,
                           execution_info.source_kind,
                           execution_info.compiler_used,
                           export_value,
                           error_out)) {
        loading_modules_.erase(key);
        return false;
    }

    v8::Global<v8::Value> persistent(isolate_, *export_value);
    module_cache_[key] = std::move(persistent);
    loading_modules_.erase(key);
    return true;
}

bool FlowScriptRequireRuntime::ExecuteScriptFile(const std::filesystem::path& path,
                                                 const std::string& source_kind,
                                                 const std::string& compiler_used,
                                                 v8::Local<v8::Value>* export_value,
                                                 std::string* error_out) {
    auto context = context_.Get(isolate_);

    std::string lock_error;
    auto cache_lock = require_support::AcquireScriptCacheLock(path, &lock_error);
    if (!cache_lock) {
        if (error_out != nullptr) {
            *error_out = lock_error;
        }
        return false;
    }

    if (!std::filesystem::exists(path)) {
        if (error_out != nullptr) {
            *error_out = "Script file not found: " + path.string();
        }
        return false;
    }

    std::string source = ToolTo::ReadTextFile(path.string());
    if (source.empty() && std::filesystem::file_size(path) > 0) {
        if (error_out != nullptr) {
            *error_out = "Failed to read script: " + path.string();
        }
        return false;
    }

    const std::filesystem::path cache_path_default     = require_support::CachePathForScript(path);
    const std::filesystem::path metadata_path_default  = require_support::MetadataPathForScript(path);
    const std::filesystem::path diff_text_path_default = require_support::DiffTextPathForScript(path);

    // Build params once from env so every decision uses a consistent snapshot.
    const EngineParams params = EngineParamsFromEnv();

    // If --cache_dir was provided, redirect cache/metadata/diff files there.
    std::filesystem::path cache_path;
    std::filesystem::path metadata_path;
    std::filesystem::path diff_text_path;
    if (!params.cache_dir.empty()) {
        std::error_code ec;
        const std::filesystem::path cdir(params.cache_dir);
        std::filesystem::create_directories(cdir, ec);
        cache_path     = cdir / cache_path_default.filename();
        metadata_path  = cdir / metadata_path_default.filename();
        diff_text_path = cdir / diff_text_path_default.filename();
    } else {
        cache_path     = cache_path_default;
        metadata_path  = metadata_path_default;
        diff_text_path = diff_text_path_default;
    }

    const bool disable_cache  = params.nocacherequire || params.cache_readonly;
    const bool print_diff     = params.print_diff;
    const bool noemit         = params.noemit;
    const bool diff_only      = params.diff_only;

    // --cache_clean: remove stale cache files for this script before proceeding.
    if (params.cache_clean) {
        std::string cleanup_error;
        (void)RemoveCacheArtifact(cache_path, &cleanup_error);
        (void)RemoveCacheArtifact(metadata_path, &cleanup_error);
        (void)RemoveCacheArtifact(diff_text_path, &cleanup_error);
        if (!cleanup_error.empty()) {
            DebugLog(params, absl::StrCat("  cache_clean warning: ", cleanup_error));
        }
        DebugLog(params, "  cache_clean: removed stale cache for " + path.string());
    }

    DebugLog(params, "require: " + path.string());
    DebugLog(params, "  source_kind=" + source_kind + " compiler=" + compiler_used);
    if (disable_cache) DebugLog(params, "  cache disabled (nocacherequire or cache_readonly)");

    std::string compiled_source;
    bool cache_ok = false;
    bool cache_rebuilt = false;
    bool cache_exists = std::filesystem::exists(cache_path);
    std::string validation_mode = "rebuild";
    std::string validation_reason = "cache_missing_or_invalid";
    ValidationCounters validation_counters;
    ValidationHistory validation_history;
    require_support::ReadValidationCounters(metadata_path, &validation_counters);
    require_support::ReadValidationHistory(metadata_path, &validation_history);

    bool validation_counter_committed = false;
    auto commit_validation_counter = [&]() {
        if (validation_counter_committed) {
            return;
        }
        require_support::IncrementValidationCounter(&validation_counters, validation_mode);
        validation_counter_committed = true;
    };

    FileDiff diff_snapshot(source, source);

    std::vector<std::string> require_stack_paths;
    require_stack_paths.reserve(stack_.size());
    for (const auto& frame : stack_) {
        require_stack_paths.push_back(frame.path.string());
    }

    std::vector<std::string> cached_module_keys;
    cached_module_keys.reserve(module_cache_.size());
    for (const auto& item : module_cache_) {
        cached_module_keys.push_back(item.first);
    }

    const std::string parent_path = stack_.empty() ? std::string() : stack_.back().path.string();
    const std::string source_sha256 = require_support::Sha256Hex(source);
    std::string compiled_sha256;
    std::string cache_binary_sha256;

    if (disable_cache) {
        validation_mode = "rebuild";
        validation_reason = "cache_disabled_by_flag";
    } else if (cache_exists) {
        std::string cached_expanded;
        std::string cache_error;
        if (require_support::ReadExpandedTextFromCache(cache_path, &cached_expanded, &cache_error)) {
            const std::string cached_expanded_sha256 = require_support::Sha256Hex(cached_expanded);

            std::string cached_binary_sha256;
            {
                std::vector<std::uint8_t> cache_bytes;
                std::string cache_bytes_error;
                if (require_support::ReadBinaryFile(cache_path, &cache_bytes, &cache_bytes_error)) {
                    cached_binary_sha256 = require_support::Sha256Hex(cache_bytes);
                }
            }

            CachedMetadataHashes metadata_hashes;
            const bool has_metadata_hashes =
                require_support::ReadCacheMetadataHashes(metadata_path, &metadata_hashes);

            bool hash_match = false;
            if (has_metadata_hashes) {
                hash_match = (metadata_hashes.script_sha256 == source_sha256)
                             && (metadata_hashes.expanded_sha256 == cached_expanded_sha256)
                             && (metadata_hashes.cache_sha256.empty()
                                 || cached_binary_sha256.empty()
                                 || metadata_hashes.cache_sha256 == cached_binary_sha256);
            }

            if (hash_match) {
                compiled_source = std::move(cached_expanded);
                cache_ok = true;
                validation_mode = "hash";
                validation_reason = "metadata_sha256_match";
            } else {
                FileDiff diff(source, cached_expanded);
                const auto& diff_result = diff.returnDiffFile();
                diff_snapshot = diff;
                if (diff_result.before == diff_result.after) {
                    compiled_source = std::move(cached_expanded);
                    cache_ok = true;
                    validation_mode = "diff";
                    validation_reason = "expanded_text_equal";
                } else {
                    validation_mode = "diff";
                    validation_reason = "expanded_text_changed";
                }
            }
        } else {
            validation_mode = "rebuild";
            validation_reason = "cache_read_or_decompress_failed";
        }
    }

    auto write_metadata = [&](const std::string& status, const std::string& message) {
        CacheMetadata metadata{};
        metadata.script_path = path;
        metadata.cache_path = cache_path;
        metadata.entry_path = entry_script_path_;
        metadata.parent_path = parent_path;
        metadata.script_source_kind = source_kind;
        metadata.typescript_compiler = compiler_used;
        metadata.require_stack_paths = require_stack_paths;
        metadata.cached_module_keys = cached_module_keys;
        metadata.source_sha256 = source_sha256;
        metadata.compiled_sha256 = compiled_sha256;
        metadata.cache_binary_sha256 = cache_binary_sha256;
        metadata.diff = diff_snapshot.returnDiffFile();
        metadata.cache_exists = cache_exists;
        metadata.cache_valid = cache_ok || metadata.diff.equal;
        metadata.cache_rebuilt = cache_rebuilt;
        metadata.used_cache = !disable_cache;
        metadata.validation_mode = validation_mode;
        metadata.validation_reason = validation_reason;
        metadata.validation_counters = validation_counters;
        metadata.validation_history = validation_history;
        metadata.status = status;
        metadata.error_message = message;

        std::string meta_error;
        if (!require_support::WriteCacheMetadataFile(metadata_path, metadata, &meta_error)) {
            if (error_out != nullptr) {
                *error_out = meta_error;
            }
            return false;
        }
        return true;
    };

    if (!cache_ok) {
        if (disable_cache) {
            compiled_source = source;
            FileDiff rebuilt_diff(source, compiled_source);
            diff_snapshot = rebuilt_diff;
        } else {
            std::string cache_error;
            if (!require_support::RebuildCacheFromExpandedText(cache_path, source, &cache_error)) {
                commit_validation_counter();
                require_support::PushValidationEvent(&validation_history, validation_mode, validation_reason,
                                                     "cache_rebuild_failed");
                write_metadata("cache_rebuild_failed", cache_error);
                if (error_out != nullptr) {
                    *error_out = cache_error;
                }
                return false;
            }
            cache_rebuilt = true;
            cache_exists = true;

            if (!require_support::ReadExpandedTextFromCache(cache_path, &compiled_source, &cache_error)) {
                commit_validation_counter();
                require_support::PushValidationEvent(&validation_history, validation_mode, validation_reason,
                                                     "cache_read_after_rebuild_failed");
                write_metadata("cache_read_after_rebuild_failed", cache_error);
                if (error_out != nullptr) {
                    *error_out = cache_error;
                }
                return false;
            }

            FileDiff rebuilt_diff(source, compiled_source);
            diff_snapshot = rebuilt_diff;
        }
    }

    compiled_sha256 = require_support::Sha256Hex(compiled_source);
    if (!disable_cache) {
        std::vector<std::uint8_t> cache_bytes;
        std::string cache_bytes_error;
        if (require_support::ReadBinaryFile(cache_path, &cache_bytes, &cache_bytes_error)) {
            cache_binary_sha256 = require_support::Sha256Hex(cache_bytes);
        }
    }

    commit_validation_counter();
    require_support::PushValidationEvent(&validation_history, validation_mode, validation_reason, "ready");

    // In noemit mode, skip writing diff and metadata files.
    if (!noemit) {
        std::string diff_error;
        if (!require_support::WriteDiffText(diff_text_path, diff_snapshot.returnDiffFile(), &diff_error)) {
            if (error_out != nullptr) {
                *error_out = diff_error;
            }
            return false;
        }
    } else {
        DebugLog(params, "  noemit: skipping WriteDiffText for " + path.string());
    }

    if (print_diff) {
        const auto& diff_result = diff_snapshot.returnDiffFile();
        const char* eq = diff_result.equal ? "true" : "false";
        std::ostringstream diff_header;
        if (!params.colors_disabled()) {
            diff_header << "\033[36m[require:diff]\033[0m script=" << path.string()
                        << " equal=" << eq
                        << " added=" << diff_result.added_count
                        << " removed=" << diff_result.removed_count;
        } else {
            diff_header << "[require:diff] script=" << path.string()
                        << " equal=" << eq
                        << " added=" << diff_result.added_count
                        << " removed=" << diff_result.removed_count;
        }
        WriteRequireLine(flux::terminal::OutputStream::kStdout, diff_header.str());
        for (const auto& op : diff_result.operations) {
            const char sign = (op.type == FileDiffOperationType::Added) ? '+' : '-';
            WriteRequireLine(flux::terminal::OutputStream::kStdout,
                absl::StrCat(std::string(1, sign), " ", op.line));
        }
    }

    // In diff_only mode, stop here — do not execute the script.
    if (diff_only) {
        DebugLog(params, "  diff_only: skipping execution of " + path.string());
        *export_value = v8::Undefined(isolate_);
        return true;
    }

    if (!noemit) {
        if (!write_metadata("ready", "")) {
            return false;
        }
    }

    DebugLog(params, "  executing " + path.string());

    stack_.push_back({path});

    v8::Local<v8::Value> result;
    if (!Engine::V8Runtime::CompileAndRun(
            isolate_, context, compiled_source, path.string(), &result, error_out)) {
        stack_.pop_back();
        return false;
    }

    ScriptFrame frame = std::move(stack_.back());
    stack_.pop_back();

    if (frame.has_export) {
        *export_value = frame.export_value.Get(isolate_);
    } else {
        *export_value = v8::Undefined(isolate_);
    }
    return true;
}

void FlowScriptRequireRuntime::SetCurrentExport(v8::Local<v8::Value> value) {
    if (stack_.empty()) {
        return;
    }

    ScriptFrame& frame = stack_.back();
    frame.has_export = true;
    frame.export_value.Reset(isolate_, value);
}

bool FlowScriptRequireRuntime::AssignNamedGlobal(v8::Local<v8::String> name,
                                                 v8::Local<v8::Value> value) {
    if (name.IsEmpty() || name->Length() == 0) {
        return false;
    }

    auto context = context_.Get(isolate_);
    return context->Global()->Set(context, name, value).FromMaybe(false);
}

bool FlowScriptRequireRuntime::AssignObjectToGlobal(v8::Local<v8::Object> object) {
    auto context = context_.Get(isolate_);
    v8::Local<v8::Array> keys;
    if (!object->GetOwnPropertyNames(context).ToLocal(&keys)) {
        return false;
    }

    for (uint32_t i = 0; i < keys->Length(); ++i) {
        v8::Local<v8::Value> key;
        if (!keys->Get(context, i).ToLocal(&key) || !key->IsName()) {
            continue;
        }

        v8::Local<v8::Value> value;
        if (!object->Get(context, key).ToLocal(&value)) {
            continue;
        }

        if (!context->Global()->Set(context, key.As<v8::Name>(), value).FromMaybe(false)) {
            return false;
        }
    }
    return true;
}

std::filesystem::path FlowScriptRequireRuntime::ResolveRequestPath(
    const std::string& request) const {
    std::filesystem::path request_path(request);
    if (request_path.is_absolute()) {
        std::filesystem::path absolute = request_path.lexically_normal();
        if (!absolute.has_extension()) {
            std::filesystem::path js_candidate = absolute;
            js_candidate += ".js";
            if (std::filesystem::exists(js_candidate)) {
                return js_candidate;
            }
            std::filesystem::path ts_candidate = absolute;
            ts_candidate += ".ts";
            if (std::filesystem::exists(ts_candidate)) {
                return ts_candidate;
            }
        }
        return absolute;
    }

    std::filesystem::path base = entry_script_path_.parent_path();
    if (!stack_.empty()) {
        base = stack_.back().path.parent_path();
    }

    std::filesystem::path resolved = (base / request_path).lexically_normal();
    if (!resolved.has_extension()) {
        std::filesystem::path js_candidate = resolved;
        js_candidate += ".js";
        if (std::filesystem::exists(js_candidate)) {
            return js_candidate;
        }
        std::filesystem::path ts_candidate = resolved;
        ts_candidate += ".ts";
        if (std::filesystem::exists(ts_candidate)) {
            return ts_candidate;
        }
    }
    return resolved;
}

bool FlowScriptRequireRuntime::IsWithinSandboxRoot(const std::filesystem::path& path) const {
    if (sandbox_root_.empty()) return false;

    std::error_code error;
    const std::filesystem::path canonical_path = std::filesystem::weakly_canonical(path, error);
    if (error) return false;

    const std::filesystem::path relative_path = canonical_path.lexically_relative(sandbox_root_);
    if (relative_path.empty()) return canonical_path == sandbox_root_;
    return !relative_path.is_absolute() && *relative_path.begin() != "..";
}

}  // namespace flow_script_detail
