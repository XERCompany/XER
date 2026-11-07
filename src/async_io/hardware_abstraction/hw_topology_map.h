#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <map>

namespace XER::AsyncIO::IO::Hardware {

// Topology level
enum class TopologyLevel {
    MACHINE,
    SOCKET,
    NUMA_NODE,
    CACHE_GROUP,
    CORE,
    THREAD
};

struct TopologyObject {
    TopologyLevel level;
    int id;
    int parent_id;
    std::vector<int> child_ids;
    std::vector<int> logical_processors;
    uint64_t cache_size_kb;
};

class HardwareTopologyMap {
public:
    HardwareTopologyMap();
    ~HardwareTopologyMap();

    // Discovery
    bool Initialize();
    bool IsInitialized() const;

    // Topology queries
    std::vector<TopologyObject> GetTopology(TopologyLevel level) const;
    TopologyObject GetObject(int id) const;
    int GetCoreCount() const;
    int GetThreadCount() const;
    int GetSocketCount() const;
    int GetNUMANodeCount() const;

    // Relationships
    TopologyObject GetParent(const TopologyObject& obj) const;
    std::vector<TopologyObject> GetChildren(const TopologyObject& obj) const;

    // Cache topology
    std::vector<TopologyObject> GetCacheHierarchy() const;
    uint64_t GetCacheSize(TopologyLevel level) const;

    // NUMA affinity
    int GetNUMANode(int logical_processor) const;
    std::vector<int> GetProcsOnNUMA(int numa_node) const;

    // String representation
    std::string GetTopologyString() const;

private:
    std::map<int, TopologyObject> topology_objects_;
    int core_count_;
    int thread_count_;
    int socket_count_;
    int numa_node_count_;
    bool initialized_;

    void DiscoverTopology();
};

}  // namespace XER::AsyncIO::IO::Hardware
