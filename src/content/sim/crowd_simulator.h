#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include <absl/container/flat_hash_map.h>

#include "agent.h"

namespace XER::Content::Sim {

struct CrowdConfig {
	Vec2 bounds_min{0.0f, 0.0f};
	Vec2 bounds_max{100.0f, 100.0f};
	float neighbor_radius = 5.0f;    // separation/neighbor query radius
	float separation_weight = 1.5f;  // steering repulsion strength
	float arrival_distance = 0.5f;   // distance at which agent "arrives"
	float max_agents = 100'000.0f;
	std::uint64_t seed = 0x9E3779B97F4A7C15ull;
};

struct AgentSpawn {
	Vec2 position;
	Vec2 goal;
	float max_speed = 1.0f;
	float radius = 0.5f;
	std::string tag;
};

struct CrowdStats {
	std::size_t agents = 0;
	std::size_t arrived = 0;
	std::size_t moving = 0;
	double avg_speed = 0.0;
	double max_speed = 0.0;
	Vec2 centroid{0.0f, 0.0f};
};

// Crowd/traffic-flow simulator. Agents seek goals with separation
// steering over a uniform-grid spatial hash. State logic is delegated
// to AgentFSM; per-agent hooks run through Agent::on_update.
class CrowdSimulator {
public:
	explicit CrowdSimulator(CrowdConfig config = {});

	std::uint64_t AddAgent(const AgentSpawn& spawn);
	std::vector<std::uint64_t> AddAgents(const std::vector<AgentSpawn>& spawns);
	// Random spawns inside bounds; goals random inside bounds.
	std::vector<std::uint64_t> SpawnRandom(std::size_t count,
					       float min_speed = 0.5f,
					       float max_speed = 2.0f);

	void Step(float dt_seconds);
	void SetBounds(Vec2 min, Vec2 max);
	void SetFSM(AgentFSM* fsm);  // non-owning; may be null

	const std::vector<Agent>& agents() const { return agents_; }
	Agent* FindAgent(std::uint64_t id);
	const Agent* FindAgent(std::uint64_t id) const;

	// Range-v3 friendly queries (return views over agents()).
	std::vector<const Agent*> AgentsNear(Vec2 point, float radius) const;
	std::vector<const Agent*> AgentsWithTag(const std::string& tag) const;
	std::vector<const Agent*> ArrivedAgents() const;
	CrowdStats Stats() const;

private:
	void RebuildGrid();
	std::int64_t CellKey(int cx, int cy) const;
	void CollectCellNeighbors(int cx, int cy,
				  std::vector<std::size_t>& out) const;

	CrowdConfig config_;
	std::vector<Agent> agents_;
	std::uint64_t next_id_ = 1;
	std::int64_t tick_ = 0;
	AgentFSM* fsm_ = nullptr;
	std::mt19937_64 rng_;

	// Uniform grid over agent indices, rebuilt each Step.
	float cell_size_ = 1.0f;
	absl::flat_hash_map<std::int64_t, std::vector<std::size_t>> grid_;
};

}  // namespace XER::Content::Sim
