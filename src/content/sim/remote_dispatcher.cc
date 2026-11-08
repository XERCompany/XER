#include "remote_dispatcher.h"

#include <cstring>

#include <async_io/async_task_queue.h>

#include "content/nettest/protocol_client.h"
#include "content/nettest/protocol_message.h"
#include "crowd_simulator.h"

namespace XER::Content::Sim {
namespace {

// Payload blob for SPAWN: {u64 seed, float bounds_min_x, bounds_min_y,
// bounds_max_x, bounds_max_y} — the remote reproduces SpawnRandom
// deterministically from these.
constexpr std::size_t kSpawnPayloadSize = 8 + 4 * sizeof(float);

// Payload blob for RESULTS: CrowdStats wire layout.
struct StatsBlob {
	std::uint64_t agents;
	std::uint64_t arrived;
	std::uint64_t moving;
	double avg_speed;
	double max_speed;
	float centroid_x;
	float centroid_y;
};
static_assert(sizeof(StatsBlob) == 8 * 3 + 8 * 2 + 4 * 2);

void PutU64(std::vector<std::uint8_t>& out, std::uint64_t v) {
	for (int i = 0; i < 8; ++i) {
		out.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
	}
}

void PutF32(std::vector<std::uint8_t>& out, float v) {
	std::uint32_t u;
	std::memcpy(&u, &v, sizeof(u));
	for (int i = 0; i < 4; ++i) {
		out.push_back(static_cast<std::uint8_t>((u >> (i * 8)) & 0xFF));
	}
}

bool GetU64(const std::vector<std::uint8_t>& in, std::size_t* pos,
	    std::uint64_t* out) {
	if (*pos + 8 > in.size()) return false;
	std::uint64_t v = 0;
	for (int i = 0; i < 8; ++i) {
		v |= static_cast<std::uint64_t>(in[*pos + i]) << (i * 8);
	}
	*pos += 8;
	*out = v;
	return true;
}

bool GetF32(const std::vector<std::uint8_t>& in, std::size_t* pos,
	    float* out) {
	if (*pos + 4 > in.size()) return false;
	std::uint32_t u = 0;
	for (int i = 0; i < 4; ++i) {
		u |= static_cast<std::uint32_t>(in[*pos + i]) << (i * 8);
	}
	*pos += 4;
	std::memcpy(out, &u, sizeof(*out));
	return true;
}

}  // namespace

RemoteDispatcher::RemoteDispatcher()
    : queue_(std::make_unique<XER::AsyncIO::IO::Async::AsyncTaskQueue>()) {}

RemoteDispatcher::~RemoteDispatcher() {
	if (queue_) queue_->Shutdown();
}

std::uint64_t RemoteDispatcher::EnqueueLocal(std::function<void()> job,
					     int priority) {
	return queue_->EnqueueTask(std::move(job), priority);
}

bool RemoteDispatcher::DispatchSpawn(NetTest::ProtocolClient& client,
				     std::size_t count, float min_speed,
				     float max_speed, float radius,
				     std::string* error_out) {
	NetTest::ProtocolMessage msg;
	msg.name = "SPAWN";
	msg.SetField("count", std::to_string(count));
	msg.SetField("speed_min", std::to_string(min_speed));
	msg.SetField("speed_max", std::to_string(max_speed));
	msg.SetField("radius", std::to_string(radius));

	// Deterministic seed + default 100x100 bounds in the payload.
	msg.payload.reserve(kSpawnPayloadSize);
	PutU64(msg.payload, 0xC0FFEEull);
	PutF32(msg.payload, 0.0f);
	PutF32(msg.payload, 0.0f);
	PutF32(msg.payload, 100.0f);
	PutF32(msg.payload, 100.0f);

	NetTest::ProtocolMessage ack;
	if (!client.RequestResponse(msg, &ack, nullptr, 5000, error_out)) {
		return false;
	}
	return ack.name == "SPAWN_ACK";
}

bool RemoteDispatcher::CollectStats(NetTest::ProtocolClient& client,
				    CrowdStats* stats,
				    std::int64_t* tick_out,
				    std::string* error_out) {
	NetTest::ProtocolMessage req;
	req.name = "RESULTS";

	NetTest::ProtocolMessage resp;
	double rtt = 0.0;
	if (!client.RequestResponse(req, &resp, &rtt, 5000, error_out)) {
		return false;
	}
	if (resp.name != "RESULTS" || resp.payload.size() < sizeof(StatsBlob)) {
		if (error_out) *error_out = "malformed RESULTS response";
		return false;
	}

	StatsBlob blob{};
	std::memcpy(&blob, resp.payload.data(), sizeof(blob));
	stats->agents = static_cast<std::size_t>(blob.agents);
	stats->arrived = static_cast<std::size_t>(blob.arrived);
	stats->moving = static_cast<std::size_t>(blob.moving);
	stats->avg_speed = blob.avg_speed;
	stats->max_speed = blob.max_speed;
	stats->centroid = {blob.centroid_x, blob.centroid_y};

	if (tick_out) {
		if (const std::string* t = resp.GetField("tick")) {
			*tick_out = std::stoll(*t);
		}
	}
	return true;
}

}  // namespace XER::Content::Sim
