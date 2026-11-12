/**
 * This file is part of XER, Content open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/civitaimodels/civitai_model_downloader.h"

#include <cstdlib>
#include <sstream>

#include <json/json.h>

#include "model_hub_http.h"

namespace content::civitai {

namespace {

constexpr const char* kHubBase = "https://huggingface.co";

bool JsonToValue(const std::string& text, Json::Value* out) {
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream ss(text);
    return Json::parseFromStream(builder, ss, out, &errs);
}

std::string JsonString(const Json::Value& obj, const char* key) {
    const Json::Value& v = obj[key];
    return v.isString() ? v.asString() : std::string{};
}

uint64_t JsonUint(const Json::Value& obj, const char* key) {
    const Json::Value& v = obj[key];
    if (v.isUInt64()) return v.asUInt64();
    if (v.isUInt()) return v.asUInt();
    if (v.isInt64() && v.asInt64() > 0) return static_cast<uint64_t>(v.asInt64());
    if (v.isDouble()) return static_cast<uint64_t>(v.asDouble());
    return 0;
}

HfRepoFile ParseRepoFile(const Json::Value& f) {
    HfRepoFile out;
    out.path = JsonString(f, "rfilename");
    out.size_bytes = JsonUint(f, "size");
    // LFS files carry their sha256 in lfs.oid; regular files have none.
    const Json::Value& lfs = f["lfs"];
    if (lfs.isObject()) out.sha256 = JsonString(lfs, "oid");
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// HuggingFaceClient
// ---------------------------------------------------------------------------

HuggingFaceClient::HuggingFaceClient(std::string token)
    : token_(std::move(token)) {
    if (token_.empty()) token_ = TokenFromEnv();
}

std::string HuggingFaceClient::TokenFromEnv() {
    if (const char* v = std::getenv("XER_HF_TOKEN")) return v;
    if (const char* v = std::getenv("HF_TOKEN")) return v;
    return {};
}

std::string HuggingFaceClient::ResolveUrl(const std::string& repo_id,
                                          const std::string& path,
                                          const std::string& revision) {
    return std::string(kHubBase) + "/" + repo_id + "/resolve/" + revision +
           "/" + path;
}

std::map<std::string, std::string> HuggingFaceClient::DownloadHeaders()
    const {
    std::map<std::string, std::string> headers;
    if (!token_.empty()) headers["Authorization"] = "Bearer " + token_;
    return headers;
}

std::optional<HfRepoInfo> HuggingFaceClient::GetRepoInfo(
    const std::string& repo_id, const std::string& revision) {
    (void)revision;  // blobs listing is revision-independent in the API
    // repo_id is "org/name" — the / is a path separator, kept raw.
    const std::string url = std::string(kHubBase) + "/api/models/" +
                            repo_id + "?blobs=true";

    std::map<std::string, std::string> headers = DownloadHeaders();
    headers["User-Agent"] = "XER-ModelHub/1.0";

    std::string body;
    int status = 0;
    std::string err;
    if (!internal::HttpGet(url, headers, &body, &status, 0, &err) ||
        status != 200) {
        return std::nullopt;
    }

    Json::Value root;
    if (!JsonToValue(body, &root)) return std::nullopt;

    HfRepoInfo info;
    info.id = JsonString(root, "id");
    if (info.id.empty()) info.id = repo_id;
    info.is_private = root.get("private", false).asBool();
    info.gated = JsonString(root, "gated");

    const Json::Value& siblings = root["siblings"];
    if (siblings.isArray()) {
        for (const auto& f : siblings) {
            HfRepoFile file = ParseRepoFile(f);
            if (!file.path.empty()) info.files.push_back(std::move(file));
        }
    }
    return info;
}

}  // namespace content::civitai
