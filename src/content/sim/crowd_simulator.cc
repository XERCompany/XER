#include "crowd_simulator.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <range/v3/all.hpp>

namespace XER::Content::Sim {
namespace rv = ranges;
namespace rvi = rv::views;

namespace {

Vec2 ClampToBounds(Vec2 p, const CrowdConfig& cfg) {
	p.x = std::clamp(p.x, cfg.bounds_min.x, cfg.bounds_max.x);
	p.y = std::clamp(p.y, cfg.bounds_min.y, cfg.bounds_max.y);
	return p;
}

Vec2 RandomInBounds(const CrowdConfig& cfg, std::mt19937_64& rng) {
	std::uniform_real_distribution<float> dx(cfg.bounds_min.x, cfg.bounds_max.x);
	std::uniform_real_distribution<float> dy(cfg.bounds_min.y, cfg.bounds_max.y);
	return {dx(rng), dy(rng)};
}

}  // namespace

CrowdSimulator::CrowdSimulator(CrowdConfig config)
    : config_(config), rng_(config.seed) {
	cell_size_ = std::max(1.0f, config_.neighbor_radius * 2.0f);
}

std::uint64_t CrowdSimulator::AddAgent(const AgentSpawn& spawn) {
	if (static_cast<float>(agents_.size()) >= config_.max_agents) {
		return 0;  // capacity guard
	}
	Agent agent;
	agent.id = next_id_++;
	agent.position = ClampToBounds(spawn.position, config_);
	agent.goal = ClampToBounds(spawn.goal, config_);
	agent.max_speed = spawn.max_speed;
	agent.radius = spawn.radius;
	agent.tag = spawn.tag;
	agents_.push_back(std::move(agent));
	return agents_.back().id;
}

std::vector<std::uint64_t> CrowdSimulator::AddAgents(
    const std::vector<AgentSpawn>& spawns) {
	std::vector<std::uint64_t> ids;
	ids.reserve(spawns.size());
	for (const auto& s : spawns) {
		if (const std::uint64_t id = AddAgent(s); id != 0) ids.push_back(id);
	}
	return ids;
}

std::vector<std::uint64_t> CrowdSimulator::SpawnRandom(std::size_t count,
						       float min_speed,
						       float max_speed) {
	std::vector<AgentSpawn> spawns;
	spawns.reserve(count);
	std::uniform_real_distribution<float> speed_dist(min_speed, max_speed);
	std::uniform_real_distribution<float> radius_dist(0.3f, 0.8f);
	for (std::size_t i = 0; i < count; ++i) {
		AgentSpawn s;
		s.position = RandomInBounds(config_, rng_);
		s.goal = RandomInBounds(config_, rng_);
		s.max_speed = speed_dist(rng_);
		s.radius = radius_dist(rng_);
		spawns.push_back(s);
	}
	return AddAgents(spawns);
}

void CrowdSimulator::SetBounds(Vec2 min, Vec2 max) {
	config_.bounds_min = min;
	config_.bounds_max = max;
}

void CrowdSimulator::SetFSM(AgentFSM* fsm) { fsm_ = fsm; }

std::int64_t CrowdSimulator::CellKey(int cx, int cy) const {
	// 21-bit packed coords; sufficient for worlds < 2M cells.
	return (static_cast<std::int64_t>(cx) << 32) |
	       (static_cast<std::uint32_t>(cy));
}

void CrowdSimulator::RebuildGrid() {
	grid_.clear();
	const float inv = 1.0f / cell_size_;
	for (std::size_t i = 0; i < agents_.size(); ++i) {
		const int cx = static_cast<int>(std::floor(agents_[i].position.x * inv));
		const int cy = static_cast<int>(std::floor(agents_[i].position.y * inv));
		grid_[CellKey(cx, cy)].push_back(i);
	}
}

void CrowdSimulator::CollectCellNeighbors(int cx, int cy,
					  std::vector<std::size_t>& out) const {
	for (int dx = -1; dx <= 1; ++dx) {
		for (int dy = -1; dy <= 1; ++dy) {
			if (const auto it = grid_.find(CellKey(cx + dx, cy + dy));
			    it != grid_.end()) {
				out.insert(out.end(), it->second.begin(), it->second.end());
			}
		}
	}
}

void CrowdSimulator::Step(float dt_seconds) {
	++tick_;
	RebuildGrid();
	const float inv_cell = 1.0f / cell_size_;
	const float sep_radius = config_.neighbor_radius;
	const float sep_radius_sq = sep_radius * sep_radius;
	std::vector<std::size_t> neighbors;
	neighbors.reserve(64);

	for (std::size_t i = 0; i < agents_.size(); ++i) {
		Agent& a = agents_[i];

		// FSM + custom logic first (may change goal/velocity).
		if (fsm_) fsm_->Update(a, dt_seconds, tick_);
		if (a.on_update) a.on_update(a, dt_seconds);

		if (a.arrived || a.max_speed <= 0.0f) {
			a.velocity = Vec2{};
			continue;
		}

		// Seek steering toward goal.
		const Vec2 to_goal = a.goal - a.position;
		const float goal_dist = Length(to_goal);
		Vec2 desired{0.0f, 0.0f};
		if (goal_dist > config_.arrival_distance) {
			desired = Normalize(to_goal) * a.max_speed;
		} else {
			a.arrived = true;
			a.velocity = Vec2{};
			continue;
		}

		// Separation from neighbors in the 3x3 cells around the agent.
		Vec2 separation{0.0f, 0.0f};
		neighbors.clear();
		const int cx = static_cast<int>(std::floor(a.position.x * inv_cell));
		const int cy = static_cast<int>(std::floor(a.position.y * inv_cell));
		CollectCellNeighbors(cx, cy, neighbors);
		for (const std::size_t j : neighbors) {
			if (j == i) continue;
			const Agent& other = agents_[j];
			const Vec2 delta = a.position - other.position;
			const float d_sq = Dot(delta, delta);
			if (d_sq > 1e-6f && d_sq < sep_radius_sq) {
				const float d = std::sqrt(d_sq);
				const float overlap = (sep_radius - d) / sep_radius;
				separation = separation + Normalize(delta) * overlap;
			}
		}

		// Combine steering and integrate.
		Vec2 steer = desired + separation * config_.separation_weight;
		const float steer_len = Length(steer);
		if (steer_len > a.max_speed) steer = steer * (a.max_speed / steer_len);
		a.velocity = steer;
		a.position = ClampToBounds(a.position + a.velocity * dt_seconds, config_);
		a.patience += dt_seconds;
	}
}

Agent* CrowdSimulator::FindAgent(std::uint64_t id) {
	for (Agent& a : agents_) {
		if (a.id == id) return &a;
	}
	return nullptr;
}

const Agent* CrowdSimulator::FindAgent(std::uint64_t id) const {
	for (const Agent& a : agents_) {
		if (a.id == id) return &a;
	}
	return nullptr;
}

std::vector<const Agent*> CrowdSimulator::AgentsNear(Vec2 point,
							     float radius) const {
	const float r_sq = radius * radius;
	std::vector<const Agent*> out;
	for (const Agent& a : agents_) {
		if (Dot(a.position - point, a.position - point) <= r_sq) {
			out.push_back(&a);
		}
	}
	return out;
}

std::vector<const Agent*> CrowdSimulator::AgentsWithTag(
    const std::string& tag) const {
	std::vector<const Agent*> out;
	for (const Agent& a : agents_ | rvi::filter(
					     [&tag](const Agent& ag) {
						     return ag.tag == tag;
					     })) {
		out.push_back(&a);
	}
	return out;
}

std::vector<const Agent*> CrowdSimulator::ArrivedAgents() const {
	std::vector<const Agent*> out;
	for (const Agent& a : agents_ |
	     rvi::filter([](const Agent& ag) { return ag.arrived; })) {
		out.push_back(&a);
	}
	return out;
}

CrowdStats CrowdSimulator::Stats() const {
	CrowdStats s;
	s.agents = agents_.size();
	double speed_sum = 0.0;
	for (const Agent& a : agents_) {
		if (a.arrived) {
			++s.arrived;
		} else {
			++s.moving;
		}
		const float sp = Length(a.velocity);
		speed_sum += sp;
		s.max_speed = std::max(s.max_speed, static_cast<double>(sp));
		s.centroid.x += a.position.x;
		s.centroid.y += a.position.y;
	}
	if (!agents_.empty()) {
		s.avg_speed = speed_sum / agents_.size();
		s.centroid.x /= static_cast<float>(agents_.size());
		s.centroid.y /= static_cast<float>(agents_.size());
	}
	return s;
}

}  // namespace XER::Content::Sim
