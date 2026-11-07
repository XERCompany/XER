#include "hw_power_monitor.h"

#include "nvml_loader.h"

#include <fstream>
#include <string>
#include <chrono>

#if defined(__linux__)
    #include <thread>
#endif

namespace XER::AsyncIO::IO::Hardware {

namespace {

#if defined(__linux__)
double ReadSysfsDouble(const std::string& path, double scale = 1.0) {
    std::ifstream f(path);
    if (!f.is_open()) return 0.0;
    double value = 0.0;
    f >> value;
    return value * scale;
}

double ReadCpuTempCelsius() {
    return ReadSysfsDouble("/sys/class/thermal/thermal_zone0/temp", 1e-3);
}

// RAPL package energy in microjoules, or -1 if unavailable.
double ReadRaplEnergyUj() {
    std::ifstream f("/sys/class/powercap/intel-rapl:0/energy_uj");
    if (!f.is_open()) return -1.0;
    double value = 0.0;
    f >> value;
    return value;
}

int ReadThrottleCount() {
    return static_cast<int>(ReadSysfsDouble(
        "/sys/devices/system/cpu/cpu0/thermal_throttle/core_throttle_count"));
}
#endif  // __linux__

void SumGpuPower(double& watts, double& limit, bool& supported) {
    watts = 0.0; limit = 0.0; supported = false;
    auto& nvml = NvmlLibrary::Instance();
    if (!nvml.IsAvailable()) return;
    const auto snaps = nvml.GetSnapshots();
    bool any = false;
    for (const auto& s : snaps) {
        if (s.power_supported)        { watts += s.power_watts;       any = true; }
        if (s.power_limit_supported) { limit += s.power_limit_watts; }
    }
    supported = any;
}

}  // namespace

HwPowerMonitor::HwPowerMonitor()
    : running_(false) {
    last_stats_ = PowerStats{};
}

HwPowerMonitor::~HwPowerMonitor() {
    Stop();
}

void HwPowerMonitor::Start(int interval_ms) {
    if (running_.exchange(true)) return;  // Already running
    if (monitor_thread_.joinable()) monitor_thread_.join();
    monitor_thread_ = std::thread(&HwPowerMonitor::MonitorLoop, this, interval_ms);
}

void HwPowerMonitor::Stop() {
    if (!running_.exchange(false)) return;
    if (monitor_thread_.joinable()) monitor_thread_.join();
}

void HwPowerMonitor::MonitorLoop(int interval_ms) {
#if defined(__linux__)
    double prev_energy_uj = ReadRaplEnergyUj();
#else
    // No CPU package power API on Windows without a kernel driver; the
    // prev_energy_uj value is unused on this platform.
    double prev_energy_uj = -1.0;
    (void)prev_energy_uj;
#endif

    while (running_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));

        PowerStats s{};

#if defined(__linux__)
        s.cpu_temp_celsius = ReadCpuTempCelsius();
        s.throttle_count   = ReadThrottleCount();

        // CPU package power from RAPL energy delta. energy_uj wraps at
        // max_energy_range_uj; a negative delta (wrap) is treated as a single
        // missed sample and skipped.
        const double cur_energy_uj = ReadRaplEnergyUj();
        if (prev_energy_uj >= 0.0 && cur_energy_uj >= 0.0) {
            const double delta_uj = cur_energy_uj - prev_energy_uj;
            if (delta_uj >= 0.0) {
                const double elapsed_s = static_cast<double>(interval_ms) * 1e-3;
                s.cpu_power_watts = delta_uj * 1e-6 / elapsed_s;
                s.cpu_power_supported = true;
            }
        }
        prev_energy_uj = cur_energy_uj;
#else
        // Windows: no generic user-space CPU package power API without a
        // kernel/driver. RAPL MSRs require ring-0 access. Mark unsupported.
        s.cpu_power_supported = false;
#endif

        SumGpuPower(s.gpu_power_watts, s.gpu_power_limit_watts, s.gpu_power_supported);

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            last_stats_ = s;
        }
    }
}

PowerStats HwPowerMonitor::GetStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return last_stats_;
}

}  // namespace XER::AsyncIO::IO::Hardware
