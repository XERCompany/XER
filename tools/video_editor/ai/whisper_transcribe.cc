// Whisper transcription via runtime-probed libwhisper. The build only
// needs <whisper.h> for the struct layout (guarded by __has_include —
// feature compiles out when absent); the DLL itself is LoadLibrary'ed
// at first use so the exe never hard-depends on it.
#include "ai/whisper_transcribe.h"

#include <algorithm>
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

    const int n = api.n_segments(ctx);
    for (int i = 0; i < n; ++i) {
        TranscriptSegment seg;
        // whisper timestamps are 10 ms units.
        seg.t0 = api.seg_t0(ctx, i) / 100.0;
        seg.t1 = api.seg_t1(ctx, i) / 100.0;
        if (const char* text = api.seg_text(ctx, i); text != nullptr) {
            seg.text = Trim(text);
        }
        out->push_back(std::move(seg));
    }
    api.free_ctx(ctx);
    return true;
#endif
}

}  // namespace XER::VideoEditor::Ai
