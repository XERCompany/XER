#pragma once

#include <string>

namespace AsyncIO::IO::DNNBackends {

// Bridge to a cuDNN handle. When built with ENABLE_CUDNN=ON, uses the
// linked cuDNN directly. Otherwise, falls back to dynamic loading of
// cudnn.dll (Windows) / libcudnn.so (Linux) at runtime, so the binary
// can still run on machines without cuDNN installed.
class CuDnnBridge {
public:
    CuDnnBridge();
    ~CuDnnBridge();

    CuDnnBridge(const CuDnnBridge &) = delete;
    CuDnnBridge &operator=(const CuDnnBridge &) = delete;

    bool Initialize();
    void Shutdown();

    bool IsReady() const;
    std::string LastError() const;
    void *Handle() const;

private:
    void *handle_;
    bool ready_;
    std::string lastError_;
};

} // namespace AsyncIO::IO::DNNBackends