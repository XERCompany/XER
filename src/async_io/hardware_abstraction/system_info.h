#pragma once

#include "cpu_info.h"
#include "gpu_info.h"
#include "memory_monitor.h"
#include "hw_temp_sensor.h"
#include "hw_power_monitor.h"
#include "hw_topology_map.h"
#include "hw_capability_scanner.h"
#include "disk_info.h"
#include "network_info.h"
#include "battery_info.h"

#include <string>
#include <vector>
#include <future>
#include <functional>
#include <mutex>
#include <thread>

namespace AsyncIO::IO::Hardware {

// One coherent snapshot of the entire machine. SystemInfoProvider owns one
// instance of every subsystem provider and produces this snapshot on demand.
struct SystemSnapshot {
    // ---- CPU ----------------------------------------------------------------
    CPUInfo cpu{};
    std::vector<ProcessorCore> cores;
    uint64_t total_memory_mb = 0;
    uint64_t available_memory_mb = 0;
    double cpu_usage_percent = 0.0;
    std::string cpu_description;

    // ---- Topology -----------------------------------------------------------
    int sockets = 0;
    int numa_nodes = 0;
    int physical_cores = 0;
    int logical_cores = 0;

    // ---- Memory -------------------------------------------------------------
    MemoryStats memory{};
    std::vector<MemoryStats> gpu_memory;   // per NVIDIA GPU

    // ---- GPUs ---------------------------------------------------------------
    std::vector<GPUDevice> gpus;

    // ---- Temperature --------------------------------------------------------
    std::vector<TemperatureReading> temperatures;
    double max_temperature_c = 0.0;
    double cpu_temperature_c = 0.0;

    // ---- Power --------------------------------------------------------------
    PowerStats power{};

    // ---- Disks --------------------------------------------------------------
    std::vector<DiskInfo> disks;
    DiskIoStats disk_io{};

    // ---- Network ------------------------------------------------------------
    std::vector<NetworkAdapter> network;
    uint64_t network_bytes_received = 0;
    uint64_t network_bytes_sent = 0;

    // ---- Battery ------------------------------------------------------------
    std::vector<BatteryInfo> batteries;
    bool on_ac_power = true;

    // ---- Capabilities -------------------------------------------------------
    HwCapabilities capabilities{};

    // ---- Snapshot metadata --------------------------------------------------
    int64_t timestamp_ms = 0;
};

// Top-level entry point for "information about all devices".
//
// Owns one instance of each hardware provider and refreshes them together.
// Call Refresh() to update the snapshot, or use the providers directly for
// streaming/background telemetry.
//
// Thread-safety contract:
//   - Refresh() / RefreshAsync() / GetLastSnapshot() / ToJson() are safe to
//     call from any thread. Refresh() itself blocks for ~50ms (CPU usage
//     sampling); prefer RefreshAsync() from latency-sensitive threads.
//   - RefreshAsync() serializes refreshes: a new call joins any in-flight
//     refresh before starting its own. The providers are accessed by at
//     most one thread at a time.
//   - The per-subsystem accessors (Cpu(), Gpu(), ...) return references to
//     non-thread-safe providers. If you use them directly from multiple
//     threads you must synchronize externally; the providers are NOT
//     reentrant. Mixing direct provider calls with Refresh()/RefreshAsync()
//     from another thread is a data race unless you synchronize.
class SystemInfoProvider {
public:
    SystemInfoProvider();
    ~SystemInfoProvider();

    // Initialize all subsystem providers (idempotent).
    bool Initialize();
    bool IsInitialized() const { return initialized_; }

    // Build a fresh snapshot. Cheap to call, but each call re-queries the OS
    // and may block briefly (CPU usage sampling is ~50ms).
    SystemSnapshot Refresh();

    // Asynchronous variant: runs Refresh() on the internal worker thread and
    // returns a future. Safe to call from any thread; the underlying provider
    // objects are accessed only by one worker at a time. If a refresh is
    // already in flight, the returned future resolves to the in-flight result.
    std::future<SystemSnapshot> RefreshAsync();

    // Return the last snapshot produced by Refresh() (cheap).
    const SystemSnapshot& GetLastSnapshot() const { return last_; }

    // JSON serialization via jsoncpp. Empty string on failure.
    std::string ToJson(const SystemSnapshot& snap, bool pretty = true) const;
    std::string ToJson(bool pretty = true) const { return ToJson(last_, pretty); }

    // Subsystem access (for callers that want streaming telemetry).
    CPUInfoProvider&       Cpu()       { return cpu_; }
    GPUInfoProvider&       Gpu()       { return gpu_; }
    MemoryMonitor&         Memory()    { return memory_; }
    TemperatureSensor&     Temp()      { return temp_; }
    HwPowerMonitor&        Power()     { return power_; }
    HardwareTopologyMap&   Topology()  { return topology_; }
    DiskInfoProvider&      Disks()     { return disks_; }
    NetworkInfoProvider&   Network()   { return network_; }
    BatteryInfoProvider&   Battery()   { return battery_; }

private:
    bool initialized_ = false;
    CPUInfoProvider      cpu_;
    GPUInfoProvider      gpu_;
    MemoryMonitor        memory_;
    TemperatureSensor    temp_;
    HwPowerMonitor       power_;
    HardwareTopologyMap  topology_;
    DiskInfoProvider     disks_;
    NetworkInfoProvider  network_;
    BatteryInfoProvider  battery_;
    SystemSnapshot       last_;

    // Async refresh support: a single background worker serves RefreshAsync
    // calls so providers are never accessed concurrently.
    std::mutex async_mutex_;
    std::thread async_thread_;
    std::promise<SystemSnapshot> async_promise_;
};

}  // namespace AsyncIO::IO::Hardware
