#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <absl/container/flat_hash_map.h>

namespace XER::Content::Sim {

struct Vec2 {
	float x = 0.0f;
	float y = 0.0f;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float Length(Vec2 v) { return std::sqrt(Dot(v, v)); }
inline Vec2 Normalize(Vec2 v) {
	const float len = Length(v);
	return len > 1e-6f ? v * (1.0f / len) : Vec2{};
}
inline float Distance(Vec2 a, Vec2 b) { return Length(a - b); }

// Per-agent state payload: named key/value pairs plus the tick the
// state was entered. Stored in an Abseil flat_hash_map for cheap lookups.
struct AgentStateData {
	absl::flat_hash_map<std::string, std::string> values;
	std::int64_t entered_tick = 0;
};

// An agent in a crowd/traffic simulation. `state` names the current
// FSM state; `goal_state` is the requested next state consumed by the
// AgentFSM on the next update.
struct Agent {
	std::uint64_t id = 0;
	Vec2 position;
	Vec2 velocity;
	Vec2 goal;             // navigation target in world space
	float max_speed = 1.0f;
	float radius = 0.5f;   // body radius for separation
	float patience = 0.0f; // seconds spent waiting (generic metric)
	bool arrived = false;
	std::string state = "idle";
	std::string goal_state;
	AgentStateData state_data;
	std::string tag;       // free-form grouping label

	// Optional per-tick logic hook invoked by the simulator.
	std::function<void(Agent&, float dt)> on_update;
};

using AgentUpdateFn = std::function<void(Agent&, float dt)>;
using TransitionGuard = std::function<bool(const Agent&)>;

// Lightweight finite-state machine layered on Agent. States own an
// update callback; transitions are guarded and evaluated each tick.
class AgentFSM {
public:
	void AddState(const std::string& name, AgentUpdateFn update);
	void AddTransition(const std::string& from, const std::string& to,
			   TransitionGuard guard = {});
	void RequestState(Agent& agent, const std::string& to) {
		agent.goal_state = to;
	}
	void Update(Agent& agent, float dt, std::int64_t tick);
	bool HasState(const std::string& name) const;

private:
	absl::flat_hash_map<std::string, AgentUpdateFn> updates_;
	// "from" -> list of {to, guard}
	absl::flat_hash_map<std::string,
			    std::vector<std::pair<std::string, TransitionGuard>>>
	    transitions_;
};

}  // namespace XER::Content::Sim
