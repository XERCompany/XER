# Async I/O subsystem

The `src/async_io` directory contains the asynchronous input/output primitives
and the hardware abstraction layer used across the project. It provides the
buffering, scheduling, concurrency, and machine-environment building blocks
used by the runtime, networking layers, native integrations, and the
hardware-aware optimization paths.

## What it provides

- asynchronous file read and write paths
- reusable buffer pools and memory management helpers
- task scheduling and queue-based execution
- synchronization primitives
- hardware capability detection and resource monitoring
- optional backend integrations for accelerated workloads

## Typical components

- `async_file_reader.*` / `async_file_writer.*`
- `async_buffer_pool.*` / `io_buffer_pool.*`
- `async_task_queue.*`
- `io_file_map.*`
- `io_prefetch_manager.*`
- `io_priority_queue.*`
- `hardware_abstraction/`
- `dnn_backends/`

## Design principles

- keep I/O off the main execution path when possible
- reuse byte buffers and scheduling resources
- isolate platform-specific behavior behind abstract interfaces
- allow optional acceleration through CPU/GPU-aware paths
- never hard-depend on vendor SDKs — load them dynamically at runtime

## Hardware abstraction

The `hardware_abstraction` directory covers the full machine environment:

| Provider                | What it reports                                       | Windows backend            | Linux backend              |
|-------------------------|-------------------------------------------------------|----------------------------|----------------------------|
| `CPUInfoProvider`       | vendor/model, cores, caches (L1/L2/L3), SIMD, usage  | `GetLogicalProcessorInformationEx`, `__cpuid`, `GetSystemTimes`, `NtQuerySystemInformation` | `/proc/cpuinfo`, `/sys/.../cache`, cpuid, `/proc/stat` |
| `GPUInfoProvider`       | GPU name, memory, temp, power, utilization, clocks   | NVML + optional CUDA/Vulkan | NVML + optional CUDA/Vulkan |
| `MemoryMonitor`         | system + GPU memory, peak tracking                    | `GlobalMemoryStatusEx`     | `sysconf` + NVML           |
| `TemperatureSensor`     | CPU + GPU temperatures, alerts, background thread    | WMI/ACPI (best-effort) + NVML | `/sys/class/thermal/*` + NVML |
| `HwPowerMonitor`        | CPU + GPU package power, throttle count              | NVML (CPU power unsupported) | RAPL sysfs + NVML          |
| `HardwareTopologyMap`   | sockets, NUMA nodes, cores, cache hierarchy          | `GetLogicalProcessorInformationEx` | `/sys/devices/system/...`  |
| `HwCapabilityScanner`   | SSE4/AVX2/AVX-512, RAM, CUDA presence                 | `__cpuid` + NVML           | cpuid + sysinfo + NVML     |
| `DirectIOManager`       | aligned buffers, O_DIRECT / FILE_FLAG_NO_BUFFERING    | Win32 + alignment utils    | `open(O_DIRECT)`           |
| `DiskInfoProvider`      | mounted disks, sizes, filesystem, I/O stats           | `GetLogicalDriveStrings` + `GetVolumeInformation` | `/proc/mounts` + `/proc/diskstats` |
| `NetworkInfoProvider`   | adapters, MAC, IPv4/IPv6, link speed, byte counters  | `GetAdaptersAddresses`     | `getifaddrs` + `/sys/class/net` |
| `BatteryInfoProvider`   | batteries, percent, state, AC power, wear/health      | `GetSystemPowerStatus`     | `/sys/class/power_supply`  |
| `SystemInfoProvider`    | aggregate snapshot of everything above + JSON export  | all of the above           | all of the above           |

### NVML (NVIDIA) — dynamic loading

`nvml_loader.*` provides a process-wide singleton that loads
`nvml.dll` (Windows) / `libnvidia-ml.so.1` (Linux) on first use, initializes
NVML once, and keeps it loaded. All GPU temperature / power / utilization /
memory queries go through this singleton instead of loading the library per
call. There is **no compile-time dependency** on NVIDIA headers or libraries:
if the driver isn't present, every GPU field is reported with an explicit
`*_supported = false`.

### Windows CPU temperature — honesty note

Reading CPU package temperature on Windows without a kernel driver or admin
rights is unreliable. We use `MSAcpi_ThermalZoneTemperature` via WMI as a
best-effort source and label every reading with `is_supported`. When WMI
returns nothing, we emit a single `CPU` entry with `is_supported = false`
rather than a fake `0 °C` value. For accurate CPU temperatures install
LibreHardwareMonitor (with WMI publishing) or a vendor driver.

### Namespace

All hardware classes live in `AsyncIO::IO::Hardware` (the older
`AsyncIO::IO::HW` alias was unified into it).

## Example usage

```cpp
#include "async_io/hardware_abstraction/system_info.h"

using AsyncIO::IO::Hardware::SystemInfoProvider;

int main() {
    SystemInfoProvider sys;
    sys.Initialize();
    auto snap = sys.Refresh();

    // Pretty-printed JSON of the entire machine.
    std::cout << sys.ToJson(snap, /*pretty=*/true) << "\n";

    // Or access the typed fields directly.
    for (const auto& gpu : snap.gpus) {
        std::cout << gpu.name << "  " << gpu.temp_celsius << " °C  "
                  << gpu.gpu_util_percent << "% util\n";
    }
    std::cout << "Disks: " << snap.disks.size() << "\n";
    std::cout << "On AC: " << (snap.on_ac_power ? "yes" : "no") << "\n";
    return 0;
}
```

Streaming temperature telemetry with a background thread:

```cpp
using AsyncIO::IO::Hardware::TemperatureSensor;
TemperatureSensor t;
t.Initialize();
t.StartMonitoring(2000);  // refresh every 2s
// ... later ...
const auto readings = t.GetAllReadings();
t.StopMonitoring();
```

## Notes

The subsystem is designed to be reusable rather than application-specific. It
serves as a foundation for file access, async execution, and hardware-aware
optimization throughout the project.
