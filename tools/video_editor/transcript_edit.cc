// Transcript editing helpers — see transcript_edit.h for the contract.
#include "transcript_edit.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>

#include <json/json.h>

#include "helper/path_utils.h"
#include "helper/string_utils.h"

namespace XER::VideoEditor {
namespace {

namespace Path = XER::Helper::Path;

// UTF-8 codepoint count — word time is distributed per character, not
// per byte, so CJK text doesn't skew the timing spread.
std::size_t Utf8Length(const std::string& s) {
    std::size_t n = 0;
    for (const unsigned char c : s) {
        if ((c & 0xC0) != 0x80) ++n;
    }
    return n;
}

// FNV-1a 64 — a stable hash for cache file names (std::hash is not
// guaranteed stable across builds).
std::uint64_t Fnv1a64(const std::string& s) {
    std::uint64_t h = 14695981039346656037ull;
    for (const unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::uintmax_t MediaFileSize(const std::string& utf8_path) {
    std::error_code ec;
    const auto size =
        std::filesystem::file_size(Path::FromUtf8(utf8_path), ec);
    return ec ? static_cast<std::uintmax_t>(0) : size;
}

}  // namespace

std::vector<TranscriptWord> TranscriptWordsFromSegments(
    const std::vector<Ai::TranscriptSegment>& segments) {
    std::vector<TranscriptWord> words;
    for (const auto& seg : segments) {
        // Real token timings ride on the segment when the probed
        // libwhisper exposes them — they land verbatim (sanitized).
        if (!seg.words.empty()) {
            bool first = true;
            for (const auto& ws : seg.words) {
                if (ws.text.empty() || !std::isfinite(ws.t0) ||
                    !std::isfinite(ws.t1) || ws.t1 < ws.t0) {
                    continue;
                }
                TranscriptWord w;
                w.text = ws.text;
                w.src_t0 = ws.t0;
                w.src_t1 = ws.t1;
                w.segment_start = first;
                words.push_back(std::move(w));
                first = false;
            }
            continue;
        }
        // Fallback: split on ASCII whitespace (whisper text is
        // already trimmed) and distribute the segment's span.
        std::vector<std::string> tokens;
        std::istringstream in(seg.text);
        std::string tok;
        while (in >> tok) tokens.push_back(tok);
        if (tokens.empty()) continue;
        const double span = std::max(0.0, seg.t1 - seg.t0);
        std::size_t total_len = 0;
        for (const auto& t : tokens) total_len += Utf8Length(t);
        if (total_len == 0) total_len = tokens.size();
        double cursor = seg.t0;
        bool first = true;
        for (const auto& t : tokens) {
            const double share =
                span * static_cast<double>(
                           std::max<std::size_t>(1, Utf8Length(t))) /
                static_cast<double>(total_len);
            TranscriptWord w;
            w.text = t;
            w.src_t0 = cursor;
            w.src_t1 = cursor + share;
            w.segment_start = first;
            words.push_back(std::move(w));
            cursor += share;
            first = false;
        }
        // Keep the last word flush with the segment end (rounding).
        words.back().src_t1 = seg.t1;
    }
    return words;
}

double SourceToTimeline(const Clip& clip, double src_t) {
    const double rate =
        clip.playback_rate > 0.0 ? clip.playback_rate : 1.0;
    const double consumed_end = clip.in_point + clip.duration * rate;
    if (!std::isfinite(src_t) || src_t < clip.in_point ||
        src_t > consumed_end) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return clip.timeline_start + (src_t - clip.in_point) / rate;
}

bool SourceRangeToTimeline(const Clip& clip, double src_t0,
                           double src_t1, double* tl_t0,
                           double* tl_t1) {
    const double rate =
        clip.playback_rate > 0.0 ? clip.playback_rate : 1.0;
    const double lo = clip.in_point;
    const double hi = clip.in_point + clip.duration * rate;
    const double a = std::clamp(std::min(src_t0, src_t1), lo, hi);
    const double b = std::clamp(std::max(src_t0, src_t1), lo, hi);
    if (b - a <= 1e-9) return false;   // no intersection
    const double m0 = clip.timeline_start + (a - clip.in_point) / rate;
    const double m1 = clip.timeline_start + (b - clip.in_point) / rate;
    if (tl_t0 != nullptr) *tl_t0 = m0;
    if (tl_t1 != nullptr) *tl_t1 = m1;
    return true;
}

std::string TranscriptCacheKey(const std::string& utf8_path) {
    std::string norm = Path::NormalizeUtf8Path(utf8_path);
    // Case-insensitive on Windows media paths — fold case so a path
    // spelled differently still hits the same cache entry.
    norm = XER::Helper::String::ToLowerAscii(norm);
    std::replace(norm.begin(), norm.end(), '\\', '/');
    const std::string payload =
        norm + "|" + std::to_string(MediaFileSize(utf8_path));
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx",
                  static_cast<unsigned long long>(Fnv1a64(payload)));
    return hex;
}

bool SaveTranscriptCache(
    const std::string& utf8_json_path, const std::string& media_path,
    const std::vector<Ai::TranscriptSegment>& spans) {
    Json::Value root(Json::objectValue);
    // version 2 adds the optional per-span "words" array (real token
    // timings); v1 files still load — their spans just carry no words.
    root["version"] = 2;
    root["media"] = media_path;
    root["size"] = static_cast<Json::UInt64>(MediaFileSize(media_path));
    Json::Value arr(Json::arrayValue);
    for (const auto& s : spans) {
        Json::Value j(Json::objectValue);
        j["t0"] = s.t0;
        j["t1"] = s.t1;
        j["text"] = s.text;
        if (!s.words.empty()) {
            Json::Value warr(Json::arrayValue);
            for (const auto& w : s.words) {
                Json::Value wj(Json::objectValue);
                wj["t0"] = w.t0;
                wj["t1"] = w.t1;
                wj["text"] = w.text;
                warr.append(std::move(wj));
            }
            j["words"] = std::move(warr);
        }
        arr.append(std::move(j));
    }
    root["spans"] = std::move(arr);
    std::error_code ec;
    const auto fs_path = Path::FromUtf8(utf8_json_path);
    std::filesystem::create_directories(fs_path.parent_path(), ec);
    std::ofstream out(fs_path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    Json::StreamWriterBuilder wb;
    wb["indentation"] = "";
    out << Json::writeString(wb, root);
    return static_cast<bool>(out);
}

bool LoadTranscriptCache(
    const std::string& utf8_json_path, const std::string& media_path,
    std::vector<Ai::TranscriptSegment>* out) {
    if (out == nullptr) return false;
    std::ifstream in(Path::FromUtf8(utf8_json_path),
                     std::ios::binary);
    if (!in) return false;
    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    if (!Json::parseFromStream(rb, in, &root, &errs) ||
        !root.isObject()) {
        return false;
    }
    // A stale entry (media replaced or re-exported at a new size)
    // must not silently serve the old transcript.
    const std::string stored_media = root["media"].asString();
    const auto stored_size =
        static_cast<std::uintmax_t>(root["size"].asUInt64());
    if (stored_media != media_path ||
        stored_size != MediaFileSize(media_path)) {
        return false;
    }
    std::vector<Ai::TranscriptSegment> spans;
    for (const Json::Value& j : root["spans"]) {
        Ai::TranscriptSegment s;
        s.t0 = j["t0"].asDouble();
        s.t1 = j["t1"].asDouble();
        s.text = j["text"].asString();
        if (!std::isfinite(s.t0) || !std::isfinite(s.t1) ||
            s.t1 < s.t0) {
            continue;
        }
        if (j["words"].isArray()) {
            for (const Json::Value& wj : j["words"]) {
                Ai::TranscriptWordSpan w;
                w.t0 = wj["t0"].asDouble();
                w.t1 = wj["t1"].asDouble();
                w.text = wj["text"].asString();
                if (!std::isfinite(w.t0) || !std::isfinite(w.t1) ||
                    w.t1 < w.t0 || w.text.empty()) {
                    continue;
                }
                s.words.push_back(std::move(w));
            }
        }
        spans.push_back(std::move(s));
    }
    if (spans.empty()) return false;
    *out = std::move(spans);
    return true;
}

}  // namespace XER::VideoEditor
