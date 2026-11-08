// Smoke test for the new content application modules:
//   model3d (Assimp), dawn (WebGPU), sim (crowd + FSM + remote dispatch),
//   nettest (protocol codec + async TCP client).
//
// Dawn init is environment-dependent (needs a GPU/D3D12 device); its
// failure is reported as SKIP rather than FAIL.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "content/dawn/dawn_canvas.h"
#include "content/dawn/dawn_context.h"
#include "content/formatconv/batch_converter.h"
#include "content/model3d/model_document.h"
#include "content/modeldiff/model_diff.h"
#include "content/nettest/protocol_client.h"
#include "content/nettest/protocol_message.h"
#include "content/network/socket/socket_server.h"
#include "content/network/socket/socket_stream.h"
#include "content/sim/agent.h"
#include "content/sim/crowd_simulator.h"
#include "content/sim/remote_dispatcher.h"

using namespace XER::Content;

static int g_failures = 0;

#define CHECK(cond, msg)                                              \
	do {                                                          \
		if (!(cond)) {                                        \
			std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, \
				    __LINE__);                          \
			++g_failures;                                 \
		}                                                     \
	} while (0)

// --- nettest codec ------------------------------------------------------

static void TestCodec() {
	NetTest::ProtocolMessage msg;
	msg.name = "AUTH";
	msg.SetField("user", "alice");
	msg.SetField("token", "a,b|c=d");   // forces quoting
	msg.SetField("seq", "42");
	const std::string line = msg.Serialize();
	CHECK(line.find("AUTH|") == 0, "codec: name prefix");

	NetTest::ProtocolMessage parsed;
	std::string err;
	CHECK(parsed.Parse(line, &err), "codec: parse roundtrip");
	CHECK(parsed.name == "AUTH", "codec: name");
	CHECK(parsed.GetField("user") && *parsed.GetField("user") == "alice",
	      "codec: user field");
	CHECK(parsed.GetField("token") && *parsed.GetField("token") == "a,b|c=d",
	      "codec: quoted field");

	// Compressed payload roundtrip.
	NetTest::ProtocolMessage comp;
	comp.name = "DATA";
	comp.compressed = true;
	comp.payload.assign(2048, 'x');
	const std::string cline = comp.Serialize();
	CHECK(cline.find("DATA.z|") == 0, "codec: compressed name mark");
	NetTest::ProtocolMessage cparsed;
	CHECK(cparsed.Parse(cline, &err), "codec: compressed parse");
	CHECK(cparsed.payload.size() == 2048 && cparsed.payload[0] == 'x',
	      "codec: compressed payload bytes");

	// Malformed input must fail cleanly.
	NetTest::ProtocolMessage bad;
	CHECK(!bad.Parse("not a frame", &err), "codec: rejects garbage");
	CHECK(!bad.Parse("lowercase|f=v|", &err), "codec: rejects bad name");
}

// --- nettest client over loopback ---------------------------------------

static void EchoServer(XER::Network::Socket::SocketServer* server,
		       std::atomic<bool>* stop) {
	while (!stop->load()) {
		std::string err;
		auto client = server->Accept(&err);
		if (!client || !client->stream()) {
			if (stop->load()) break;
			continue;
		}
		for (;;) {
			std::string line;
			if (!client->stream()->ReadLine(&line, &err)) break;
			if (!line.empty() && line.back() == '\n') line.pop_back();
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty()) continue;
			std::string out = line + "\r\n";
			if (!client->stream()->WriteAll(out, &err)) break;
		}
	}
}

static void TestClientLoopback() {
	XER::Network::Socket::SocketServer server;
	std::string err;
	// SocketServer reports the requested port; port 0 (ephemeral) is not
	// reflected back, so use a fixed high port for the loopback test.
	CHECK(server.Listen("127.0.0.1", 47819, 4, &err), "nettest: listen");
	const std::uint16_t port = server.GetListenPort();
	CHECK(port != 0, "nettest: got port");

	std::atomic<bool> stop{false};
	std::thread srv_thread(EchoServer, &server, &stop);

	NetTest::ProtocolClient client;
	CHECK(client.Connect("127.0.0.1", port, 3000, &err),
	      "nettest: connect");
	if (!client.IsConnected()) {
		std::printf("  connect error: %s\n", err.c_str());
	}

	NetTest::ProtocolMessage ping;
	ping.name = "PING";
	ping.SetField("seq", "7");
	NetTest::ProtocolMessage resp;
	double rtt = -1.0;
	CHECK(client.RequestResponse(ping, &resp, &rtt, 3000, &err),
	      "nettest: request/response");
	if (!err.empty()) std::printf("  rr error: %s\n", err.c_str());
	CHECK(resp.name == "PING", "nettest: echo name");
	CHECK(rtt >= 0.0, "nettest: rtt measured");

	const auto stats = client.Stats();
	CHECK(stats.requests_sent == 1 && stats.responses_ok == 1,
	      "nettest: stats counted");

	client.Disconnect();
	stop = true;
	server.Close();
	srv_thread.join();
}

// --- sim ----------------------------------------------------------------

static void TestSim() {
	Sim::CrowdConfig cfg;
	cfg.bounds_min = {0, 0};
	cfg.bounds_max = {50, 50};
	cfg.neighbor_radius = 4.0f;
	Sim::CrowdSimulator sim(cfg);

	Sim::AgentFSM fsm;
	fsm.AddState("idle", [](Sim::Agent&, float) {});
	fsm.AddState("walk", [](Sim::Agent& a, float) {
		a.state_data.values["steps"] = "1";
	});
	fsm.AddTransition("idle", "walk", [](const Sim::Agent&) { return true; });
	fsm.AddTransition("walk", "idle", [](const Sim::Agent& a) {
		return a.arrived;
	});
	sim.SetFSM(&fsm);

	Sim::AgentSpawn spawn;
	spawn.position = {1, 1};
	spawn.goal = {10, 10};
	spawn.max_speed = 3.0f;
	const std::uint64_t id = sim.AddAgent(spawn);
	CHECK(id != 0, "sim: add agent");

	sim.SpawnRandom(20, 0.5f, 2.0f);
	CHECK(sim.agents().size() == 21, "sim: agent count");

	for (int i = 0; i < 600; ++i) sim.Step(1.0f / 60.0f);

	const auto stats = sim.Stats();
	CHECK(stats.agents == 21, "sim: stats agents");
	CHECK(stats.arrived > 0, "sim: some agents arrived");

	// FSM transition check on the seeded agent.
	const Sim::Agent* a = sim.FindAgent(id);
	CHECK(a != nullptr, "sim: find agent");
	if (a) {
		CHECK(a->state == "walk" || a->state == "idle",
		      "sim: fsm state valid");
	}

	const auto nearby = sim.AgentsNear(Sim::Vec2{5.0f, 5.0f}, 10.0f);
	CHECK(!nearby.empty(), "sim: spatial query");

	// Remote dispatcher enqueue path (no network needed).
	Sim::RemoteDispatcher disp;
	const std::uint64_t task_id = disp.EnqueueLocal([]() {}, 1);
	CHECK(task_id != 0, "sim: async task enqueued");
}

// --- model3d ------------------------------------------------------------

static void TestModel3D() {
	// Minimal OBJ: a single triangle + a material-ish name.
	const char* obj =
	    "v 0 0 0\n"
	    "v 1 0 0\n"
	    "v 0 1 0\n"
	    "vn 0 0 1\n"
	    "vt 0 0\n"
	    "usemtl test_mat\n"
	    "f 1/1/1 2/1/1 3/1/1\n";

	char tmp[MAX_PATH];
	GetTempPathA(MAX_PATH, tmp);
	const std::string path = std::string(tmp) + "xer_smoke_tri.obj";
	{
		std::ofstream f(path, std::ios::binary);
		f << obj;
	}

	Model3D::ModelScene scene;
	std::string err;
	CHECK(Model3D::ModelLoader::Load(path, &scene, &err),
	      "model3d: load obj");
	if (!err.empty()) std::printf("  model err: %s\n", err.c_str());
	CHECK(scene.loaded, "model3d: scene loaded");
	CHECK(scene.meshes.size() == 1, "model3d: one mesh");
	CHECK(scene.meshes[0].positions.size() == 3, "model3d: 3 verts");
	CHECK(scene.meshes[0].indices.size() == 3, "model3d: 3 indices");
	CHECK(scene.bounds.valid, "model3d: bounds computed");
	CHECK(!Model3D::ModelLoader::SupportedImportFormats().empty(),
	      "model3d: format list");
	DeleteFileA(path.c_str());
}

// --- formatconv ---------------------------------------------------------

static void TestFormatConv() {
	using namespace XER::Content;

	CHECK(FormatConv::BatchConverter::IsExportFormatSupported("obj"),
	      "formatconv: obj export supported");
	CHECK(!FormatConv::BatchConverter::IsExportFormatSupported("bogus_fmt"),
	      "formatconv: bogus format rejected");

	// Pick an export format that also has an importer, so the round-trip
	// verification below exercises both directions.
	std::string target;
	const auto import_fmts =
	    Model3D::ModelLoader::SupportedImportFormats();
	for (const std::string& ext :
	     FormatConv::BatchConverter::SupportedExportFormats()) {
		if (ext == "obj") continue;
		if (std::find(import_fmts.begin(), import_fmts.end(), ext) !=
		    import_fmts.end()) {
			target = ext;
			break;
		}
	}
	if (target.empty()) {
		std::printf("SKIP: formatconv: no round-trippable export format\n");
		return;
	}

	char tmp[MAX_PATH];
	GetTempPathA(MAX_PATH, tmp);
	const std::string dir = std::string(tmp) + "xer_fmtconv\\";
	CreateDirectoryA(dir.c_str(), nullptr);

	const char* obj =
	    "v 0 0 0\n"
	    "v 1 0 0\n"
	    "v 0 1 0\n"
	    "vn 0 0 1\n"
	    "vt 0 0\n"
	    "usemtl m\n"
	    "f 1/1/1 2/1/1 3/1/1\n";
	std::vector<std::string> inputs;
	for (int i = 0; i < 3; ++i) {
		const std::string p = dir + "tri_" + std::to_string(i) + ".obj";
		std::ofstream f(p, std::ios::binary);
		f << obj;
		inputs.push_back(p);
	}

	FormatConv::ConvertOptions opts;
	opts.target_format = target;
	opts.max_parallel = 2;
	opts.overwrite = true;

	const auto results =
	    FormatConv::BatchConverter::ConvertFiles(inputs, opts);
	CHECK(results.size() == 3, "formatconv: result count");
	for (const auto& r : results) {
		if (!r.ok) {
			std::printf("  conv err %s: %s\n", r.input_path.c_str(),
			            r.error.c_str());
		}
		CHECK(r.ok, "formatconv: convert ok");
		CHECK(GetFileAttributesA(r.output_path.c_str()) !=
		          INVALID_FILE_ATTRIBUTES,
		      "formatconv: output exists");
	}

	// Round-trip: the converted file must load back through model3d.
	Model3D::ModelScene scene;
	std::string err;
	CHECK(Model3D::ModelLoader::Load(results[0].output_path, &scene,
	                                 &err),
	      "formatconv: reload converted");
	if (!scene.meshes.empty()) {
		CHECK(scene.meshes[0].positions.size() >= 3,
		      "formatconv: geometry preserved");
	}

	// Error path: missing input reports per-file failure, batch continues.
	const auto bad = FormatConv::BatchConverter::ConvertFiles(
	    {dir + "does_not_exist.obj"}, opts);
	CHECK(bad.size() == 1 && !bad[0].ok && !bad[0].error.empty(),
	      "formatconv: missing input error");

	for (const auto& p : inputs) DeleteFileA(p.c_str());
	for (const auto& r : results) DeleteFileA(r.output_path.c_str());
	DeleteFileA((dir + "does_not_exist." + target).c_str());
	RemoveDirectoryA(dir.c_str());
}

// --- modeldiff ----------------------------------------------------------

static void TestModelDiff() {
	using namespace XER::Content;

	char tmp[MAX_PATH];
	GetTempPathA(MAX_PATH, tmp);
	const std::string dir = std::string(tmp) + "xer_modeldiff\\";
	CreateDirectoryA(dir.c_str(), nullptr);

	// v1: one triangle. v2: same object name but a quad (4 verts, 6 idx).
	const std::string objA =
	    "o body\n"
	    "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
	    "vn 0 0 1\nvt 0 0\n"
	    "f 1/1/1 2/1/1 3/1/1\n";
	const std::string objB =
	    "o body\n"
	    "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 1\n"
	    "vn 0 0 1\nvt 0 0\n"
	    "f 1/1/1 2/1/1 3/1/1\nf 1/1/1 3/1/1 4/1/1\n";
	const std::string pa = dir + "v1.obj";
	const std::string pb = dir + "v2.obj";
	{ std::ofstream f(pa, std::ios::binary); f << objA; }
	{ std::ofstream f(pb, std::ios::binary); f << objB; }

	Model3D::ModelScene sa, sb;
	std::string err;
	CHECK(Model3D::ModelLoader::Load(pa, &sa, &err), "modeldiff: load v1");
	CHECK(Model3D::ModelLoader::Load(pb, &sb, &err), "modeldiff: load v2");

	ModelDiff::DiffStats stats;
	ModelDiff::ModelDiff::Compare(sa, sb, &stats);
	CHECK(stats.meshes_a == 1 && stats.meshes_b == 1, "modeldiff: mesh count");
	CHECK(stats.changed_meshes.size() == 1,
	      "modeldiff: quad change detected");
	CHECK(stats.bounds_changed, "modeldiff: bounds changed");
	CHECK(!stats.empty(), "modeldiff: diff non-empty");

	// Identical scene diffs to empty.
	ModelDiff::DiffStats same;
	ModelDiff::ModelDiff::Compare(sa, sa, &same);
	CHECK(same.empty(), "modeldiff: identical scenes equal");

	// RE2 name search.
	const auto hits = ModelDiff::ModelDiff::SearchNames(sa, "bo.*");
	CHECK(!hits.empty(), "modeldiff: re2 search hits");
	CHECK(ModelDiff::ModelDiff::SearchNames(sa, "[").empty(),
	      "modeldiff: invalid re2 rejected");
	CHECK(!ModelDiff::ModelDiff::NodePaths(sa).empty(),
	      "modeldiff: node paths");

	DeleteFileA(pa.c_str());
	DeleteFileA(pb.c_str());
	RemoveDirectoryA(dir.c_str());
}

// --- dawn ---------------------------------------------------------------

static void TestDawn() {
	Dawn::DawnContext ctx;
	std::string err;
	if (!ctx.Initialize(&err)) {
		std::printf("SKIP: dawn init unavailable: %s\n", err.c_str());
		return;
	}
	CHECK(ctx.is_ready(), "dawn: context ready");
	std::printf("  adapter: %s\n", ctx.adapter_info().c_str());

	Dawn::DawnCanvas canvas(&ctx);
	CHECK(canvas.Initialize(64, 64, &err), "dawn: canvas init");
	if (!err.empty()) std::printf("  canvas err: %s\n", err.c_str());
	std::vector<std::uint8_t> rgba;
	CHECK(canvas.RenderTestPattern(&rgba, &err), "dawn: render pattern");
	if (!err.empty()) std::printf("  render err: %s\n", err.c_str());
	if (!rgba.empty()) {
		CHECK(rgba.size() == 64 * 64 * 4, "dawn: rgba size");
		// Center pixel should be the pipeline color (0.2,0.6,0.9).
		const std::size_t c = (32 * 64 + 32) * 4;
		CHECK(rgba[c] > 30 && rgba[c + 2] > 150, "dawn: pixel is bluish");
	}
}

int main() {
#ifdef _WIN32
	// The XER network stack expects Winsock to be initialized by the host.
	WSADATA wsa{};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		std::printf("FAIL: WSAStartup\n");
		return 1;
	}
#endif
	TestCodec();
	TestClientLoopback();
	TestSim();
	TestModel3D();
	TestModelDiff();
	TestFormatConv();
	TestDawn();

	if (g_failures == 0) {
		std::printf("content apps smoke: OK\n");
		return 0;
	}
	std::printf("content apps smoke: %d failure(s)\n", g_failures);
	return 1;
}
