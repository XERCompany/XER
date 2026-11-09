// ServerAudio — TCP audio processing server built on the XER src/ API.
//
// Line-based text protocol (one command per line, one reply line each,
// replies start with "OK" or "ERR"):
//   load <path>                         -> OK <id> <rate> <channels> <frames>
//   effects                             -> OK <name>,<name>,...
//   apply <id> <effect> [node.param=v]  -> OK <id> <effect> peak=<p> rms=<r>
//   info <id>                           -> OK <id> rate=.. channels=.. ...
//   reset <id>                          -> OK <id>
//   save <id> <path>                    -> OK <path> <bytes>
//   delete <id>                         -> OK <id>
//   list                                -> OK <id>,<id>,...
//   quit                                -> OK bye (closes the connection)
//
// Loaded audio is kept in memory and in the XER persistent cache
// (scope "server_audio", key = id), so ids survive a server restart.
//
// Usage: ServerAudio.exe [--bind <address>] [--port <port>]
//        (default 127.0.0.1:7450; use --bind 0.0.0.0 for network access)

#include <winsock2.h>
#include <windows.h>
#include <objbase.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/audio_core/audio_source_loader.h"
#include "audio/effects_rack/custom_effect.h"
#include "audio/effects_rack/custom_effect_invoke.h"
#include "audio/file_io_codecs/codec_ffmpeg_encode_helper.h"
#include "audio/file_io_codecs/codec_file_streamer.h"
#include "audio/file_io_codecs/codec_mp3_lame.h"
#include "audio/file_io_codecs/codec_wav_float.h"
#include "audio/file_io_codecs/codec_wav_pcm.h"
#include "audio/fx_customs/fx_customs_presets.h"
#include "cache/cache_entry.h"
#include "cache/cache_manager.h"
#include "content/network/socket/platform/socket_platform.h"
#include "content/network/socket/socket_server.h"
#include "content/network/socket/tcp_client_socket.h"
#include "helper/string_utils.h"

namespace {

namespace AudioCore = XER::Audio::Core;
namespace Codec = XER::Audio::CodecIO;
namespace FX = XER::Audio::FX;
namespace Str = XER::Helper::String;
using XER::Cache::CacheManager;
using XER::Network::Socket::SocketServer;
using XER::Network::Socket::TcpClientSocket;

constexpr char kCacheScope[] = "server_audio";
constexpr char kOriginalName[] = "original.wav";
constexpr char kCurrentName[] = "current.wav";
constexpr char kMetaName[] = "meta.txt";
constexpr std::uint16_t kDefaultPort = 7450;

struct AudioClip {
	std::vector<float> original;  // interleaved
	std::vector<float> current;   // interleaved, effects applied
	int sample_rate = 0;
	int channels = 0;
	std::string source;
	std::vector<std::string> effects;
};

std::mutex g_clips_mutex;
std::map<std::string, std::shared_ptr<AudioClip>> g_clips;

void Log(const std::string& text) {
	std::printf("[ServerAudio] %s\n", text.c_str());
	std::fflush(stdout);
}

std::string NewId() {
	GUID guid{};
	if (FAILED(CoCreateGuid(&guid))) return {};
	wchar_t text[64] = {};
	if (StringFromGUID2(guid, text, 64) == 0) return {};
	const std::string id = Str::ToLowerAscii(Str::WideToUtf8(text));
	return Str::ReplaceAll(Str::ReplaceAll(id, "{", ""), "}", "");
}

std::filesystem::path ToPath(const std::string& utf8) {
	return std::filesystem::path(Str::Utf8ToWide(utf8));
}

template <typename T>
bool ParseNumber(std::string_view text, T* out) {
	const auto result =
	    std::from_chars(text.data(), text.data() + text.size(), *out);
	return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

// Interleaved samples are stored as a float WAV payload; rate/channels
// live in meta.txt because WavFloatCodec writes a fixed mono header.
bool PersistSamples(const std::string& id, const char* name,
                    const std::vector<float>& samples, std::string* error) {
	std::vector<std::uint8_t> bytes;
	if (!Codec::WavFloatCodec().Encode32(samples.data(), samples.size(),
	                                     bytes)) {
		*error = "failed to encode cache payload";
		return false;
	}
	return CacheManager::Instance().WritePersistentBinary(kCacheScope, id, name,
	                                                     bytes, error);
}

bool LoadSamples(const std::string& id, const char* name,
                 std::vector<float>* samples, std::string* error) {
	std::vector<std::uint8_t> bytes;
	if (!CacheManager::Instance().ReadPersistentBinary(kCacheScope, id, name,
	                                                  &bytes, error)) {
		return false;
	}
	if (!Codec::WavFloatCodec().Decode32(bytes.data(), bytes.size(),
	                                     *samples)) {
		*error = "cache payload is corrupt";
		return false;
	}
	return true;
}

bool PersistMeta(const std::string& id, const AudioClip& clip,
                 std::string* error) {
	const std::string text =
	    Str::BuildKeyValueLine("sample_rate", std::to_string(clip.sample_rate)) +
	    "\n" + Str::BuildKeyValueLine("channels", std::to_string(clip.channels)) +
	    "\n" + Str::BuildKeyValueLine("source", clip.source) + "\n" +
	    Str::BuildKeyValueLine("effects", Str::Join(clip.effects, ",")) + "\n";
	return CacheManager::Instance().WritePersistentText(kCacheScope, id,
	                                                   kMetaName, text, error);
}

bool PersistCurrent(const std::string& id, const AudioClip& clip,
                    std::string* error) {
	return PersistSamples(id, kCurrentName, clip.current, error) &&
	       PersistMeta(id, clip, error);
}

std::shared_ptr<AudioClip> RestoreFromCache(const std::string& id) {
	std::string meta, error;
	if (!CacheManager::Instance().ReadPersistentText(kCacheScope, id, kMetaName,
	                                                &meta, &error)) {
		return nullptr;
	}
	auto clip = std::make_shared<AudioClip>();
	for (const std::string& line : Str::SplitLines(meta)) {
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		const std::string key = Str::TrimAsciiWhitespaceCopy(line.substr(0, eq));
		const std::string value =
		    Str::TrimAsciiWhitespaceCopy(line.substr(eq + 1));
		if (key == "sample_rate") ParseNumber(std::string_view(value), &clip->sample_rate);
		if (key == "channels") ParseNumber(std::string_view(value), &clip->channels);
		if (key == "source") clip->source = value;
		if (key == "effects" && !value.empty()) clip->effects = Str::Split(value, ',');
	}
	if (clip->sample_rate <= 0 || clip->channels <= 0 ||
	    !LoadSamples(id, kOriginalName, &clip->original, &error) ||
	    !LoadSamples(id, kCurrentName, &clip->current, &error)) {
		return nullptr;
	}
	return clip;
}

// Caller must hold g_clips_mutex.
std::shared_ptr<AudioClip> FindClipLocked(const std::string& id) {
	const auto it = g_clips.find(id);
	if (it != g_clips.end()) return it->second;
	auto clip = RestoreFromCache(id);
	if (clip) g_clips[id] = clip;
	return clip;
}

size_t FrameCount(const AudioClip& clip) {
	return clip.current.size() / static_cast<size_t>(clip.channels);
}

std::string HandleLoad(const std::string& path) {
	if (path.empty()) return "ERR usage: load <path>";
	AudioCore::AudioSourceLoadOptions options;
	options.input_path = ToPath(path);
	options.target_channels = 0;  // keep the source channel layout
	AudioCore::AudioSourceBuffer buffer;
	std::string error;
	if (!AudioCore::AudioSourceLoader::Load(options, &buffer, &error)) {
		return "ERR " + error;
	}
	// Keep the source sample rate instead of the loader's default.
	if (buffer.original_sample_rate > 0 &&
	    buffer.original_sample_rate != buffer.sample_rate) {
		options.target_sample_rate = buffer.original_sample_rate;
		buffer = {};
		if (!AudioCore::AudioSourceLoader::Load(options, &buffer, &error)) {
			return "ERR " + error;
		}
	}
	if (buffer.samples.empty() || buffer.channels <= 0) {
		return "ERR decoded audio is empty";
	}

	const std::string id = NewId();
	if (id.empty()) return "ERR failed to create id";
	auto clip = std::make_shared<AudioClip>();
	clip->sample_rate = buffer.sample_rate;
	clip->channels = buffer.channels;
	clip->source = path;
	clip->original = std::move(buffer.samples);
	clip->current = clip->original;
	if (!PersistSamples(id, kOriginalName, clip->original, &error) ||
	    !PersistCurrent(id, *clip, &error)) {
		return "ERR cache: " + error;
	}
	{
		std::lock_guard lock(g_clips_mutex);
		g_clips[id] = clip;
	}
	Log("loaded " + path + " -> " + id);
	return "OK " + id + " " + std::to_string(clip->sample_rate) + " " +
	       std::to_string(clip->channels) + " " +
	       std::to_string(FrameCount(*clip));
}

std::string HandleApply(const std::vector<std::string>& args) {
	if (args.size() < 3) {
		return "ERR usage: apply <id> <effect> [node.param=value ...]";
	}
	const std::string& id = args[1];
	const std::string& effect = args[2];
	if (!FX::Customs::IsFxCustomPresetSupported(effect)) {
		return "ERR unknown effect " + effect + " (see: effects)";
	}

	FX::CustomEffectPackage package = FX::Customs::BuildFxCustomPreset(effect);
	for (size_t i = 3; i < args.size(); ++i) {
		const std::vector<std::string> kv = Str::Split(args[i], '=');
		const size_t dot = kv.empty() ? std::string::npos : kv[0].rfind('.');
		float value = 0.0f;
		if (kv.size() != 2 || dot == std::string::npos ||
		    !ParseNumber(std::string_view(kv[1]), &value)) {
			return "ERR bad parameter " + args[i] + " (expected node.param=value)";
		}
		if (!FX::InvokeCustomEffectParameter(&package, kv[0].substr(0, dot),
		                                     kv[0].substr(dot + 1), value)) {
			return "ERR effect " + effect + " has no node " + kv[0].substr(0, dot);
		}
	}

	std::lock_guard lock(g_clips_mutex);
	auto clip = FindClipLocked(id);
	if (!clip) return "ERR unknown id " + id;
	std::vector<float> output;
	FX::CustomEffectReport report;
	std::string error;
	if (!FX::RunCustomEffectPackageInterleaved(
	        package, static_cast<float>(clip->sample_rate), clip->current,
	        clip->channels, &output, &report, &error)) {
		return "ERR " + error;
	}
	clip->current = std::move(output);
	clip->effects.push_back(effect);
	if (!PersistCurrent(id, *clip, &error)) return "ERR cache: " + error;
	Log("applied " + effect + " to " + id);
	return "OK " + id + " " + effect + " peak=" + std::to_string(report.peak) +
	       " rms=" + std::to_string(report.rms);
}

std::string HandleInfo(const std::string& id) {
	std::lock_guard lock(g_clips_mutex);
	auto clip = FindClipLocked(id);
	if (!clip) return "ERR unknown id " + id;
	const size_t frames = FrameCount(*clip);
	return "OK " + id + " rate=" + std::to_string(clip->sample_rate) +
	       " channels=" + std::to_string(clip->channels) +
	       " frames=" + std::to_string(frames) + " seconds=" +
	       std::to_string(static_cast<double>(frames) / clip->sample_rate) +
	       " effects=" + Str::DefaultString(Str::Join(clip->effects, ","), "-") +
	       " source=" + clip->source;
}

std::string HandleReset(const std::string& id) {
	std::lock_guard lock(g_clips_mutex);
	auto clip = FindClipLocked(id);
	if (!clip) return "ERR unknown id " + id;
	clip->current = clip->original;
	clip->effects.clear();
	std::string error;
	if (!PersistCurrent(id, *clip, &error)) return "ERR cache: " + error;
	return "OK " + id;
}

bool EncodeForPath(const AudioClip& clip, const std::string& path,
                   std::vector<std::uint8_t>* bytes, std::string* error) {
	const std::string ext = Str::ToLowerAscii(ToPath(path).extension().string());
	const size_t frames = FrameCount(clip);
	const float* data = clip.current.data();
	if (ext == ".wav") {
		if (Codec::WavPcmCodec().Encode16(data, frames, *bytes, clip.sample_rate,
		                                  clip.channels)) {
			return true;
		}
		*error = "wav encode failed";
		return false;
	}
	if (ext == ".mp3") {
		if (Codec::Mp3LameCodec().EncodeInterleaved(data, frames, clip.sample_rate,
		                                            clip.channels, *bytes)) {
			return true;
		}
		*error = "mp3 encode failed";
		return false;
	}
	static const std::map<std::string, std::string> kFfmpegCodecs = {
	    {".flac", "flac"}, {".ogg", "libvorbis"}, {".opus", "libopus"},
	    {".m4a", "aac"},   {".aac", "aac"}};
	const auto codec = kFfmpegCodecs.find(ext);
	if (codec == kFfmpegCodecs.end()) {
		*error = "unsupported output format '" + ext +
		         "' (wav, mp3, flac, ogg, opus, m4a, aac)";
		return false;
	}
	return Codec::detail::EncodeInterleavedAudioBufferWithFfmpeg(
	    data, frames, clip.sample_rate, clip.channels, ext, codec->second, bytes,
	    error);
}

std::string HandleSave(const std::string& id, const std::string& path) {
	if (id.empty() || path.empty()) return "ERR usage: save <id> <path>";
	std::vector<std::uint8_t> bytes;
	std::string error;
	{
		std::lock_guard lock(g_clips_mutex);
		auto clip = FindClipLocked(id);
		if (!clip) return "ERR unknown id " + id;
		if (!EncodeForPath(*clip, path, &bytes, &error)) return "ERR " + error;
	}
	Codec::CodecFileStreamer file;
	if (!file.OpenWrite(path)) return "ERR cannot open " + path;
	const size_t written = file.WriteBytes(bytes.data(), bytes.size());
	file.Close();
	if (written != bytes.size()) return "ERR short write to " + path;
	Log("saved " + id + " -> " + path);
	return "OK " + path + " " + std::to_string(bytes.size());
}

std::string HandleDelete(const std::string& id) {
	if (id.empty()) return "ERR usage: delete <id>";
	{
		std::lock_guard lock(g_clips_mutex);
		g_clips.erase(id);
	}
	auto& persistent = CacheManager::Instance().Persistent();
	bool removed = false;
	for (const char* name : {kOriginalName, kCurrentName, kMetaName}) {
		std::string error;
		const auto entry = XER::Cache::MakeEntry(kCacheScope, id, name);
		if (persistent.Exists(entry) && persistent.Remove(entry, &error)) {
			removed = true;
		}
	}
	return removed ? "OK " + id : "ERR unknown id " + id;
}

std::string HandleList() {
	std::lock_guard lock(g_clips_mutex);
	std::vector<std::string> ids;
	for (const auto& [id, clip] : g_clips) ids.push_back(id);
	return "OK " + Str::Join(ids, ",");
}

// Rest of `line` after `count` space-separated words (keeps spaces in paths).
std::string RestAfterWords(const std::string& line, int count) {
	size_t pos = 0;
	for (int i = 0; i < count && pos != std::string::npos; ++i) {
		pos = line.find(' ', pos);
		if (pos != std::string::npos) pos = line.find_first_not_of(' ', pos);
	}
	return pos == std::string::npos
	           ? std::string()
	           : Str::TrimAsciiWhitespaceCopy(line.substr(pos));
}

std::string HandleCommand(const std::string& line, bool* quit) {
	std::vector<std::string> args;
	for (std::string& word : Str::Split(line, ' ')) {
		if (!word.empty()) args.push_back(std::move(word));
	}
	if (args.empty()) return "ERR empty command";
	const std::string command = Str::ToLowerAscii(args[0]);
	const std::string arg1 = args.size() > 1 ? args[1] : std::string();
	if (command == "load") return HandleLoad(RestAfterWords(line, 1));
	if (command == "effects") {
		return "OK " + Str::Join(FX::Customs::ListFxCustomPresetNames(), ",");
	}
	if (command == "apply") return HandleApply(args);
	if (command == "info") return HandleInfo(arg1);
	if (command == "reset") return HandleReset(arg1);
	if (command == "save") return HandleSave(arg1, RestAfterWords(line, 2));
	if (command == "delete") return HandleDelete(arg1);
	if (command == "list") return HandleList();
	if (command == "quit" || command == "exit") {
		*quit = true;
		return "OK bye";
	}
	return "ERR unknown command " + args[0] +
	       " (load, effects, apply, info, reset, save, delete, list, quit)";
}

bool SendLine(TcpClientSocket& client, const std::string& text) {
	const std::string line = text + "\n";
	size_t sent = 0;
	std::string error;
	while (sent < line.size()) {
		const int n = client.Write(
		    reinterpret_cast<const std::uint8_t*>(line.data()) + sent,
		    line.size() - sent, &error);
		if (n <= 0) return false;
		sent += static_cast<size_t>(n);
	}
	return true;
}

void ServeClient(std::unique_ptr<TcpClientSocket> client) {
	const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	SendLine(*client, "OK ServerAudio ready");
	std::string pending, error;
	std::uint8_t buffer[4096];
	bool quit = false;
	while (!quit) {
		const int n = client->Read(buffer, sizeof(buffer), &error);
		if (n <= 0) break;
		pending.append(reinterpret_cast<const char*>(buffer),
		               static_cast<size_t>(n));
		size_t newline;
		while (!quit && (newline = pending.find('\n')) != std::string::npos) {
			const std::string line =
			    Str::TrimAsciiWhitespaceCopy(pending.substr(0, newline));
			pending.erase(0, newline + 1);
			if (line.empty()) continue;
			if (!SendLine(*client, HandleCommand(line, &quit))) quit = true;
		}
	}
	client->Disconnect();
	if (SUCCEEDED(com)) CoUninitialize();
}

}  // namespace

int main(int argc, char** argv) {
	std::string bind = "127.0.0.1";
	std::uint16_t port = kDefaultPort;
	for (int i = 1; i < argc; i += 2) {
		const std::string flag = argv[i];
		const bool has_value = i + 1 < argc;
		if (flag == "--bind" && has_value) {
			bind = argv[i + 1];
		} else if (!(flag == "--port" && has_value &&
		             ParseNumber(std::string_view(argv[i + 1]), &port))) {
			std::fprintf(stderr,
			             "usage: ServerAudio.exe [--bind <address>] [--port <port>]\n");
			return 2;
		}
	}

	std::string error;
	auto& platform =
	    XER::Network::Socket::Platform::SocketPlatform::GetInstance();
	if (!platform.Initialize(&error)) {
		std::fprintf(stderr, "socket init failed: %s\n", error.c_str());
		return 1;
	}
	SocketServer server;
	if (!server.Listen(bind, port, SOMAXCONN, &error)) {
		std::fprintf(stderr, "listen %s:%u failed: %s\n", bind.c_str(),
		             static_cast<unsigned>(port), error.c_str());
		return 1;
	}
	Log("listening on " + bind + ":" + std::to_string(port) + ", cache " +
	    CacheManager::Instance().DirectoryFor(kCacheScope, "").string());

	while (server.IsListening()) {
		auto client = server.Accept(&error);
		if (!client) {
			Log("accept failed: " + error);
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}
		std::thread(ServeClient, std::move(client)).detach();
	}
	platform.Shutdown();
	return 0;
}
