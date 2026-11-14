/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/civitaimodels/civitai_model_downloader.h"

#include "helper/env_utils.h"

#include <cstdlib>
#include <sstream>

#include <json/json.h>

#include "model_hub_http.h"

namespace content::civitai {

namespace {

constexpr const char* kApiBase = "https://civitai.com/api/v1";

std::string JsonString(const Json::Value& obj, const char* key) {
    if (!obj.isObject()) return {};
    const Json::Value& v = obj[key];
    // Civitai emits ids as numbers; asString() renders them correctly.
    if (v.isString() || v.isInt() || v.isUInt() || v.isInt64() ||
        v.isUInt64() || v.isDouble()) {
        return v.asString();
    }
    return {};
}

uint64_t JsonUint(const Json::Value& obj, const char* key) {
    if (!obj.isObject()) return 0;
    const Json::Value& v = obj[key];
    if (v.isUInt64()) return v.asUInt64();
    if (v.isUInt()) return v.asUInt();
    if (v.isInt64() && v.asInt64() > 0) return static_cast<uint64_t>(v.asInt64());
    if (v.isDouble()) return static_cast<uint64_t>(v.asDouble());
    return 0;
}

bool JsonToValue(const std::string& text, Json::Value* out) {
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream ss(text);
    return Json::parseFromStream(builder, ss, out, &errs);
}

CivitaiModelFile ParseModelFile(const Json::Value& f) {
    CivitaiModelFile out;
    out.name = JsonString(f, "name");
    out.type = JsonString(f, "type");
    out.format = JsonString(f, "format");
    out.size_bytes = JsonUint(f, "sizeKB") * 1024;
    out.download_url = JsonString(f, "downloadUrl");
    const Json::Value& hashes = f["hashes"];
    if (hashes.isObject()) out.sha256 = JsonString(hashes, "SHA256");
    out.primary = f.get("primaryFile", false).asBool();
    return out;
}

CivitaiModelVersion ParseModelVersion(const Json::Value& v) {
    CivitaiModelVersion out;
    out.id = JsonString(v, "id");
    out.name = JsonString(v, "name");
    const Json::Value& bm = v["baseModel"];
    out.base_model = bm.isString() ? bm.asString() : JsonString(bm, "name");
    const Json::Value& files = v["files"];
    if (files.isArray()) {
        for (const auto& f : files) out.files.push_back(ParseModelFile(f));
    }
    return out;
}

CivitaiModel ParseModel(const Json::Value& m) {
    CivitaiModel out;
    out.id = JsonString(m, "id");
    out.name = JsonString(m, "name");
    out.type = JsonString(m, "type");
    out.nsfw = m.get("nsfw", false).asBool();
    const Json::Value& stats = m["stats"];
    if (stats.isObject()) {
        out.download_count = JsonUint(stats, "downloadCount");
        out.thumbs_up_count = JsonUint(stats, "thumbsupCount");
        const Json::Value& rating = stats["rating"];
        if (rating.isDouble() || rating.isInt() || rating.isUInt()) {
            out.rating = rating.asDouble();
        }
    }
    const Json::Value& images = m["images"];
    if (images.isArray() && !images.empty()) {
        out.cover_url = JsonString(images[0], "url");
        if (out.cover_url.empty()) {
            // url may be nested under a "url"/"name" pair on some entries
            out.cover_url = JsonString(images[0], "url");
        }
    }
    const Json::Value& creator = m["creator"];
    if (creator.isObject()) out.creator = JsonString(creator, "username");
    const Json::Value& versions = m["modelVersions"];
    if (versions.isArray()) {
        for (const auto& v : versions) {
            out.versions.push_back(ParseModelVersion(v));
        }
    }
    return out;
}

// Performs an authenticated GET against the Civitai API and returns the
// parsed JSON body, or nullopt on failure.
std::optional<Json::Value> CivitaiApiGet(const std::string& path_and_query,
                                       const std::string& api_key) {
    std::string url = std::string(kApiBase) + path_and_query;
    if (!api_key.empty()) {
        url += (url.find('?') == std::string::npos ? "?" : "&");
        url += "token=" + internal::UrlEncode(api_key);
    }
    std::string body;
    int status = 0;
    std::string err;
    std::map<std::string, std::string> headers;
    headers["User-Agent"] = "XER-ModelHub/1.0";
    if (!internal::HttpGet(url, headers, &body, &status, 0, &err)) {
        return std::nullopt;
    }
    if (status < 200 || status >= 300) return std::nullopt;
    Json::Value root;
    if (!JsonToValue(body, &root)) { return std::nullopt; }
    return root;
}

}  // namespace

// ---------------------------------------------------------------------------
// CivitaiClient
// ---------------------------------------------------------------------------

CivitaiClient::CivitaiClient(std::string api_key)
    : api_key_(std::move(api_key)) {
    if (api_key_.empty()) api_key_ = ApiKeyFromEnv();
}

std::string CivitaiClient::ApiKeyFromEnv() {
    return XER::Helper::GetEnv("XER_CIVITAI_API_KEY");
}

std::optional<CivitaiModel> CivitaiClient::GetModel(
    const std::string& model_id) {
    const auto root = CivitaiApiGet("/models/" +
                                        internal::UrlEncode(model_id),
                                    api_key_);
    if (!root) return std::nullopt;
    return ParseModel(*root);
}

std::vector<CivitaiModel> CivitaiClient::SearchModels(
    const std::string& query, int limit, const std::string& types,
    const std::string& sort, int page, const std::string& cursor,
    bool include_nsfw, std::string* next_cursor_out) {
    // Civitai rejects `page` when `query` is present — browse pagination
    // uses `page`, search pagination uses `cursor` (response metadata).
    std::string q = "/models?limit=" + std::to_string(limit);
    if (!query.empty()) q += "&query=" + internal::UrlEncode(query);
    if (!cursor.empty()) {
        q += "&cursor=" + internal::UrlEncode(cursor);
    } else if (query.empty() && page > 1) {
        q += "&page=" + std::to_string(page);
    }
    if (!types.empty()) q += "&types=" + internal::UrlEncode(types);
    if (!sort.empty()) q += "&sort=" + internal::UrlEncode(sort);
    q += include_nsfw ? "&nsfw=true" : "&nsfw=false";

    const auto root = CivitaiApiGet(q, api_key_);
    if (!root) return {};

    if (next_cursor_out) {
        const Json::Value& meta = (*root)["metadata"];
        if (meta.isObject()) {
            const Json::Value& nc = meta["nextCursor"];
            if (nc.isString()) {
                *next_cursor_out = nc.asString();
            } else {
                const Json::Value& np = meta["nextPage"];
                if (np.isString()) *next_cursor_out = np.asString();
            }
        }
    }

    std::vector<CivitaiModel> out;
    const Json::Value& items = (*root)["items"];
    if (items.isArray()) {
        for (const auto& m : items) out.push_back(ParseModel(m));
    }
    return out;
}

// ---------------------------------------------------------------------------
// CivitaiModelDownloader facade
// ---------------------------------------------------------------------------

CivitaiModelDownloader& CivitaiModelDownloader::Instance() {
    static CivitaiModelDownloader inst;
    return inst;
}

CivitaiModelDownloader::CivitaiModelDownloader()
    : client_(CivitaiClient::ApiKeyFromEnv()) {}

void CivitaiModelDownloader::SetApiKey(const std::string& api_key) {
    client_.SetApiKey(api_key);
}

void CivitaiModelDownloader::SetCaBundlePath(const std::string& path) {
    base_options_.ca_bundle_path = path;
}

void CivitaiModelDownloader::SetSkipVerify(bool skip) {
    base_options_.skip_verify = skip;
}

CivitaiClient& CivitaiModelDownloader::Client() { return client_; }

std::vector<CivitaiModel> CivitaiModelDownloader::SearchModels(
    const std::string& query, int limit) {
    return client_.SearchModels(query, limit);
}

std::optional<CivitaiModel> CivitaiModelDownloader::GetModel(
    const std::string& model_id) {
    return client_.GetModel(model_id);
}

bool CivitaiModelDownloader::DownloadModelFile(
    const CivitaiModelFile& file, const std::string& model_id,
    const std::string& version_id, ProgressCallback on_progress,
    DoneCallback on_done) {
    if (file.download_url.empty() || file.name.empty()) {
        if (on_done) on_done(false, "empty file name or url", {});
        return false;
    }

    // downloadUrl is a site-relative path like
    // "/api/download/models/<versionId>?type=Model&format=SafeTensor".
    std::string url = file.download_url;
    if (url.rfind("http", 0) != 0) {
        url = "https://civitai.com" + (url.front() == '/' ? url : "/" + url);
    }

    // The /api/download endpoint authenticates with ?token=<key>.
    if (!client_.ApiKey().empty()) {
        url += (url.find('?') == std::string::npos ? "?" : "&");
        url += "token=" + internal::UrlEncode(client_.ApiKey());
    }

    ModelFileDownloader::Options opt = base_options_;
    opt.key = "civitai/" + model_id + "/" + version_id;
    opt.file_name = file.name;
    opt.expected_size = file.size_bytes;
    opt.sha256 = file.sha256;
    opt.verify_checksum = !file.sha256.empty();

    ModelFileDownloader dl;
    return dl.Download(url, opt, std::move(on_progress), std::move(on_done));
}

bool CivitaiModelDownloader::DownloadModelVersion(
    const CivitaiModelVersion& version, const std::string& model_id,
    ProgressCallback on_progress, DoneCallback on_done) {
    bool queued = false;
    for (const auto& f : version.files) {
        if (f.type != "Model") continue;
        queued |= DownloadModelFile(f, model_id, version.id, on_progress,
                                    on_done);
    }
    return queued;
}

}  // namespace content::civitai
