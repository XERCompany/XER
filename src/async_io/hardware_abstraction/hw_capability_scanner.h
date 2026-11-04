#pragma once

#include <cstddef>
#include <cstdint>

namespace AsyncIO::IO::Hardware {

struct HwCapabilities {
    bool    has_sse4   = false;
    bool    has_avx2   = false;
    bool    has_avx512 = false;
    int     cpu_core_count = 0;
    size_t  total_ram_bytes = 0;
    bool    has_cuda        = false;
    int     cuda_device_count = 0;
};

class HwCapabilityScanner {
public:
    static HwCapabilityScanner& Instance();

    HwCapabilities Scan();

    HwCapabilityScanner(const HwCapabilityScanner&) = delete;
    HwCapabilityScanner& operator=(const HwCapabilityScanner&) = delete;

private:
    HwCapabilityScanner() = default;
    ~HwCapabilityScanner() = default;
};

}  // namespace AsyncIO::IO::Hardware
