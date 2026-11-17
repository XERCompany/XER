// Runtime BLAS probe for the Object Lab — see mkl_accel.h for the
// backend order contract. All DLL resolution is guarded by _WIN32;
// other platforms compile to the scalar backend unchanged.
#include "mkl_accel.h"

#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

// Build-time gate for the OpenBLAS backend (per spec: "via cblas.h if
// present at build time"). The header is included for availability
// detection only — symbols are resolved at RUNTIME from libopenblas.dll
// so no import library ever reaches the link line.
#if defined(__has_include)
#  if __has_include(<cblas.h>)
#    define XER_LAB_HAVE_OPENBLAS_HEADER 1
#    include <cblas.h>
#  elif __has_include(<openblas/cblas.h>)
#    define XER_LAB_HAVE_OPENBLAS_HEADER 1
#    include <openblas/cblas.h>
#  endif
#endif

namespace XER::VideoEditor::Lab {
namespace {

using SdotFn = float (*)(int n, const float* x, int incx,
                         const float* y, int incy);

float ScalarSdot(int n, const float* x, int incx, const float* y,
                 int incy) {
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        acc += static_cast<double>(x[i * incx]) *
               static_cast<double>(y[i * incy]);
    }
    return static_cast<float>(acc);
}

struct BackendState {
    AccelBackend backend = AccelBackend::Scalar;
    SdotFn sdot = &ScalarSdot;
    std::string name = "scalar";
};

#ifdef _WIN32

std::string Narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                             -1, nullptr, 0, nullptr,
                                             nullptr);
    std::string out(static_cast<size_t>(needed > 0 ? needed - 1 : 0), '\0');
    if (!out.empty()) {
        ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(),
                              needed, nullptr, nullptr);
    }
    return out;
}

// Loads the first candidate DLL that both exists and exports a working
// cblas_sdot. A 1-element sanity dot product is run immediately: MKL
// defers mkl_core/threading DLL resolution to the first call, so this
// catches half-installed providers before the lab commits to them.
SdotFn TryLoadBlas(const std::vector<std::wstring>& candidates,
                   std::string* loaded_from) {
    for (const std::wstring& path : candidates) {
        // Full paths get LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR so the DLL's
        // own directory is searched for ITS dependencies (libopenblas
        // needs libomp.dll sitting next to it; mkl_rt pulls mkl_core/
        // mkl_def from the same bin dir) without requiring PATH.
        const bool full_path = path.find(L'\\') != std::wstring::npos ||
                               path.find(L'/') != std::wstring::npos;
        HMODULE mod = full_path
                          ? ::LoadLibraryExW(
                                path.c_str(), nullptr,
                                LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR)
                          : ::LoadLibraryW(path.c_str());
        if (mod == nullptr) continue;
        auto fn = reinterpret_cast<SdotFn>(reinterpret_cast<void*>(
            ::GetProcAddress(mod, "cblas_sdot")));
        if (fn == nullptr) {
            ::FreeLibrary(mod);
            continue;
        }
        const float one = 1.0f;
        if (fn(1, &one, 1, &one, 1) != 1.0f) {
            ::FreeLibrary(mod);
            continue;
        }
        if (loaded_from != nullptr) *loaded_from = Narrow(path);
        return fn;  // handle intentionally leaked — lives for process time
    }
    return nullptr;
}

void AppendEnvDir(std::vector<std::wstring>* dirs, const wchar_t* var,
                  const wchar_t* suffix) {
    wchar_t buffer[4096];
    const DWORD n = ::GetEnvironmentVariableW(var, buffer, 4096);
    if (n == 0 || n >= 4096) return;
    std::wstring dir(buffer, n);
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    dirs->push_back(dir + suffix);
}

BackendState ProbeWindows() {
    BackendState state;

    // Debug/diagnostic override: XER_LAB_ACCEL=scalar|openblas|mkl
    // short-circuits the probe order (useful for A/B benchmarking).
    wchar_t env[32] = {};
    const DWORD env_n =
        ::GetEnvironmentVariableW(L"XER_LAB_ACCEL", env, 32);
    const std::wstring forced(env, env_n < 32 ? env_n : 0);
    const bool allow_mkl = forced != L"openblas" && forced != L"scalar";
    const bool allow_openblas =
        forced != L"mkl" && forced != L"scalar";

    // --- 1. Intel oneMKL -------------------------------------------------
    // oneAPI 2025+ ships versioned runtime names (mkl_rt.3.dll); older
    // layouts used mkl_rt.2.dll / mkl_rt.dll. "latest" is a junction the
    // installer maintains alongside the versioned dir.
    std::vector<std::wstring> dirs;
    for (const wchar_t* d : {
             L"C:\\Program Files (x86)\\Intel\\oneAPI\\mkl\\2026.2\\bin\\",
             L"C:\\Program Files (x86)\\Intel\\oneAPI\\mkl\\latest\\bin\\",
             L"C:\\Program Files\\Intel\\oneAPI\\mkl\\2026.2\\bin\\",
             L"C:\\Program Files\\Intel\\oneAPI\\mkl\\latest\\bin\\",
         }) {
        dirs.emplace_back(d);
    }
    AppendEnvDir(&dirs, L"ONEAPI_ROOT", L"mkl\\latest\\bin\\");
    AppendEnvDir(&dirs, L"MKLROOT", L"..\\bin\\");  // MKLROOT=<mkl>/latest

    std::vector<std::wstring> candidates;
    for (const std::wstring& dir : dirs) {
        for (const wchar_t* name :
             {L"mkl_rt.3.dll", L"mkl_rt.2.dll", L"mkl_rt.dll"}) {
            candidates.push_back(dir + name);
        }
    }
    // PATH fallbacks (e.g. oneAPI setvars.bat / conda environments).
    for (const wchar_t* name :
         {L"mkl_rt.3.dll", L"mkl_rt.2.dll", L"mkl_rt.dll"}) {
        candidates.emplace_back(name);
    }
    std::string loaded;
    if (allow_mkl) {
        if (SdotFn fn = TryLoadBlas(candidates, &loaded)) {
            state.backend = AccelBackend::MKL;
            state.sdot = fn;
            state.name = "mkl_rt (" + loaded + ")";
            return state;
        }
    }

    // --- 2. OpenBLAS (only when cblas.h was visible at build time) -------
#ifdef XER_LAB_HAVE_OPENBLAS_HEADER
    candidates.clear();
    for (const wchar_t* d : {
             L"C:\\tools\\msys64\\clang64\\bin\\",
             L"C:\\msys64\\clang64\\bin\\",
             L"C:\\tools\\msys64\\ucrt64\\bin\\",
             L"C:\\msys64\\ucrt64\\bin\\",
         }) {
        candidates.emplace_back(std::wstring(d) + L"libopenblas.dll");
    }
    candidates.emplace_back(L"libopenblas.dll");  // PATH
    loaded.clear();
    if (allow_openblas) {
        if (SdotFn fn = TryLoadBlas(candidates, &loaded)) {
            state.backend = AccelBackend::OpenBLAS;
            state.sdot = fn;
            state.name = "openblas (" + loaded + ")";
            return state;
        }
    }
#endif
    // --- 3. Scalar -------------------------------------------------------
    return state;
}

#endif  // _WIN32

const BackendState& State() {
    static const BackendState state = [] {
#ifdef _WIN32
        return ProbeWindows();
#else
        return BackendState{};
#endif
    }();
    return state;
}

}  // namespace

AccelBackend ActiveBackend() { return State().backend; }

const char* BackendName() { return State().name.c_str(); }

float Sdot(int n, const float* a, const float* b) {
    if (n <= 0) return 0.0f;
    return State().sdot(n, a, 1, b, 1);
}

float PatchSsd(int n, const float* a, const float* b) {
    if (n <= 0) return 0.0f;
    const BackendState& s = State();
    float ssd = 0.0f;
    if (s.backend == AccelBackend::Scalar) {
        // One fused pass — avoids the cancellation a decomposed SSD can
        // hit when the patches are nearly identical.
        for (int i = 0; i < n; ++i) {
            const float d = a[i] - b[i];
            ssd += d * d;
        }
        return ssd < 0.0f ? 0.0f : ssd;
    }
    // <a,a> - 2<a,b> + <b,b>; n is a patch tap count (small), so the
    // float cancellation risk is negligible for u8-derived data.
    const float aa = s.sdot(n, a, 1, a, 1);
    const float ab = s.sdot(n, a, 1, b, 1);
    const float bb = s.sdot(n, b, 1, b, 1);
    ssd = aa - 2.0f * ab + bb;
    return ssd < 0.0f ? 0.0f : ssd;
}

}  // namespace XER::VideoEditor::Lab
