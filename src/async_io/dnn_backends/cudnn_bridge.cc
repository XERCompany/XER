#include "cudnn_bridge.h"

#if defined(XER_ASYNC_IO_HAS_CUDNN)
    // Linked cuDNN: use the header directly.
    #include <cudnn.h>
#else
    // Dynamic load fallback: no compile-time cuDNN dependency. We declare
    // the minimal ABI types we need (cudnnHandle_t, cudnnStatus_t, and the
    // create/destroy/error-string function pointers) and resolve them at
    // runtime via LoadLibrary/dlopen.
    #if defined(_WIN32)
        #include <windows.h>
    #else
        #include <dlfcn.h>
    #endif

    using cudnnHandle_t_alias = void*;
    using cudnnStatus_t_alias = int;
    constexpr cudnnStatus_t_alias kCudnnStatusSuccess = 0;

    using PfnCudnnCreate  = cudnnStatus_t_alias (*)(cudnnHandle_t_alias*);
    using PfnCudnnDestroy = cudnnStatus_t_alias (*)(cudnnHandle_t_alias);
    using PfnCudnnError   = const char* (*)(cudnnStatus_t_alias);
#endif

namespace AsyncIO::IO::DNNBackends {

#if !defined(XER_ASYNC_IO_HAS_CUDNN)
namespace {

struct DynamicCudnn {
    void* lib = nullptr;
    PfnCudnnCreate  pfnCreate  = nullptr;
    PfnCudnnDestroy pfnDestroy = nullptr;
    PfnCudnnError   pfnError   = nullptr;

    bool Load() {
        if (lib) return true;
#if defined(_WIN32)
        lib = static_cast<void*>(LoadLibraryA("cudnn64_*.dll"));
        if (!lib) lib = static_cast<void*>(LoadLibraryA("cudnn.dll"));
#else
        lib = dlopen("libcudnn.so", RTLD_LAZY | RTLD_LOCAL);
        if (!lib) lib = dlopen("libcudnn.so.8", RTLD_LAZY | RTLD_LOCAL);
        if (!lib) lib = dlopen("libcudnn.so.7", RTLD_LAZY | RTLD_LOCAL);
#endif
        if (!lib) return false;
#if defined(_WIN32)
        auto sym = [this](const char* n) -> void* {
            return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(lib), n));
        };
#else
        auto sym = [this](const char* n) -> void* { return dlsym(lib, n); };
#endif
        pfnCreate  = reinterpret_cast<PfnCudnnCreate>(sym("cudnnCreate"));
        pfnDestroy = reinterpret_cast<PfnCudnnDestroy>(sym("cudnnDestroy"));
        pfnError   = reinterpret_cast<PfnCudnnError>(sym("cudnnGetErrorString"));
        return pfnCreate && pfnDestroy;
    }

    ~DynamicCudnn() {
        if (lib) {
#if defined(_WIN32)
            FreeLibrary(static_cast<HMODULE>(lib));
#else
            dlclose(lib);
#endif
        }
    }
};

DynamicCudnn& DynCudnn() {
    static DynamicCudnn d;
    return d;
}

}  // namespace
#endif  // !XER_ASYNC_IO_HAS_CUDNN

CuDnnBridge::CuDnnBridge() : handle_(nullptr), ready_(false) {}

CuDnnBridge::~CuDnnBridge() {
    Shutdown();
}

bool CuDnnBridge::Initialize() {
    if (ready_) return true;

#if defined(XER_ASYNC_IO_HAS_CUDNN)
    cudnnHandle_t handle = nullptr;
    const cudnnStatus_t status = cudnnCreate(&handle);
    if (status != CUDNN_STATUS_SUCCESS) {
        lastError_ = cudnnGetErrorString(status);
        return false;
    }
    handle_ = handle;
#else
    if (!DynCudnn().Load()) {
        lastError_ = "cuDNN library not found (dynamic load failed)";
        return false;
    }
    cudnnHandle_t_alias handle = nullptr;
    const cudnnStatus_t_alias status = DynCudnn().pfnCreate(&handle);
    if (status != kCudnnStatusSuccess) {
        lastError_ = DynCudnn().pfnError ? DynCudnn().pfnError(status)
                                          : "cudnnCreate failed (no error string)";
        return false;
    }
    handle_ = handle;
#endif

    ready_ = true;
    lastError_.clear();
    return true;
}

void CuDnnBridge::Shutdown() {
    if (handle_ != nullptr) {
#if defined(XER_ASYNC_IO_HAS_CUDNN)
        cudnnDestroy(static_cast<cudnnHandle_t>(handle_));
#else
        if (DynCudnn().pfnDestroy) {
            DynCudnn().pfnDestroy(handle_);
        }
#endif
    }
    handle_ = nullptr;
    ready_ = false;
}

bool CuDnnBridge::IsReady() const { return ready_; }
std::string CuDnnBridge::LastError() const { return lastError_; }
void *CuDnnBridge::Handle() const { return handle_; }

} // namespace AsyncIO::IO::DNNBackends
