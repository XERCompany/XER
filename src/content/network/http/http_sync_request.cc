/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_sync_request.h"
#include "helper/string_utils.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <curl/curl.h>

#include <algorithm>
#include <cctype>

namespace XER::network::http {
namespace {

size_t WriteBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// Header lines arrive "Name: value\r\n"; informational 1xx blocks are
// skipped by curl before this callback — only the final header block and
// trailers reach us.
size_t WriteHeader(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    std::string_view line(ptr, size * nmemb);
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return size * nmemb;  // status line / stray line
    }
    const std::string name = XER::Engine::Helper::String::ToLowerAscii(line.substr(0, colon));
    std::string value(line.substr(colon + 1));
    value = XER::Engine::Helper::String::TrimAsciiWhitespaceCopy(value);
    (*headers)[std::move(name)] = std::move(value);
    return size * nmemb;
}

}  // namespace

SyncResponse SyncRequest(const std::string& method,
                         const std::string& url,
                         const std::string& body,
                         const std::vector<std::string>& headers,
                         long timeout_ms,
                         const std::string& user_agent) {
    SyncResponse r;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        r.error = "curl: init failed";
        return r;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, WriteHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &r.headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                     std::min(timeout_ms, 15000L));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent.c_str());
    if (method == "HEAD") {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }

    if (!body.empty() || method == "POST" || method == "PUT" ||
        method == "PATCH") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(body.size()));
    }

    curl_slist* hdrs = nullptr;
    for (const auto& h : headers) {
        hdrs = curl_slist_append(hdrs, h.c_str());
    }
    if (hdrs != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    }

    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
        r.transport_ok = true;
    } else {
        r.error = curl_easy_strerror(rc);
    }

    if (hdrs != nullptr) curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    return r;
}

}  // namespace XER::network::http
