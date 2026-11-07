#include "hw_topology_map.h"

#ifdef _WIN32
    #include <windows.h>
    #ifdef GetObject
        #undef GetObject
    #endif
#elif defined(__linux__)
    #include <unistd.h>
    #include <fstream>
    #include <sstream>
    #include <dirent.h>
#endif

#include <algorithm>

namespace XER::AsyncIO::IO::Hardware {

namespace {

#if defined(_WIN32)
// Expand a KAFFINITY bitset (group 0) into a list of logical processor ids.
std::vector<int> MaskToLogicalProcessors(KAFFINITY mask) {
    std::vector<int> procs;
    for (int bit = 0; bit < 64; ++bit) {
        if (mask & (KAFFINITY{1} << bit)) procs.push_back(bit);
    }
    return procs;
}

struct WinTopologyData {
    // Per package: id and group mask (logical processors in this package).
    std::vector<std::pair<int, KAFFINITY>> sockets;
    std::vector<std::pair<int, KAFFINITY>> numa_nodes;
    std::vector<std::pair<int, KAFFINITY>> cores;        // physical cores
    std::vector<int>                       core_ids;
    uint64_t l1 = 0, l2 = 0, l3 = 0;
};

bool EnumerateRelation(LOGICAL_PROCESSOR_RELATIONSHIP rel, WinTopologyData& out) {
    DWORD len = 0;
    if (!GetLogicalProcessorInformationEx(rel, nullptr, &len)) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;
    }
    if (len == 0) return false;
    std::vector<uint8_t> buffer(len);
    if (!GetLogicalProcessorInformationEx(rel, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &len)) {
        return false;
    }
    DWORD offset = 0;
    while (offset < len) {
        auto* entry = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
        switch (entry->Relationship) {
            case RelationProcessorCore: {
                // Use first group mask (single-group assumption).
                KAFFINITY mask = entry->Processor.GroupMask[0].Mask;
                out.cores.push_back({static_cast<int>(out.cores.size()), mask});
                out.core_ids.push_back(static_cast<int>(out.core_ids.size()));
                break;
            }
            case RelationProcessorPackage: {
                KAFFINITY mask = entry->Processor.GroupMask[0].Mask;
                out.sockets.push_back({static_cast<int>(out.sockets.size()), mask});
                break;
            }
            case RelationNumaNode: {
                KAFFINITY mask = entry->NumaNode.GroupMask.Mask;
                out.numa_nodes.push_back({static_cast<int>(out.numa_nodes.size()), mask});
                break;
            }
            case RelationCache: {
                const uint64_t sz = static_cast<uint64_t>(entry->Cache.CacheSize);
                const BYTE level = entry->Cache.Level;
                if (level == 1 && sz > out.l1) out.l1 = sz;
                else if (level == 2 && sz > out.l2) out.l2 = sz;
                else if (level == 3 && sz > out.l3) out.l3 = sz;
                break;
            }
            default:
                break;
        }
        offset += entry->Size;
        if (entry->Size == 0) break;
    }
    return true;
}
#endif  // _WIN32

}  // namespace

HardwareTopologyMap::HardwareTopologyMap()
    : core_count_(0), thread_count_(0), socket_count_(1), numa_node_count_(1), initialized_(false) {}

HardwareTopologyMap::~HardwareTopologyMap() {}

bool HardwareTopologyMap::Initialize() {
    DiscoverTopology();
    initialized_ = true;
    return true;
}

bool HardwareTopologyMap::IsInitialized() const {
    return initialized_;
}

std::vector<TopologyObject> HardwareTopologyMap::GetTopology(TopologyLevel level) const {
    std::vector<TopologyObject> objects;
    for (const auto& pair : topology_objects_) {
        if (pair.second.level == level) {
            objects.push_back(pair.second);
        }
    }
    return objects;
}

TopologyObject HardwareTopologyMap::GetObject(int id) const {
    auto it = topology_objects_.find(id);
    if (it != topology_objects_.end()) {
        return it->second;
    }
    return TopologyObject();
}

int HardwareTopologyMap::GetCoreCount() const { return core_count_; }
int HardwareTopologyMap::GetThreadCount() const { return thread_count_; }
int HardwareTopologyMap::GetSocketCount() const { return socket_count_; }
int HardwareTopologyMap::GetNUMANodeCount() const { return numa_node_count_; }

TopologyObject HardwareTopologyMap::GetParent(const TopologyObject& obj) const {
    return GetObject(obj.parent_id);
}

std::vector<TopologyObject> HardwareTopologyMap::GetChildren(const TopologyObject& obj) const {
    std::vector<TopologyObject> children;
    for (int child_id : obj.child_ids) {
        children.push_back(GetObject(child_id));
    }
    return children;
}

std::vector<TopologyObject> HardwareTopologyMap::GetCacheHierarchy() const {
    return GetTopology(TopologyLevel::CACHE_GROUP);
}

uint64_t HardwareTopologyMap::GetCacheSize(TopologyLevel level) const {
    auto caches = GetTopology(level);
    if (!caches.empty()) {
        return caches[0].cache_size_kb;
    }
    return 0;
}

int HardwareTopologyMap::GetNUMANode(int logical_processor) const {
#ifdef _WIN32
    // Find the NUMA node whose mask includes this logical processor.
    for (const auto& pair : topology_objects_) {
        if (pair.second.level != TopologyLevel::NUMA_NODE) continue;
        for (int p : pair.second.logical_processors) {
            if (p == logical_processor) return pair.second.id - 200;
        }
    }
    return 0;
#elif defined(__linux__)
    // /sys/devices/system/node/nodeX/cpulist contains comma/range cpu lists
    for (int n = 0; n < numa_node_count_; ++n) {
        const std::string cpulist_path =
            "/sys/devices/system/node/node" + std::to_string(n) + "/cpulist";
        std::ifstream f(cpulist_path);
        if (!f.is_open()) continue;
        std::string cpulist;
        std::getline(f, cpulist);
        std::istringstream iss(cpulist);
        std::string token;
        while (std::getline(iss, token, ',')) {
            const auto dash = token.find('-');
            if (dash == std::string::npos) {
                try {
                    if (std::stoi(token) == logical_processor) return n;
                } catch (...) {}
            } else {
                try {
                    int lo = std::stoi(token.substr(0, dash));
                    int hi = std::stoi(token.substr(dash + 1));
                    if (logical_processor >= lo && logical_processor <= hi) return n;
                } catch (...) {}
            }
        }
    }
    return 0;
#else
    return 0;
#endif
}

std::vector<int> HardwareTopologyMap::GetProcsOnNUMA(int numa_node) const {
    std::vector<int> procs;
#ifdef _WIN32
    auto it = topology_objects_.find(200 + numa_node);
    if (it != topology_objects_.end()) return it->second.logical_processors;
#elif defined(__linux__)
    const std::string cpulist_path =
        "/sys/devices/system/node/node" + std::to_string(numa_node) + "/cpulist";
    std::ifstream f(cpulist_path);
    if (f.is_open()) {
        std::string cpulist;
        std::getline(f, cpulist);
        std::istringstream iss(cpulist);
        std::string token;
        while (std::getline(iss, token, ',')) {
            const auto dash = token.find('-');
            if (dash == std::string::npos) {
                try { procs.push_back(std::stoi(token)); } catch (...) {}
            } else {
                try {
                    int lo = std::stoi(token.substr(0, dash));
                    int hi = std::stoi(token.substr(dash + 1));
                    for (int i = lo; i <= hi; ++i) procs.push_back(i);
                } catch (...) {}
            }
        }
    }
#endif
    return procs;
}

std::string HardwareTopologyMap::GetTopologyString() const {
    std::string result;
    result += "Sockets: " + std::to_string(socket_count_) + "\n";
    result += "NUMA Nodes: " + std::to_string(numa_node_count_) + "\n";
    result += "Cores: " + std::to_string(core_count_) + "\n";
    result += "Threads: " + std::to_string(thread_count_) + "\n";
    return result;
}

void HardwareTopologyMap::DiscoverTopology() {
#ifdef _WIN32
    WinTopologyData data;
    EnumerateRelation(RelationProcessorCore,    data);
    EnumerateRelation(RelationProcessorPackage, data);
    EnumerateRelation(RelationNumaNode,         data);
    EnumerateRelation(RelationCache,            data);

    thread_count_     = 0;
    for (const auto& c : data.cores) {
        const auto procs = MaskToLogicalProcessors(c.second);
        thread_count_ += static_cast<int>(procs.size());
    }
    core_count_       = static_cast<int>(data.cores.size());
    socket_count_     = (std::max)(1, static_cast<int>(data.sockets.size()));
    numa_node_count_  = (std::max)(1, static_cast<int>(data.numa_nodes.size()));

    if (thread_count_ == 0) {
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        thread_count_ = static_cast<int>(sys_info.dwNumberOfProcessors);
        if (core_count_ == 0) core_count_ = thread_count_ / 2;
    }

    // MACHINE root
    TopologyObject machine{};
    machine.level = TopologyLevel::MACHINE;
    machine.id = 0;
    machine.parent_id = -1;

    // Sockets (id 100+i)
    std::vector<int> socket_ids;
    for (const auto& s : data.sockets) {
        TopologyObject obj{};
        obj.level = TopologyLevel::SOCKET;
        obj.id = 100 + s.first;
        obj.parent_id = 0;
        obj.logical_processors = MaskToLogicalProcessors(s.second);
        obj.cache_size_kb = 0;
        topology_objects_[obj.id] = obj;
        socket_ids.push_back(obj.id);
    }

    // NUMA nodes (id 200+i)
    std::vector<int> numa_ids;
    for (const auto& n : data.numa_nodes) {
        TopologyObject obj{};
        obj.level = TopologyLevel::NUMA_NODE;
        obj.id = 200 + n.first;
        obj.parent_id = 0;
        obj.logical_processors = MaskToLogicalProcessors(n.second);
        topology_objects_[obj.id] = obj;
        numa_ids.push_back(obj.id);
    }

    // Cores (id 1000+i); parent = the socket whose mask is a superset.
    std::vector<int> core_ids;
    for (const auto& c : data.cores) {
        const auto procs = MaskToLogicalProcessors(c.second);
        TopologyObject obj{};
        obj.level = TopologyLevel::CORE;
        obj.id = 1000 + c.first;
        obj.logical_processors = procs;

        // Find owning socket.
        int parent_socket = 0;
        for (const auto& s : data.sockets) {
            const auto sp = MaskToLogicalProcessors(s.second);
            // If every logical proc of this core is in the socket, this core
            // belongs to that socket.
            bool all_in = true;
            for (int p : procs) {
                if (std::find(sp.begin(), sp.end(), p) == sp.end()) { all_in = false; break; }
            }
            if (all_in && !sp.empty()) { parent_socket = 100 + s.first; break; }
        }
        obj.parent_id = parent_socket;
        topology_objects_[obj.id] = obj;
        core_ids.push_back(obj.id);
        if (parent_socket != 0) {
            topology_objects_[parent_socket].child_ids.push_back(obj.id);
        }
    }

    // Cache groups (id 2000+i) — one per level found.
    if (data.l1 || data.l2 || data.l3) {
        int cache_idx = 0;
        auto add_cache = [&](uint64_t size) {
            if (size == 0) return;
            TopologyObject obj{};
            obj.level = TopologyLevel::CACHE_GROUP;
            obj.id = 2000 + cache_idx;
            obj.parent_id = 0;
            obj.cache_size_kb = size / 1024;
            topology_objects_[obj.id] = obj;
            machine.child_ids.push_back(obj.id);
            ++cache_idx;
        };
        add_cache(data.l1);
        add_cache(data.l2);
        add_cache(data.l3);
    }

    // Wire socket/numa children into the machine.
    for (int sid : socket_ids) machine.child_ids.push_back(sid);
    for (int nid : numa_ids)   machine.child_ids.push_back(nid);
    // Cores whose parent is the machine (no socket found) attach to machine.
    for (int cid : core_ids) {
        if (topology_objects_[cid].parent_id == 0) {
            machine.child_ids.push_back(cid);
        }
    }
    topology_objects_[0] = machine;

#elif defined(__linux__)
    thread_count_ = sysconf(_SC_NPROCESSORS_ONLN);

    // Physical core count from /proc/cpuinfo
    {
        std::ifstream cpuinfo("/proc/cpuinfo");
        int max_core_id = -1;
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.rfind("core id", 0) == 0) {
                const auto colon = line.find(':');
                if (colon != std::string::npos) {
                    try {
                        int cid = std::stoi(line.substr(colon + 1));
                        if (cid > max_core_id) max_core_id = cid;
                    } catch (...) {}
                }
            }
        }
        core_count_ = (max_core_id >= 0) ? (max_core_id + 1) : thread_count_;
    }

    // Count NUMA nodes from /sys/devices/system/node/
    {
        numa_node_count_ = 0;
        for (int n = 0; n < 64; ++n) {
            std::ifstream f("/sys/devices/system/node/node" +
                            std::to_string(n) + "/cpulist");
            if (!f.is_open()) break;
            ++numa_node_count_;
        }
        if (numa_node_count_ == 0) numa_node_count_ = 1;
    }

    // Count sockets from physical_package_id
    {
        int max_pkg = 0;
        for (int cpu = 0; cpu < thread_count_; ++cpu) {
            std::ifstream pf("/sys/devices/system/cpu/cpu" +
                             std::to_string(cpu) +
                             "/topology/physical_package_id");
            if (!pf.is_open()) break;
            int pkg = 0;
            pf >> pkg;
            if (pkg > max_pkg) max_pkg = pkg;
        }
        socket_count_ = max_pkg + 1;
    }

    // MACHINE root
    TopologyObject machine{};
    machine.level = TopologyLevel::MACHINE;
    machine.id = 0;
    machine.parent_id = -1;
    topology_objects_[0] = machine;

    // Sockets (id 100+i)
    for (int i = 0; i < socket_count_; ++i) {
        TopologyObject obj{};
        obj.level = TopologyLevel::SOCKET;
        obj.id = 100 + i;
        obj.parent_id = 0;
        topology_objects_[obj.id] = obj;
        topology_objects_[0].child_ids.push_back(obj.id);
    }

    // NUMA nodes (id 200+i)
    for (int n = 0; n < numa_node_count_; ++n) {
        TopologyObject obj{};
        obj.level = TopologyLevel::NUMA_NODE;
        obj.id = 200 + n;
        obj.parent_id = 0;
        obj.logical_processors = GetProcsOnNUMA(n);
        topology_objects_[obj.id] = obj;
        topology_objects_[0].child_ids.push_back(obj.id);
    }

    // Per-logical-CPU CORE leaves (id 1000+i)
    for (int cpu = 0; cpu < thread_count_; ++cpu) {
        TopologyObject core_obj{};
        core_obj.level = TopologyLevel::CORE;
        core_obj.id = 1000 + cpu;
        core_obj.parent_id = 0;
        core_obj.logical_processors.push_back(cpu);
        topology_objects_[1000 + cpu] = core_obj;
        topology_objects_[0].child_ids.push_back(1000 + cpu);
    }

    // Cache sizes from cpu0 (best-effort, uniform hierarchy assumed)
    {
        uint64_t l1 = 0, l2 = 0, l3 = 0;
        for (int idx = 0; idx < 8; ++idx) {
            const std::string base = "/sys/devices/system/cpu/cpu0/cache/index" + std::to_string(idx);
            std::ifstream level_f(base + "/level");
            if (!level_f.is_open()) break;
            int level = 0; level_f >> level;
            std::ifstream size_f(base + "/size");
            if (!size_f.is_open()) continue;
            std::string size_str; size_f >> size_str;
            uint64_t kb = 0;
            try {
                double v = std::stod(size_str);
                char unit = size_str.back();
                if (unit == 'K')      kb = static_cast<uint64_t>(v);
                else if (unit == 'M') kb = static_cast<uint64_t>(v * 1024);
                else if (unit == 'G') kb = static_cast<uint64_t>(v * 1024 * 1024);
                else                  kb = static_cast<uint64_t>(v / 1024);
            } catch (...) {}
            if (level == 1)      l1 = kb;
            else if (level == 2) l2 = kb;
            else if (level == 3) l3 = kb;
        }
        int cache_idx = 0;
        auto add_cache = [&](uint64_t size) {
            if (size == 0) return;
            TopologyObject obj{};
            obj.level = TopologyLevel::CACHE_GROUP;
            obj.id = 2000 + cache_idx;
            obj.parent_id = 0;
            obj.cache_size_kb = size;
            topology_objects_[obj.id] = obj;
            topology_objects_[0].child_ids.push_back(obj.id);
            ++cache_idx;
        };
        add_cache(l1); add_cache(l2); add_cache(l3);
    }
#endif
}

}  // namespace XER::AsyncIO::IO::Hardware
