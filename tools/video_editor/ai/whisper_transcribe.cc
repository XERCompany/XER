// Whisper transcription via runtime-probed libwhisper. The build only
// needs <whisper.h> for the struct layout (guarded by __has_include —
// feature compiles out when absent); the DLL itself is LoadLibrary'ed
// at first use so the exe never hard-depends on it.
#include "ai/whisper_transcribe.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__has_include)
#if __has_include(<whisper.h>)
#include <whisper.h>
#define XER_VE_HAVE_WHISPER_H 1
#endif
#endif

namespace XER::VideoEditor::Ai {
namespace {

#if defined(XER_VE_HAVE_WHISPER_H) && defined(_WIN32)

struct WhisperApi {
    HMODULE dll = nullptr;
    whisper_context* (*init_from_file)(const char*) = nullptr;
    void (*free_ctx)(whisper_context*) = nullptr;
    whisper_full_params (*default_params)(
        enum whisper_sampling_strategy) = nullptr;
    int (*full)(whisper_context*, whisper_full_params, const float*,
                int) = nullptr;
    int (*n_segments)(whisper_context*) = nullptr;
    int64_t (*seg_t0)(whisper_context*, int) = nullptr;
    int64_t (*seg_t1)(whisper_context*, int) = nullptr;
    const char* (*seg_text)(whisper_context*, int) = nullptr;
    // OPTIONAL token-level API — word timings. Absent symbols only
    // disable the per-word path; segment results are unaffected.
    int (*n_tokens)(whisper_context*, int) = nullptr;
    const char* (*token_text)(whisper_context*, int, int) = nullptr;
    whisper_token_data (*token_tstamps)(whisper_context*, int, int) =
        nullptr;
    whisper_token (*token_eot)(whisper_context*) = nullptr;
    std::string detail;  // resolved path or failure reason
};

const WhisperApi& Api() {
    static const WhisperApi api = [] {
        WhisperApi a;
        const wchar_t* names[] = {L"libwhisper-1.dll", L"whisper.dll",
                                  L"libwhisper.dll"};
        for (const wchar_t* name : names) {
            a.dll = LoadLibraryW(name);
            if (a.dll != nullptr) {
                wchar_t full[MAX_PATH] = {};
                GetModuleFileNameW(a.dll, full, MAX_PATH);
                a.detail =
                    std::filesystem::path(full).filename().string();
                break;
            }
        }
        if (a.dll == nullptr) {
            a.detail = "libwhisper DLL not found on PATH";
            return a;
        }
        const auto sym = [&](const char* n) {
            return reinterpret_cast<void*>(GetProcAddress(a.dll, n));
        };
        a.init_from_file = reinterpret_cast<decltype(a.init_from_file)>(
            sym("whisper_init_from_file"));
        a.free_ctx = reinterpret_cast<decltype(a.free_ctx)>(
            sym("whisper_free"));
        a.default_params = reinterpret_cast<decltype(a.default_params)>(
            sym("whisper_full_default_params"));
        a.full = reinterpret_cast<decltype(a.full)>(sym("whisper_full"));
        a.n_segments = reinterpret_cast<decltype(a.n_segments)>(
            sym("whisper_full_n_segments"));
        a.seg_t0 = reinterpret_cast<decltype(a.seg_t0)>(
            sym("whisper_full_get_segment_t0"));
        a.seg_t1 = reinterpret_cast<decltype(a.seg_t1)>(
            sym("whisper_full_get_segment_t1"));
        a.seg_text = reinterpret_cast<decltype(a.seg_text)>(
            sym("whisper_full_get_segment_text"));
        a.n_tokens = reinterpret_cast<decltype(a.n_tokens)>(
            sym("whisper_full_n_tokens"));
        a.token_text = reinterpret_cast<decltype(a.token_text)>(
            sym("whisper_full_get_token_text"));
        // whisper.cpp >= 1.5 splits timestamp reads into
        // whisper_full_get_token_tstamp_data; older versions fold
        // t0/t1 into whisper_full_get_token_data — same struct,
        // same signature, try the new name first.
        a.token_tstamps = reinterpret_cast<decltype(a.token_tstamps)>(
            sym("whisper_full_get_token_tstamp_data"));
        if (a.token_tstamps == nullptr) {
            a.token_tstamps =
                reinterpret_cast<decltype(a.token_tstamps)>(
                    sym("whisper_full_get_token_data"));
        }
        a.token_eot = reinterpret_cast<decltype(a.token_eot)>(
            sym("whisper_token_eot"));
        const bool ok = a.init_from_file && a.free_ctx &&
                        a.default_params && a.full && a.n_segments &&
                        a.seg_t0 && a.seg_t1 && a.seg_text;
        if (!ok) {
            a.detail += " (missing exports)";
            FreeLibrary(a.dll);
            a.dll = nullptr;
        }
        return a;
    }();
    return api;
}

// One-line trim for marker labels.
std::string Trim(std::string s) {
    const auto not_space = [](unsigned char c) {
        return c != ' ' && c != '\t' && c != '\n' && c != '\r';
    };
    const auto b = std::find_if(s.begin(), s.end(), not_space);
    const auto e = std::find_if(s.rbegin(), s.rend(), not_space).base();
    if (b >= e) return {};
    return std::string(b, e);
}

#endif  // XER_VE_HAVE_WHISPER_H && _WIN32

// First ggml*.bin in `dir` (non-recursive) — model naming is uniform.
std::string FirstModelIn(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return {};
    for (const auto& entry :
         std::filesystem::directory_iterator(dir, ec)) {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file(ec) && name.rfind("ggml", 0) == 0 &&
            entry.path().extension() == ".bin") {
            return entry.path().string();
        }
    }
    return {};
}

}  // namespace

bool WhisperAvailable(std::string* detail) {
#if defined(XER_VE_HAVE_WHISPER_H) && defined(_WIN32)
    const auto& api = Api();
    if (detail) *detail = api.detail;
    return api.dll != nullptr;
#else
    if (detail) {
        *detail = "built without whisper.h / non-Windows target";
    }
    return false;
#endif
}

std::string FindWhisperModel() {
    if (const char* env = std::getenv("XER_WHISPER_MODEL");
        env != nullptr && *env != '\0' &&
        std::filesystem::exists(env)) {
        return env;
    }
    std::error_code ec;
    std::vector<std::filesystem::path> dirs;
#if defined(_WIN32)
    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) != 0) {
        dirs.push_back(std::filesystem::path(exe).parent_path() /
                       "models");
    }
#endif
    if (const char* home = std::getenv("USERPROFILE");
        home != nullptr && *home != '\0') {
        dirs.push_back(std::filesystem::path(home) / ".cache" /
                       "whisper");
    }
    if (const char* xdg = std::getenv("XDG_CACHE_HOME");
        xdg != nullptr && *xdg != '\0') {
        dirs.push_back(std::filesystem::path(xdg) / "whisper");
    }
    for (const auto& dir : dirs) {
        if (std::string found = FirstModelIn(dir); !found.empty()) {
            return found;
        }
    }
    return {};
}

bool Transcribe(const std::vector<float>& samples,
                const std::string& model_path,
                const std::function<void(float)>& progress,
                std::vector<TranscriptSegment>* out,
                std::string* error_out) {
    if (out == nullptr) return false;
    out->clear();
#if !(defined(XER_VE_HAVE_WHISPER_H) && defined(_WIN32))
    (void)samples;
    (void)model_path;
    (void)progress;
    if (error_out) *error_out = "whisper not built into this binary";
    return false;
#else
    const auto& api = Api();
    if (api.dll == nullptr) {
        if (error_out) *error_out = api.detail;
        return false;
    }
    if (samples.empty()) {
        if (error_out) *error_out = "empty audio";
        return false;
    }

    whisper_context* ctx = api.init_from_file(model_path.c_str());
    if (ctx == nullptr) {
        if (error_out) {
            *error_out = "failed to load model: " + model_path;
        }
        return false;
    }

    whisper_full_params params =
        api.default_params(WHISPER_SAMPLING_GREEDY);
    params.language = "auto";
    params.translate = false;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.single_segment = false;
    params.no_timestamps = false;
    // Ask for token timestamps so per-word spans can be harvested
    // below when the DLL exports the token API — a no-cost flag when
    // it doesn't.
    params.token_timestamps = true;
    params.n_threads = std::max<int>(
        1, static_cast<int>(std::min<unsigned>(
               8, std::thread::hardware_concurrency())));
    params.progress_callback = nullptr;

    const int rc =
        api.full(ctx, params, samples.data(),
                 static_cast<int>(samples.size()));
    if (progress) progress(1.0f);
    if (rc != 0) {
        api.free_ctx(ctx);
        if (error_out) *error_out = "whisper_full failed";
        return false;
    }

    // Optional word-level path: token timestamps -> grouped
    // whitespace-delimited words. All three symbols are optional —
    // an older libwhisper just leaves seg.words empty.
    const bool have_token_api =
        api.n_tokens != nullptr && api.token_text != nullptr &&
        api.token_tstamps != nullptr;
    const whisper_token eot =
        api.token_eot != nullptr ? api.token_eot(ctx) : -1;

    const int n = api.n_segments(ctx);
    for (int i = 0; i < n; ++i) {
        TranscriptSegment seg;
        // whisper timestamps are 10 ms units.
        seg.t0 = api.seg_t0(ctx, i) / 100.0;
        seg.t1 = api.seg_t1(ctx, i) / 100.0;
        if (const char* text = api.seg_text(ctx, i); text != nullptr) {
            seg.text = Trim(text);
        }
        if (have_token_api) {
            const int nt = api.n_tokens(ctx, i);
            for (int j = 0; j < nt; ++j) {
                const whisper_token_data td =
                    api.token_tstamps(ctx, i, j);
                // Timestamp/control tokens (id >= eot) carry no text —
                // skip them when the eot boundary resolved.
                if (eot >= 0 && td.id >= eot) continue;
                const char* raw_ptr = api.token_text(ctx, i, j);
                if (raw_ptr == nullptr) continue;
                const std::string raw(raw_ptr);
                const std::string word = Trim(raw);
                if (word.empty()) continue;
                // whisper marks word starts with a leading space on
                // Latin text; tokens without it continue the current
                // word (sub-word pieces — "trans", "cription").
                const bool boundary =
                    seg.words.empty() || raw.front() == ' ' ||
                    raw.front() == '\t';
                const double wt0 = td.t0 / 100.0;
                const double wt1 =
                    std::max(wt0, td.t1 / 100.0);
                if (boundary) {
                    TranscriptWordSpan w;
                    w.t0 = wt0;
                    w.t1 = wt1;
                    w.text = word;
                    seg.words.push_back(std::move(w));
                } else {
                    TranscriptWordSpan& w = seg.words.back();
                    w.text += word;
                    w.t0 = std::min(w.t0, wt0);
                    w.t1 = std::max(w.t1, wt1);
                }
            }
            // Sanity: timestamps must be finite and ordered inside
            // the segment — a pathological token stream is worse than
            // the proportional fallback, so drop it.
            for (const auto& w : seg.words) {
                if (!std::isfinite(w.t0) || !std::isfinite(w.t1) ||
                    w.t1 < w.t0) {
                    seg.words.clear();
                    break;
                }
            }
        }
        out->push_back(std::move(seg));
    }
    api.free_ctx(ctx);
    return true;
#endif
}

double TranscribeMaxSeconds() {
    // 60-minute default (was a hard-coded 10): roughly a feature
    // film, ~220 MiB of 16 kHz mono float PCM — a bound on decode
    // time and worker residency, not a quality limit.
    constexpr double kDefaultMaxSeconds = 60.0 * 60.0;
    const char* env = std::getenv("XER_TRANSCRIBE_MAX_MINUTES");
    if (env == nullptr || *env == '\0') {
        return kDefaultMaxSeconds;
    }
    char* end = nullptr;
    const double minutes = std::strtod(env, &end);
    if (end == env || !std::isfinite(minutes)) {
        // A typo'd override must not silently un-cap the decode —
        // treat it as unset.
        return kDefaultMaxSeconds;
    }
    // <= 0 is the documented "uncapped" escape hatch.
    return minutes <= 0.0 ? 0.0 : minutes * 60.0;
}

}  // namespace XER::VideoEditor::Ai
