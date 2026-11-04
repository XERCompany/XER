// Async I/O example — exercises the real src/async_io hardware abstraction.
//
// This builds a SystemInfoProvider, takes one snapshot of every device on the
// machine (CPU, GPUs, memory, temperature, power, disks, network, battery,
// capabilities), and prints a compact human-readable summary plus optional
// pretty-printed JSON.

#include "async_io/hardware_abstraction/system_info.h"

#include <iomanip>
#include <iostream>

using AsyncIO::IO::Hardware::SystemInfoProvider;

namespace {

void PrintSummary(const AsyncIO::IO::Hardware::SystemSnapshot& s) {
    std::cout << "=== XER async_io hardware snapshot ===\n";
    std::cout << "CPU       : " << s.cpu_description
              << "  usage=" << std::fixed << std::setprecision(1)
              << s.cpu_usage_percent << "%\n";
    std::cout << "Topology  : " << s.sockets << " socket(s), "
              << s.numa_nodes << " NUMA node(s), "
              << s.physical_cores << "P / " << s.logical_cores << "L cores\n";
    std::cout << "Cache     : L1=" << s.cpu.l1_cache_kb << "KB  L2="
              << s.cpu.l2_cache_kb << "KB  L3=" << s.cpu.l3_cache_kb << "KB\n";
    std::cout << "SIMD      : SSE=" << s.cpu.supports_sse
              << "  AVX=" << s.cpu.supports_avx
              << "  AVX2=" << s.cpu.supports_avx2
              << "  AVX-512=" << s.cpu.supports_avx512 << "\n";
    std::cout << "Memory    : " << s.memory.used_mb << "/" << s.memory.total_mb
              << " MB  (" << std::setprecision(1) << (s.memory.usage_percent * 100.0)
              << "%)  peak=" << s.memory.peak_usage_mb << " MB\n";

    if (!s.gpus.empty()) {
        std::cout << "GPUs (" << s.gpus.size() << "):\n";
        for (const auto& g : s.gpus) {
            std::cout << "  [" << g.device_id << "] " << g.name
                      << "  " << g.backend
                      << "  VRAM=" << (g.total_memory_mb / 1024) << "GB";
            if (g.temp_supported)
                std::cout << "  T=" << std::setprecision(1) << g.temp_celsius << "C";
            if (g.power_supported)
                std::cout << "  P=" << g.power_watts << "W";
            if (g.util_supported)
                std::cout << "  util=" << g.gpu_util_percent << "%";
            std::cout << "\n";
        }
    }

    if (!s.temperatures.empty()) {
        std::cout << "Temperature:\n";
        for (const auto& t : s.temperatures) {
            std::cout << "  " << std::left << std::setw(20) << t.sensor_name
                      << "[" << std::setw(8) << t.source << "] ";
            if (t.is_supported)
                std::cout << std::setprecision(1) << t.temperature_celsius << "C";
            else
                std::cout << "(unsupported)";
            if (t.is_critical) std::cout << "  ** CRITICAL **";
            std::cout << "\n";
        }
    }

    std::cout << "Power     : CPU=" << std::setprecision(2)
              << (s.power.cpu_power_supported ? s.power.cpu_power_watts : 0.0)
              << "W" << (s.power.cpu_power_supported ? "" : " (unsupported)")
              << "  GPU=" << (s.power.gpu_power_supported ? s.power.gpu_power_watts : 0.0)
              << "W\n";

    if (!s.disks.empty()) {
        std::cout << "Disks (" << s.disks.size() << "):\n";
        for (const auto& d : s.disks) {
            std::cout << "  " << std::left << std::setw(24) << d.device
                      << " " << std::setw(8) << d.filesystem << " "
                      << (d.used_bytes >> 20) << "/" << (d.total_bytes >> 20)
                      << " MB";
            if (d.is_network)   std::cout << " (network)";
            if (d.is_removable) std::cout << " (removable)";
            if (d.is_read_only) std::cout << " (ro)";
            std::cout << "\n";
        }
    }

    if (!s.network.empty()) {
        std::cout << "Network (" << s.network.size() << "):\n";
        for (const auto& a : s.network) {
            std::cout << "  " << std::left << std::setw(24) << a.name;
            if (!a.ipv4_address.empty()) std::cout << " " << a.ipv4_address;
            if (!a.mac_address.empty()) std::cout << "  " << a.mac_address;
            std::cout << (a.is_up ? "  UP" : "  down");
            if (a.is_wireless)  std::cout << " (wifi)";
            if (a.is_loopback)  std::cout << " (lo)";
            if (a.is_virtual)   std::cout << " (virtual)";
            std::cout << "\n";
        }
    }

    if (!s.batteries.empty()) {
        std::cout << "Battery:\n";
        for (const auto& b : s.batteries) {
            std::cout << "  " << b.name;
            if (b.present)
                std::cout << " " << std::setprecision(1) << b.percent << "% "
                          << "(" << b.state_name << ")"
                          << "  AC=" << (b.on_ac_power ? "yes" : "no");
            else
                std::cout << " (not present)";
            if (b.health_fraction > 0 && b.health_fraction < 1.0)
                std::cout << "  health=" << std::setprecision(0)
                          << (b.health_fraction * 100.0) << "%";
            std::cout << "\n";
        }
    }

    std::cout << "Capabilities: CUDA=" << (s.capabilities.has_cuda ? "yes" : "no")
              << " (" << s.capabilities.cuda_device_count << " device(s))\n";
}

}  // namespace

int main(int argc, char** argv) {
    SystemInfoProvider sys;
    if (!sys.Initialize()) {
        std::cerr << "Failed to initialize SystemInfoProvider\n";
        return 1;
    }

    const auto snap = sys.Refresh();
    PrintSummary(snap);

    if (argc > 1 && std::string(argv[1]) == "--json") {
        std::cout << "\n=== JSON ===\n" << sys.ToJson(snap, /*pretty=*/true) << "\n";
    }

    return 0;
}
