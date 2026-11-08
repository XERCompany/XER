#include "agent.h"

namespace XER::Content::Sim {

void AgentFSM::AddState(const std::string& name, AgentUpdateFn update) {
	updates_[name] = std::move(update);
}

void AgentFSM::AddTransition(const std::string& from, const std::string& to,
			     TransitionGuard guard) {
	transitions_[from].emplace_back(to, std::move(guard));
}

bool AgentFSM::HasState(const std::string& name) const {
	return updates_.contains(name);
}

void AgentFSM::Update(Agent& agent, float dt, std::int64_t tick) {
	// Apply a pending state request if a guarded transition allows it.
	if (!agent.goal_state.empty() && agent.goal_state != agent.state) {
		if (const auto it = transitions_.find(agent.state); it != transitions_.end()) {
			for (const auto& [to, guard] : it->second) {
				if (to == agent.goal_state && (!guard || guard(agent))) {
					agent.state = to;
					agent.state_data.entered_tick = tick;
					agent.goal_state.clear();
					break;
				}
			}
		}
	}
	if (const auto it = updates_.find(agent.state); it != updates_.end()) {
		it->second(agent, dt);
	}
}

}  // namespace XER::Content::Sim
