#include "system_info.h"

#include "nvml_loader.h"

#include <chrono>

#include <json/json.h>

namespace AsyncIO::IO::Hardware {

namespace {

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void AppendCpu(Json::Value& root, const SystemSnapshot& s) {
    Json::Value cpu(Json::objectValue);
    cpu["vendor"]          = s.cpu.vendor;
    cpu["model"]           = s.cpu.model;
    cpu["physical_cores"]  = s.cpu.physical_cores;
    cpu["logical_cores"]   = s.cpu.logical_cores;
    cpu["l1_cache_kb"]     = static_cast<Json::UInt64>(s.cpu.l1_cache_kb);
    cpu["l2_cache_kb"]     = static_cast<Json::UInt64>(s.cpu.l2_cache_kb);
    cpu["l3_cache_kb"]     = static_cast<Json::UInt64>(s.cpu.l3_cache_kb);
    cpu["supports_sse"]    = s.cpu.supports_sse;
    cpu["supports_avx"]    = s.cpu.supports_avx;
    cpu["supports_avx2"]   = s.cpu.supports_avx2;
    cpu["supports_avx512"] = s.cpu.supports_avx512;
    cpu["description"]     = s.cpu_description;
    cpu["usage_percent"]   = s.cpu_usage_percent;

    Json::Value cores(Json::arrayValue);
    for (const auto& c : s.cores) {
        Json::Value j(Json::objectValue);
        j["core_id"]               = c.core_id;
        j["logical_id"]            = c.logical_id;
        j["frequency_mhz"]         = static_cast<Json::UInt64>(c.frequency_mhz);
        j["current_frequency_mhz"] = static_cast<Json::UInt64>(c.current_frequency_mhz);
        j["is_active"]             = c.is_active;
        cores.append(std::move(j));
    }
    cpu["cores"] = std::move(cores);
    root["cpu"]  = std::move(cpu);

    Json::Value topo(Json::objectValue);
    topo["sockets"]       = s.sockets;
    topo["numa_nodes"]    = s.numa_nodes;
    topo["physical_cores"]= s.physical_cores;
    topo["logical_cores"] = s.logical_cores;
    root["topology"]      = std::move(topo);
}

void AppendMemory(Json::Value& root, const SystemSnapshot& s) {
    Json::Value mem(Json::objectValue);
    mem["total_mb"]            = static_cast<Json::UInt64>(s.memory.total_mb);
    mem["used_mb"]             = static_cast<Json::UInt64>(s.memory.used_mb);
    mem["free_mb"]             = static_cast<Json::UInt64>(s.memory.free_mb);
    mem["usage_percent"]       = s.memory.usage_percent;
    mem["peak_usage_mb"]       = static_cast<Json::UInt64>(s.memory.peak_usage_mb);
    mem["total_system_mb"]     = static_cast<Json::UInt64>(s.total_memory_mb);
    mem["available_system_mb"] = static_cast<Json::UInt64>(s.available_memory_mb);
    root["memory"]             = std::move(mem);

    Json::Value gpus_mem(Json::arrayValue);
    for (size_t i = 0; i < s.gpu_memory.size() && i < s.gpus.size(); ++i) {
        Json::Value j(Json::objectValue);
        j["device_id"] = s.gpus[i].device_id;
        j["name"]      = s.gpus[i].name;
        j["total_mb"]  = static_cast<Json::UInt64>(s.gpu_memory[i].total_mb);
        j["free_mb"]   = static_cast<Json::UInt64>(s.gpu_memory[i].free_mb);
        j["used_mb"]   = static_cast<Json::UInt64>(s.gpu_memory[i].used_mb);
        j["usage_percent"] = s.gpu_memory[i].usage_percent;
        gpus_mem.append(std::move(j));
    }
    root["gpu_memory"] = std::move(gpus_mem);
}

void AppendGpus(Json::Value& root, const SystemSnapshot& s) {
    Json::Value gpus(Json::arrayValue);
    for (const auto& d : s.gpus) {
        Json::Value j(Json::objectValue);
        j["device_id"]          = d.device_id;
        j["name"]               = d.name;
        j["compute_capability"] = d.compute_capability;
        j["total_memory_mb"]    = static_cast<Json::UInt64>(d.total_memory_mb);
        j["free_memory_mb"]     = static_cast<Json::UInt64>(d.free_memory_mb);
        j["is_available"]       = d.is_available;
        j["vendor_id"]          = d.vendor_id;
        j["pci_device_id"]      = d.pci_device_id;
        j["driver_version"]     = d.driver_version;
        j["backend"]            = d.backend;
        j["temp_celsius"]       = d.temp_celsius;
        j["temp_supported"]     = d.temp_supported;
        j["power_watts"]        = d.power_watts;
        j["power_supported"]    = d.power_supported;
        j["gpu_util_percent"]  = d.gpu_util_percent;
        j["mem_util_percent"]  = d.mem_util_percent;
        j["util_supported"]    = d.util_supported;
        j["clock_graphics_mhz"] = static_cast<Json::UInt>(d.clock_graphics_mhz);
        j["clock_mem_mhz"]      = static_cast<Json::UInt>(d.clock_mem_mhz);
        gpus.append(std::move(j));
    }
    root["gpus"] = std::move(gpus);
}

void AppendTemps(Json::Value& root, const SystemSnapshot& s) {
    Json::Value temps(Json::arrayValue);
    for (const auto& r : s.temperatures) {
        Json::Value j(Json::objectValue);
        j["sensor_name"]          = r.sensor_name;
        j["source"]               = r.source;
        j["temperature_celsius"]  = r.temperature_celsius;
        j["temperature_critical"] = r.temperature_critical;
        j["temperature_slowdown"] = r.temperature_slowdown;
        j["is_critical"]          = r.is_critical;
        j["is_supported"]         = r.is_supported;
        j["timestamp_ms"]         = static_cast<Json::Int64>(r.timestamp_ms);
        temps.append(std::move(j));
    }
    root["temperatures"]        = std::move(temps);
    root["max_temperature_c"]   = s.max_temperature_c;
    root["cpu_temperature_c"]   = s.cpu_temperature_c;
}

void AppendPower(Json::Value& root, const SystemSnapshot& s) {
    Json::Value p(Json::objectValue);
    p["cpu_temp_celsius"]        = s.power.cpu_temp_celsius;
    p["cpu_power_watts"]         = s.power.cpu_power_watts;
    p["throttle_count"]          = s.power.throttle_count;
    p["gpu_power_watts"]         = s.power.gpu_power_watts;
    p["gpu_power_limit_watts"]   = s.power.gpu_power_limit_watts;
    p["cpu_power_supported"]     = s.power.cpu_power_supported;
    p["gpu_power_supported"]     = s.power.gpu_power_supported;
    root["power"]                = std::move(p);
}

void AppendDisks(Json::Value& root, const SystemSnapshot& s) {
    Json::Value disks(Json::arrayValue);
    for (const auto& d : s.disks) {
        Json::Value j(Json::objectValue);
        j["device"]       = d.device;
        j["mount_point"]  = d.mount_point;
        j["filesystem"]   = d.filesystem;
        j["label"]        = d.label;
        j["total_bytes"]  = static_cast<Json::UInt64>(d.total_bytes);
        j["free_bytes"]   = static_cast<Json::UInt64>(d.free_bytes);
        j["used_bytes"]   = static_cast<Json::UInt64>(d.used_bytes);
        j["is_removable"] = d.is_removable;
        j["is_read_only"] = d.is_read_only;
        j["is_network"]   = d.is_network;
        disks.append(std::move(j));
    }
    root["disks"] = std::move(disks);

    Json::Value io(Json::objectValue);
    io["reads"]        = static_cast<Json::UInt64>(s.disk_io.reads);
    io["writes"]       = static_cast<Json::UInt64>(s.disk_io.writes);
    io["read_bytes"]   = static_cast<Json::UInt64>(s.disk_io.read_bytes);
    io["write_bytes"]  = static_cast<Json::UInt64>(s.disk_io.write_bytes);
    root["disk_io"]    = std::move(io);
}

void AppendNetwork(Json::Value& root, const SystemSnapshot& s) {
    Json::Value nets(Json::arrayValue);
    for (const auto& a : s.network) {
        Json::Value j(Json::objectValue);
        j["name"]            = a.name;
        j["friendly_name"]   = a.friendly_name;
        j["description"]     = a.description;
        j["mac_address"]     = a.mac_address;
        j["ipv4_address"]    = a.ipv4_address;
        j["is_up"]           = a.is_up;
        j["is_loopback"]     = a.is_loopback;
        j["is_wireless"]     = a.is_wireless;
        j["is_virtual"]      = a.is_virtual;
        j["bytes_received"]  = static_cast<Json::UInt64>(a.bytes_received);
        j["bytes_sent"]      = static_cast<Json::UInt64>(a.bytes_sent);
        j["packets_received"]= static_cast<Json::UInt64>(a.packets_received);
        j["packets_sent"]    = static_cast<Json::UInt64>(a.packets_sent);
        j["link_speed_bps"]  = static_cast<Json::UInt64>(a.link_speed_bps);
        nets.append(std::move(j));
    }
    root["network"]              = std::move(nets);
    root["network_bytes_received"]= static_cast<Json::UInt64>(s.network_bytes_received);
    root["network_bytes_sent"]    = static_cast<Json::UInt64>(s.network_bytes_sent);
}

void AppendBattery(Json::Value& root, const SystemSnapshot& s) {
    Json::Value bats(Json::arrayValue);
    for (const auto& b : s.batteries) {
        Json::Value j(Json::objectValue);
        j["id"]                            = b.id;
        j["name"]                          = b.name;
        j["present"]                       = b.present;
        j["percent"]                       = b.percent;
        j["state"]                         = b.state_name;
        j["estimated_seconds_remaining"]   = static_cast<Json::Int64>(b.estimated_seconds_remaining);
        j["voltage_volts"]                 = b.voltage_volts;
        j["design_capacity_wh"]            = b.design_capacity_wh;
        j["full_charged_capacity_wh"]      = b.full_charged_capacity_wh;
        j["current_capacity_wh"]           = b.current_capacity_wh;
        j["health_fraction"]               = b.health_fraction;
        j["on_ac_power"]                   = b.on_ac_power;
        bats.append(std::move(j));
    }
    root["batteries"] = std::move(bats);
    root["on_ac_power"] = s.on_ac_power;
}

void AppendCaps(Json::Value& root, const SystemSnapshot& s) {
    Json::Value c(Json::objectValue);
    c["has_sse4"]          = s.capabilities.has_sse4;
    c["has_avx2"]         = s.capabilities.has_avx2;
    c["has_avx512"]       = s.capabilities.has_avx512;
    c["cpu_core_count"]   = s.capabilities.cpu_core_count;
    c["total_ram_bytes"]  = static_cast<Json::UInt64>(s.capabilities.total_ram_bytes);
    c["has_cuda"]         = s.capabilities.has_cuda;
    c["cuda_device_count"]= s.capabilities.cuda_device_count;
    root["capabilities"]  = std::move(c);
}

}  // namespace

SystemInfoProvider::SystemInfoProvider() = default;
SystemInfoProvider::~SystemInfoProvider() {
    if (async_thread_.joinable()) async_thread_.join();
}

bool SystemInfoProvider::Initialize() {
    if (initialized_) return true;
    cpu_.Initialize();
    gpu_.Initialize();
    temp_.Initialize();
    topology_.Initialize();
    disks_.Initialize();
    network_.Initialize();
    battery_.Initialize();
    memory_.StartMonitoring();
    power_.Start(1000);
    initialized_ = true;
    return true;
}

SystemSnapshot SystemInfoProvider::Refresh() {
    if (!initialized_) Initialize();

    SystemSnapshot s;
    s.timestamp_ms = NowMs();

    // CPU + topology
    s.cpu             = cpu_.GetInfo();
    cpu_.RefreshFrequencies();  // update per-core current frequencies
    s.cores           = cpu_.GetCores();
    s.total_memory_mb = cpu_.GetTotalMemoryMB();
    s.available_memory_mb = cpu_.GetAvailableMemoryMB();
    s.cpu_usage_percent   = cpu_.GetCPUUsagePercent();
    s.cpu_description     = cpu_.GetCPUDescription();

    s.sockets        = topology_.GetSocketCount();
    s.numa_nodes     = topology_.GetNUMANodeCount();
    s.physical_cores = topology_.GetCoreCount();
    s.logical_cores  = topology_.GetThreadCount();

    // Memory
    s.memory = memory_.GetMemoryStats();

    // GPUs (with NVML telemetry)
    s.gpus = gpu_.GetDevices();
    for (const auto& g : s.gpus) {
        s.gpu_memory.push_back(memory_.GetGPUMemoryStats(g.device_id));
    }

    // Temperature
    temp_.Refresh();
    s.temperatures        = temp_.GetAllReadings();
    s.max_temperature_c  = temp_.GetMaxTemperature();
    s.cpu_temperature_c  = temp_.GetCPUTemperature();

    // Power (latest background sample). The monitor thread may not have
    // produced its first sample yet, so fall back to a synchronous NVML
    // query for GPU power when the background value is zero.
    s.power = power_.GetStats();
    if (!s.power.gpu_power_supported) {
        auto& nvml = NvmlLibrary::Instance();
        if (nvml.IsAvailable()) {
            double watts = 0.0, limit = 0.0;
            const auto snaps = nvml.GetSnapshots();
            bool any = false;
            for (const auto& sn : snaps) {
                if (sn.power_supported)        { watts += sn.power_watts;       any = true; }
                if (sn.power_limit_supported) { limit += sn.power_limit_watts; }
            }
            if (any) {
                s.power.gpu_power_watts        = watts;
                s.power.gpu_power_limit_watts = limit;
                s.power.gpu_power_supported    = true;
            }
        }
    }

    // Disks
    s.disks   = disks_.GetDisks();
    s.disk_io = disks_.GetIoStats();

    // Network
    s.network                = network_.GetAdapters();
    s.network_bytes_received = network_.GetTotalBytesReceived();
    s.network_bytes_sent     = network_.GetTotalBytesSent();

    // Battery
    s.batteries   = battery_.GetBatteries();
    s.on_ac_power = battery_.IsOnACPower();

    // Capabilities (singleton scan)
    s.capabilities = HwCapabilityScanner::Instance().Scan();

    last_ = s;
    return s;
}

std::future<SystemSnapshot> SystemInfoProvider::RefreshAsync() {
    std::lock_guard<std::mutex> lock(async_mutex_);
    // If a refresh is already in flight, join it first so we serialize
    // provider access (providers are not guaranteed reentrant).
    if (async_thread_.joinable()) async_thread_.join();

    // Reuse a fresh promise each call. std::promise is move-only and
    // single-shot; the worker fulfills it exactly once.
    async_promise_ = std::promise<SystemSnapshot>();
    auto fut = async_promise_.get_future();
    async_thread_ = std::thread([this]() {
        SystemSnapshot snap = this->Refresh();
        this->async_promise_.set_value(std::move(snap));
    });
    return fut;
}

std::string SystemInfoProvider::ToJson(const SystemSnapshot& s, bool pretty) const {
    Json::Value root(Json::objectValue);
    root["timestamp_ms"] = static_cast<Json::Int64>(s.timestamp_ms);

    AppendCpu(root, s);
    AppendMemory(root, s);
    AppendGpus(root, s);
    AppendTemps(root, s);
    AppendPower(root, s);
    AppendDisks(root, s);
    AppendNetwork(root, s);
    AppendBattery(root, s);
    AppendCaps(root, s);

    if (pretty) {
        Json::StreamWriterBuilder w;
        w["indentation"] = "  ";
        return Json::writeString(w, root);
    }
    return Json::writeString(Json::StreamWriterBuilder(), root);
}

}  // namespace AsyncIO::IO::Hardware
