/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace content::civitai {

// ---------------------------------------------------------------------------
// Civitai REST API types (api.civitai.com / civitai.com/api/v1)
// ---------------------------------------------------------------------------

// A file attached to a Civitai model version (e.g. a .safetensors checkpoint).
struct CivitaiModelFile {
    std::string name;
    std::string type;          // "Model", "Text Encoder", "VAE", ...
    std::string format;        // "SafeTensor", "Pickle", "GGUF", ...
    uint64_t    size_bytes = 0;
    std::string download_url;  // path relative to https://civitai.com
    std::string sha256;        // hex digest when supplied by the API
    bool        primary = false;
};

// A specific version of a Civitai model.
struct CivitaiModelVersion {
    std::string id;
    std::string name;
    std::string base_model;    // "SD 1.5", "SDXL 1.0", "Flux.1 D", ...
    std::vector<CivitaiModelFile> files;
};

// A Civitai model entry as returned by /api/v1/models.
struct CivitaiModel {
    std::string id;
    std::string name;
    std::string type;          // "Checkpoint", "TextualInversion", ...
    std::vector<CivitaiModelVersion> versions;

    // Browse/display metadata (populated by ParseModel).
    bool        nsfw = false;
    uint64_t    download_count = 0;
    uint64_t    thumbs_up_count = 0;
    double      rating = 0.0;      // 0..5, 0 when unrated
    std::string cover_url;         // first entry of images[]
    std::string creator;           // creator.username
};

// ---------------------------------------------------------------------------
// Hugging Face hub types (huggingface.co)
// ---------------------------------------------------------------------------

// One file inside a HF repository (from /api/models/<repo>?blobs=true).
struct HfRepoFile {
    std::string path;          // path inside the repo, e.g. "model.safetensors"
    uint64_t    size_bytes = 0;
    std::string sha256;        // LFS oid (hex) when the file is LFS-backed
};

struct HfRepoInfo {
    std::string id;            // "org/name"
    bool        is_private = false;
    std::string gated;         // "", "auto", "manual"
    std::vector<HfRepoFile> files;
};

// ---------------------------------------------------------------------------
// Shared callbacks
// ---------------------------------------------------------------------------

using ProgressCallback =
    std::function<void(uint64_t done_bytes, uint64_t total_bytes)>;
using DoneCallback =
    std::function<void(bool ok, const std::string& error,
                       const std::filesystem::path& path)>;

// ---------------------------------------------------------------------------
// Civitai REST client
// ---------------------------------------------------------------------------

// Thin client over https://civitai.com/api/v1 using the project's HTTP stack
// (XER::Network::Http::HttpTransaction). The API key is optional and is sent
// as the `token` query parameter; read from XER_CIVITAI_API_KEY when empty.
class CivitaiClient {
public:
    explicit CivitaiClient(std::string api_key = {});

    // Falls back to the XER_CIVITAI_API_KEY environment variable.
    static std::string ApiKeyFromEnv();

    void SetApiKey(const std::string& api_key) { api_key_ = api_key; }
    const std::string& ApiKey() const { return api_key_; }

    // GET /api/v1/models/<id>. Returns nullopt on transport/parse failure.
    std::optional<CivitaiModel> GetModel(const std::string& model_id);

    // GET /api/v1/models?query=...&limit=...&types=...&sort=...
    // Browse pagination uses |page|; search pagination uses |cursor|
    // (pass the value returned via |next_cursor_out| on the previous call —
    // the API rejects `page` combined with `query`).
    // Returns an empty vector on transport/parse failure.
    std::vector<CivitaiModel> SearchModels(
        const std::string& query, int limit = 20,
        const std::string& types = {}, const std::string& sort = {},
        int page = 1, const std::string& cursor = {},
        bool include_nsfw = false,
        std::string* next_cursor_out = nullptr);

private:
    std::string api_key_;
};

// ---------------------------------------------------------------------------
// Hugging Face hub client
// ---------------------------------------------------------------------------

// Client over https://huggingface.co. The token is optional (needed only for
// gated/private repos) and is sent as `Authorization: Bearer`; read from
// XER_HF_TOKEN / HF_TOKEN when empty.
class HuggingFaceClient {
public:
    explicit HuggingFaceClient(std::string token = {});

    static std::string TokenFromEnv();

    void SetToken(const std::string& token) { token_ = token; }

    // GET /api/models/<repo>?blobs=true — returns file list with sizes and
    // LFS sha256 oids. Returns nullopt on transport/parse failure.
    std::optional<HfRepoInfo> GetRepoInfo(const std::string& repo_id,
                                          const std::string& revision = "main");

    // https://huggingface.co/<repo>/resolve/<revision>/<path>
    // The endpoint answers 302 to a signed CDN URL; the downloader follows it.
    static std::string ResolveUrl(const std::string& repo_id,
                                  const std::string& path,
                                  const std::string& revision = "main");

    // Extra headers required for a download (Authorization when a token is
    // set). The downloader drops Authorization on cross-host redirects, so it
    // is safe to pass these for gated repos.
    std::map<std::string, std::string> DownloadHeaders() const;

private:
    std::string token_;
};

// ---------------------------------------------------------------------------
// Streaming model-file downloader
// ---------------------------------------------------------------------------

// Downloads a single file over HTTPS into the XER cache with:
//  * streaming writes (no full-body buffering — safe for multi-GB weights)
//  * `.part` files + HTTP Range resume across retries/restarts
//  * SHA256 verification when a hash is supplied
//  * placement via XER::Cache::CacheManager (persistent storage scope/key)
//
// All callbacks run on the async I/O thread pool thread — do not block in
// them. Download() returns false only for synchronous argument errors;
// transport failures are reported through on_done.
class ModelFileDownloader {
public:
    struct Options {
        std::string scope = "models";   // CacheManager scope
        std::string key;                // e.g. "civitai/12345/67890" or "hf/org/repo"
        std::string file_name;          // destination filename inside the key dir
        uint64_t    expected_size = 0;  // bytes; 0 = unknown / take from server
        std::string sha256;             // hex digest; empty = no verification
        bool        verify_checksum = true;
        int         max_retries = 3;
        // Extra request headers (e.g. HF Authorization). Authorization is
        // dropped automatically on cross-host redirects.
        std::map<std::string, std::string> headers;
        // PEM CA bundle path for TLS verification; empty = OpenSSL defaults
        // (or XER_CA_BUNDLE env). SetSkipVerify exists for testing only.
        std::string ca_bundle_path;
        bool        skip_verify = false;
    };

    // Asynchronous download on the shared IOThreadPool.
    bool Download(const std::string& url, const Options& options,
                  ProgressCallback on_progress = {},
                  DoneCallback on_done = {});

    // Synchronous download (also used by Download()). Blocks the calling
    // thread; returns true on success. |out_path| receives the final path.
    bool DownloadSync(const std::string& url, const Options& options,
                      ProgressCallback on_progress, std::string* error_out,
                      std::filesystem::path* out_path = nullptr);

    // True when a verified file already exists in the cache for these
    // options (name + expected size match). Used to skip re-downloads.
    static bool IsCached(const Options& options);

    // Persist a small JSON manifest next to the downloaded file through
    // CacheManager (e.g. model metadata). Best-effort; returns false on IO
    // error. Path is <key dir>/manifest.json.
    static bool WriteManifest(const Options& options, const std::string& json,
                              std::string* error_out = nullptr);

private:
    // Internal streaming engine — implemented in model_file_downloader.cc.
    struct Impl;
};

// ---------------------------------------------------------------------------
// CivitaiModelDownloader — facade combining client + downloader
// ---------------------------------------------------------------------------

// Downloads Civitai models into the XER cache via CacheManager
// (scope "models", key "civitai/<model_id>/<version_id>").
class CivitaiModelDownloader {
public:
    static CivitaiModelDownloader& Instance();

    void SetApiKey(const std::string& api_key);
    void SetCaBundlePath(const std::string& path);
    void SetSkipVerify(bool skip);

    CivitaiClient& Client();

    // Convenience wrappers around CivitaiClient.
    std::vector<CivitaiModel> SearchModels(const std::string& query,
                                           int limit = 20,
                                           const std::string& types = {},
                                           const std::string& sort = {},
                                           bool include_nsfw = false);
    std::optional<CivitaiModel> GetModel(const std::string& model_id);

    // Download one file of a model version into the cache.
    // |on_progress| reports bytes; |on_done| fires exactly once.
    // Returns false only for synchronous argument/IO errors.
    bool DownloadModelFile(const CivitaiModelFile& file,
                           const std::string& model_id,
                           const std::string& version_id,
                           ProgressCallback on_progress = {},
                           DoneCallback on_done = {});

    // Download every file marked type=="Model" for a version.
    bool DownloadModelVersion(const CivitaiModelVersion& version,
                              const std::string& model_id,
                              ProgressCallback on_progress = {},
                              DoneCallback on_done = {});

private:
    CivitaiModelDownloader();
    CivitaiClient client_;
    ModelFileDownloader::Options base_options_;
};

}  // namespace content::civitai
