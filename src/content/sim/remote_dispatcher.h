#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "agent.h"

namespace XER::Content::NetTest {
class ProtocolClient;
}

namespace XER::AsyncIO::IO::Async {
class AsyncTaskQueue;
}

namespace XER::Content::Sim {

struct CrowdStats;

// Dispatches crowd-simulation jobs: parallel local batches through
// AsyncTaskQueue and remote spawn/result exchange through the
// nettest ProtocolClient (XER network stack).
//
// Wire protocol (one line each, see nettest/protocol_message.h):
//   SPAWN|count=N,speed_min=F,speed_max=F,radius=R|<base64 AgentSpawn blob>
//   RESULTS|tick=T,arrived=A,moving=M|<base64 CrowdStats blob>
// The payload blob layout is documented in remote_dispatcher.cc.
class RemoteDispatcher {
public:
	RemoteDispatcher();
	~RemoteDispatcher();

	RemoteDispatcher(const RemoteDispatcher&) = delete;
	RemoteDispatcher& operator=(const RemoteDispatcher&) = delete;

	// Runs `job` on the shared AsyncTaskQueue with the given priority.
	// Returns the task id (0 on enqueue failure).
	std::uint64_t EnqueueLocal(std::function<void()> job, int priority = 0);

	// Sends a spawn request for `count` agents to a remote sim host.
	bool DispatchSpawn(NetTest::ProtocolClient& client, std::size_t count,
			   float min_speed, float max_speed, float radius,
			   std::string* error_out = nullptr);

	// Requests the remote sim's latest stats; fills `stats` on success.
	bool CollectStats(NetTest::ProtocolClient& client, CrowdStats* stats,
			  std::int64_t* tick_out = nullptr,
			  std::string* error_out = nullptr);

private:
	std::unique_ptr<XER::AsyncIO::IO::Async::AsyncTaskQueue> queue_;
};

}  // namespace XER::Content::Sim
