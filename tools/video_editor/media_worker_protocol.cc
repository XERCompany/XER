// media_worker_protocol.cc — see media_worker_protocol.h for the wire
// contract. JsonCpp is already a core-lib dep (project_store uses it).
#include "media_worker_protocol.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <json/json.h>

#include "frame.h"

namespace XER::VideoEditor::MediaWorker {
namespace {

const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string KindName(Kind k) {
    switch (k) {
        case Kind::kPeaks:        return "peaks";
        case Kind::kPing:         return "ping";
        case Kind::kInfo:         return "info";
        case Kind::kSyncPlan:     return "sync_plan";
        case Kind::kFxList:       return "fx_list";
        case Kind::kFxValidate:   return "fx_validate";
        case Kind::kFxAudio:      return "fx_audio";
        case Kind::kFxTransition: return "fx_transition";
        default:                  return "poster";
    }
}

// Writes `root` as one compact JSON line (no trailing newline).
std::string WriteJsonLine(const Json::Value& root) {
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, root);
}

// Peaks payloads appear in the "peaks" reply AND embedded per-clip in
// sync_plan requests (the injection seam) — one codec, both spots.
Json::Value PeaksToJson(const AudioPeaks& peaks) {
    Json::Value root(Json::objectValue);
    root["channels"] = peaks.channels;
    root["buckets"] = Json::UInt64(peaks.buckets);
    root["spp"] = peaks.samples_per_peak;
    Json::Value data(Json::arrayValue);
    for (const float f : peaks.data) {
        data.append(f);
    }
    root["data"] = data;
    return root;
}

bool PeaksFromJson(const Json::Value& root, AudioPeaks* out) {
    if (out == nullptr || !root.isObject()) return false;
    const int channels = root.get("channels", 0).asInt();
    const auto buckets =
        static_cast<std::size_t>(root.get("buckets", 0).asUInt64());
    const Json::Value& data = root["data"];
    if (channels <= 0 || buckets == 0 || !data.isArray() ||
        data.size() != channels * static_cast<int>(buckets) * 2) {
        return false;
    }
    out->channels = channels;
    out->buckets = buckets;
    out->samples_per_peak = root.get("spp", 256).asInt();
    out->data.resize(data.size());
    for (Json::ArrayIndex i = 0; i < data.size(); ++i) {
        out->data[i] = data[i].asFloat();
    }
    return true;
}

// Parses `line` into a Json::Value (must be an object).
bool ParseJsonLine(const std::string& line, Json::Value* out) {
    std::string errs;
    Json::CharReaderBuilder r;
    std::istringstream in(line);
    return Json::parseFromStream(r, in, out, &errs) &&
           out->isObject();
}

Json::Value FxParamsToJson(
    const std::vector<FxParamItem>& params) {
    Json::Value arr(Json::arrayValue);
    for (const auto& p : params) {
        Json::Value item(Json::objectValue);
        item["id"] = p.id;
        item["label"] = p.label;
        item["def"] = p.def;
        item["min"] = p.min;
        item["max"] = p.max;
        arr.append(item);
    }
    return arr;
}

Json::Value FxDescsToJson(const std::vector<FxListItem>& descs) {
    Json::Value arr(Json::arrayValue);
    for (const auto& d : descs) {
        Json::Value item(Json::objectValue);
        item["id"] = d.id;
        item["title"] = d.title;
        item["file"] = d.file;
        item["params"] = FxParamsToJson(d.params);
        arr.append(item);
    }
    return arr;
}

void FxDescsFromJson(const Json::Value& arr,
                     std::vector<FxListItem>* out) {
    if (out == nullptr || !arr.isArray()) return;
    for (const auto& e : arr) {
        FxListItem item;
        item.id = e.get("id", "").asString();
        item.title = e.get("title", item.id).asString();
        item.file = e.get("file", "").asString();
        const Json::Value& params = e["params"];
        if (params.isArray()) {
            for (const auto& p : params) {
                FxParamItem def;
                def.id = p.get("id", "").asString();
                def.label = p.get("label", def.id).asString();
                def.def = static_cast<float>(
                    p.get("def", 0.0).asDouble());
                def.min = static_cast<float>(
                    p.get("min", 0.0).asDouble());
                def.max = static_cast<float>(
                    p.get("max", 1.0).asDouble());
                if (!def.id.empty()) item.params.push_back(def);
            }
        }
        if (!item.id.empty()) out->push_back(std::move(item));
    }
}

}  // namespace

std::string B64Encode(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t b0 = p[i];
        const std::uint32_t b1 = i + 1 < size ? p[i + 1] : 0;
        const std::uint32_t b2 = i + 2 < size ? p[i + 2] : 0;
        out.push_back(kB64[b0 >> 2]);
        out.push_back(kB64[((b0 & 3) << 4) | (b1 >> 4)]);
        out.push_back(i + 1 < size ? kB64[((b1 & 15) << 2) | (b2 >> 6)]
                                 : '=');
        out.push_back(i + 2 < size ? kB64[b2 & 63] : '=');
    }
    return out;
}

bool B64Decode(const std::string& text,
               std::vector<std::uint8_t>* out) {
    if (out == nullptr || text.size() % 4 != 0) return false;
    static std::array<std::int8_t, 256> table = [] {
        std::array<std::int8_t, 256> t{};
        t.fill(-1);
        for (int i = 0; i < 64; ++i) {
            t[static_cast<std::uint8_t>(kB64[i])] =
                static_cast<std::int8_t>(i);
        }
        return t;
    }();
    out->clear();
    out->reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        int v[4];
        for (int j = 0; j < 4; ++j) {
            const char c = text[i + j];
            v[j] = c == '=' ? -2 : table[static_cast<std::uint8_t>(c)];
            if (v[j] == -1) return false;
            if (v[j] == -2 && i + j < text.size() - 2) return false;
        }
        const std::uint32_t b0 =
            static_cast<std::uint32_t>(v[0] < 0 ? 0 : v[0]);
        const std::uint32_t b1 =
            static_cast<std::uint32_t>(v[1] < 0 ? 0 : v[1]);
        const std::uint32_t b2 =
            static_cast<std::uint32_t>(v[2] < 0 ? 0 : v[2]);
        const std::uint32_t b3 =
            static_cast<std::uint32_t>(v[3] < 0 ? 0 : v[3]);
        out->push_back(
            static_cast<std::uint8_t>((b0 << 2) | (b1 >> 4)));
        if (v[2] >= 0) {
            out->push_back(
                static_cast<std::uint8_t>((b1 << 4) | (b2 >> 2)));
        }
        if (v[3] >= 0) {
            out->push_back(
                static_cast<std::uint8_t>((b2 << 6) | b3));
        }
    }
    return true;
}

std::string EncodeRequest(const Request& request) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(request.id);
    root["kind"] = KindName(request.kind);
    root["path"] = request.path;
    root["time"] = request.seconds;
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, root);
}

bool DecodeRequest(const std::string& line, Request* out) {
    if (out == nullptr) return false;
    Json::Value root;
    std::string errs;
    Json::CharReaderBuilder r;
    std::istringstream in(line);
    if (!Json::parseFromStream(r, in, &root, &errs) ||
        !root.isObject()) {
        return false;
    }
    out->id = root.get("id", 0).asUInt64();
    const std::string kind = root.get("kind", "").asString();
    if (kind == "peaks") {
        out->kind = Kind::kPeaks;
    } else if (kind == "ping") {
        out->kind = Kind::kPing;
    } else if (kind == "info") {
        out->kind = Kind::kInfo;
    } else if (kind == "sync_plan") {
        out->kind = Kind::kSyncPlan;
    } else if (kind == "fx_list") {
        out->kind = Kind::kFxList;
    } else if (kind == "fx_validate") {
        out->kind = Kind::kFxValidate;
    } else if (kind == "fx_audio") {
        out->kind = Kind::kFxAudio;
    } else if (kind == "fx_transition") {
        out->kind = Kind::kFxTransition;
    } else {
        out->kind = Kind::kPosterAt;
    }
    out->path = root.get("path", "").asString();
    out->seconds = root.get("time", 0.0).asDouble();
    // Only the media-decode kinds require a path — liveness probes and
    // the engine-role requests carry structured bodies instead.
    return !out->path.empty() ||
           (out->kind != Kind::kPosterAt && out->kind != Kind::kPeaks);
}

std::string EncodePosterReply(std::uint64_t id,
                              const video::Frame& frame) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["ok"] = true;
    root["w"] = frame.Width();
    root["h"] = frame.Height();
    root["fmt"] = "bgra8";
    root["b64"] = B64Encode(frame.Data(), frame.DataSize());
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, root);
}

bool DecodePosterReply(const std::string& line, std::uint64_t* id,
                       video::Frame* out) {
    std::uint64_t rid = 0;
    bool ok = false;
    std::string err;
    if (!DecodeReplyEnvelope(line, &rid, &ok, &err) || !ok) {
        return false;
    }
    Json::Value root;
    std::string errs;
    Json::CharReaderBuilder r;
    std::istringstream in(line);
    if (!Json::parseFromStream(r, in, &root, &errs)) return false;
    const int w = root.get("w", 0).asInt();
    const int h = root.get("h", 0).asInt();
    if (w <= 0 || h <= 0 ||
        root.get("fmt", "").asString() != "bgra8") {
        return false;
    }
    std::vector<std::uint8_t> pixels;
    if (!B64Decode(root.get("b64", "").asString(), &pixels) ||
        pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return false;
    }
    if (out == nullptr || !out->Allocate(w, h, video::PixelFormat::BGRA8)) {
        return false;
    }
    std::memcpy(out->Data(), pixels.data(), pixels.size());
    if (id != nullptr) *id = rid;
    return true;
}

std::string EncodePeaksReply(std::uint64_t id,
                             const AudioPeaks& peaks) {
    Json::Value root = PeaksToJson(peaks);
    root["id"] = Json::UInt64(id);
    root["ok"] = true;
    return WriteJsonLine(root);
}

bool DecodePeaksReply(const std::string& line, std::uint64_t* id,
                      AudioPeaks* out) {
    std::uint64_t rid = 0;
    bool ok = false;
    std::string err;
    if (!DecodeReplyEnvelope(line, &rid, &ok, &err) || !ok) {
        return false;
    }
    Json::Value root;
    if (!ParseJsonLine(line, &root) ||
        !PeaksFromJson(root, out)) {
        return false;
    }
    if (id != nullptr) *id = rid;
    return true;
}

std::string EncodeErrorReply(std::uint64_t id,
                             const std::string& err) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["ok"] = false;
    root["err"] = err;
    return WriteJsonLine(root);
}

std::string EncodePongReply(std::uint64_t id,
                            const std::string& role) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["ok"] = true;
    root["pong"] = true;
    root["role"] = role;
    return WriteJsonLine(root);
}

std::string EncodeInfoReply(std::uint64_t id,
                            const std::string& role,
                            const std::string& serves) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["ok"] = true;
    root["role"] = role;
    root["serves"] = serves;
    return WriteJsonLine(root);
}

bool DecodePongReply(const std::string& line, std::uint64_t* id,
                     std::string* role_out) {
    std::uint64_t rid = 0;
    bool ok = false;
    std::string err;
    if (!DecodeReplyEnvelope(line, &rid, &ok, &err) || !ok) {
        return false;
    }
    Json::Value root;
    std::string errs;
    Json::CharReaderBuilder r;
    std::istringstream in(line);
    if (!Json::parseFromStream(r, in, &root, &errs)) return false;
    if (id != nullptr) *id = rid;
    if (role_out != nullptr) {
        *role_out = root.get("role", "").asString();
    }
    return true;
}

bool DecodeReplyEnvelope(const std::string& line, std::uint64_t* id,
                         bool* ok, std::string* err) {
    Json::Value root;
    std::string errs;
    Json::CharReaderBuilder r;
    std::istringstream in(line);
    if (!Json::parseFromStream(r, in, &root, &errs) ||
        !root.isObject()) {
        return false;
    }
    if (id != nullptr) *id = root.get("id", 0).asUInt64();
    if (ok != nullptr) *ok = root.get("ok", false).asBool();
    if (err != nullptr) *err = root.get("err", "").asString();
    return true;
}

// ---- sync_plan (XERSyncMedia) --------------------------------------

std::string EncodeSyncPlanRequest(
    std::uint64_t id, const SyncPlanRequestData& data) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["kind"] = "sync_plan";
    root["ref"] = data.ref_index;
    Json::Value clips(Json::arrayValue);
    for (const auto& c : data.clips) {
        Json::Value clip(Json::objectValue);
        clip["id"] = c.clip_id;
        clip["path"] = c.path;
        clip["start"] = c.start;
        clip["in"] = c.in_point;
        clip["out"] = c.out_point;
        clip["rate"] = c.rate;
        if (c.has_peaks) {
            clip["peaks"] = PeaksToJson(c.peaks);
        }
        clips.append(clip);
    }
    root["clips"] = clips;
    return WriteJsonLine(root);
}

bool DecodeSyncPlanRequest(const std::string& line, std::uint64_t* id,
                           SyncPlanRequestData* out) {
    if (out == nullptr) return false;
    Json::Value root;
    if (!ParseJsonLine(line, &root)) return false;
    if (id != nullptr) *id = root.get("id", 0).asUInt64();
    out->ref_index = root.get("ref", -1).asInt();
    const Json::Value& clips = root["clips"];
    if (!clips.isArray()) return false;
    out->clips.clear();
    for (const auto& c : clips) {
        SyncPlanClip clip;
        clip.clip_id = c.get("id", "").asString();
        clip.path = c.get("path", "").asString();
        clip.start = c.get("start", 0.0).asDouble();
        clip.in_point = c.get("in", 0.0).asDouble();
        clip.out_point = c.get("out", 0.0).asDouble();
        clip.rate = c.get("rate", 1.0).asDouble();
        const Json::Value& peaks = c["peaks"];
        if (peaks.isObject()) {
            clip.has_peaks = PeaksFromJson(peaks, &clip.peaks);
        }
        out->clips.push_back(std::move(clip));
    }
    return !out->clips.empty();
}

std::string EncodeSyncPlanReply(std::uint64_t id,
                                const SyncPlanResult& result) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["ok"] = true;
    root["ref"] = result.reference_clip_id;
    root["status"] = result.status;
    Json::Value entries(Json::arrayValue);
    for (const auto& e : result.entries) {
        Json::Value item(Json::objectValue);
        item["id"] = e.clip_id;
        item["new_start"] = e.new_timeline_start;
        item["delta"] = e.delta_seconds;
        item["conf"] = e.confidence;
        item["matched"] = e.matched;
        item["note"] = e.note;
        entries.append(item);
    }
    root["entries"] = entries;
    return WriteJsonLine(root);
}

bool DecodeSyncPlanReply(const std::string& line, std::uint64_t* id,
                         SyncPlanResult* out) {
    if (out == nullptr) return false;
    std::uint64_t rid = 0;
    bool ok = false;
    std::string err;
    if (!DecodeReplyEnvelope(line, &rid, &ok, &err) || !ok) {
        return false;   // caller reads the error via DecodeReplyEnvelope
    }
    Json::Value root;
    if (!ParseJsonLine(line, &root)) return false;
    out->reference_clip_id = root.get("ref", "").asString();
    out->status = root.get("status", "").asString();
    const Json::Value& entries = root["entries"];
    if (!entries.isArray()) return false;
    out->entries.clear();
    for (const auto& e : entries) {
        SyncPlanResultEntry entry;
        entry.clip_id = e.get("id", "").asString();
        entry.new_timeline_start = e.get("new_start", 0.0).asDouble();
        entry.delta_seconds = e.get("delta", 0.0).asDouble();
        entry.confidence = e.get("conf", 0.0).asDouble();
        entry.matched = e.get("matched", false).asBool();
        entry.note = e.get("note", "").asString();
        out->entries.push_back(std::move(entry));
    }
    if (id != nullptr) *id = rid;
    return true;
}

// ---- fx_* (XERFxScript) --------------------------------------------

bool DecodeFxRequest(const std::string& line, std::uint64_t* id,
                     FxRequest* out) {
    if (out == nullptr) return false;
    Json::Value root;
    if (!ParseJsonLine(line, &root)) return false;
    if (id != nullptr) *id = root.get("id", 0).asUInt64();
    out->dir = root.get("dir", "").asString();
    out->fx_id = root.get("fx_id", "").asString();
    out->source = root.get("src", "").asString();
    out->clip_id = root.get("clip_id", "").asString();
    out->time_s = root.get("time", 0.0).asDouble();
    out->progress = root.get("t", 0.0).asDouble();
    out->frames = root.get("frames", 0).asInt();
    out->channels = root.get("channels", 0).asInt();
    out->sample_rate = root.get("rate", 0).asInt();
    const Json::Value& params = root["params"];
    if (params.isObject()) {
        for (const auto& key : params.getMemberNames()) {
            const auto ordinal =
                static_cast<std::uint32_t>(
                    std::strtoul(key.c_str(), nullptr, 10));
            out->params[ordinal] =
                static_cast<float>(params[key].asDouble());
        }
    }
    const Json::Value& pcm = root["pcm"];
    if (pcm.isString()) {
        std::vector<std::uint8_t> bytes;
        if (!B64Decode(pcm.asString(), &bytes) ||
            bytes.size() % sizeof(float) != 0) {
            return false;
        }
        out->pcm.resize(bytes.size() / sizeof(float));
        std::memcpy(out->pcm.data(), bytes.data(), bytes.size());
    }
    return true;
}

std::string EncodeFxListRequest(std::uint64_t id,
                                const std::string& dir) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["kind"] = "fx_list";
    root["dir"] = dir;
    return WriteJsonLine(root);
}

std::string EncodeFxValidateRequest(std::uint64_t id,
                                    const std::string& dir,
                                    const std::string& fx_id_or_source,
                                    bool is_source) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["kind"] = "fx_validate";
    root["dir"] = dir;
    root[is_source ? "src" : "fx_id"] = fx_id_or_source;
    return WriteJsonLine(root);
}

std::string EncodeFxAudioRequest(
    std::uint64_t id, const std::string& dir,
    const std::string& fx_id, const std::string& clip_id,
    const float* pcm, int frames, int channels, int sample_rate,
    const std::map<std::uint32_t, float>& params, double time_s) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["kind"] = "fx_audio";
    root["dir"] = dir;
    root["fx_id"] = fx_id;
    root["clip_id"] = clip_id;
    root["frames"] = frames;
    root["channels"] = channels;
    root["rate"] = sample_rate;
    root["time"] = time_s;
    Json::Value p(Json::objectValue);
    for (const auto& [ordinal, value] : params) {
        p[std::to_string(ordinal)] = value;
    }
    root["params"] = p;
    if (pcm != nullptr && frames > 0 && channels > 0) {
        root["pcm"] = B64Encode(
            pcm, static_cast<std::size_t>(frames) * channels *
                     sizeof(float));
    }
    return WriteJsonLine(root);
}

std::string EncodeFxTransitionRequest(std::uint64_t id,
                                      const std::string& dir,
                                      const std::string& fx_id,
                                      double progress) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["kind"] = "fx_transition";
    root["dir"] = dir;
    root["fx_id"] = fx_id;
    root["t"] = progress;
    return WriteJsonLine(root);
}

std::string EncodeFxListReply(std::uint64_t id,
                              const FxListReply& list) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["ok"] = true;
    root["video"] = FxDescsToJson(list.video);
    root["audio"] = FxDescsToJson(list.audio);
    root["transitions"] = FxDescsToJson(list.transitions);
    return WriteJsonLine(root);
}

bool DecodeFxListReply(const std::string& line, std::uint64_t* id,
                       FxListReply* out) {
    if (out == nullptr) return false;
    std::uint64_t rid = 0;
    bool ok = false;
    std::string err;
    if (!DecodeReplyEnvelope(line, &rid, &ok, &err) || !ok) {
        return false;
    }
    Json::Value root;
    if (!ParseJsonLine(line, &root)) return false;
    FxDescsFromJson(root["video"], &out->video);
    FxDescsFromJson(root["audio"], &out->audio);
    FxDescsFromJson(root["transitions"], &out->transitions);
    if (id != nullptr) *id = rid;
    return true;
}

std::string EncodeFxValidateReply(std::uint64_t id, bool valid,
                                  const std::string& error) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["ok"] = true;
    root["valid"] = valid;
    root["err"] = error;
    return WriteJsonLine(root);
}

bool DecodeFxValidateReply(const std::string& line, std::uint64_t* id,
                           bool* valid, std::string* error) {
    std::uint64_t rid = 0;
    bool ok = false;
    std::string err;
    if (!DecodeReplyEnvelope(line, &rid, &ok, &err) || !ok) {
        return false;
    }
    Json::Value root;
    if (!ParseJsonLine(line, &root)) return false;
    if (valid != nullptr) {
        *valid = root.get("valid", false).asBool();
    }
    if (error != nullptr) {
        *error = root.get("err", "").asString();
    }
    if (id != nullptr) *id = rid;
    return true;
}

std::string EncodeFxAudioReply(std::uint64_t id, const float* pcm,
                               std::size_t sample_count) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["ok"] = true;
    root["pcm"] =
        B64Encode(pcm, sample_count * sizeof(float));
    return WriteJsonLine(root);
}

bool DecodeFxAudioReply(const std::string& line, std::uint64_t* id,
                        std::vector<float>* pcm) {
    if (pcm == nullptr) return false;
    std::uint64_t rid = 0;
    bool ok = false;
    std::string err;
    if (!DecodeReplyEnvelope(line, &rid, &ok, &err) || !ok) {
        return false;
    }
    Json::Value root;
    if (!ParseJsonLine(line, &root)) return false;
    std::vector<std::uint8_t> bytes;
    if (!B64Decode(root.get("pcm", "").asString(), &bytes) ||
        bytes.size() % sizeof(float) != 0) {
        return false;
    }
    pcm->resize(bytes.size() / sizeof(float));
    std::memcpy(pcm->data(), bytes.data(), bytes.size());
    if (id != nullptr) *id = rid;
    return true;
}

std::string EncodeFxTransitionReply(std::uint64_t id, double weight) {
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["ok"] = true;
    root["w"] = weight;
    return WriteJsonLine(root);
}

bool DecodeFxTransitionReply(const std::string& line,
                             std::uint64_t* id, double* weight) {
    if (weight == nullptr) return false;
    std::uint64_t rid = 0;
    bool ok = false;
    std::string err;
    if (!DecodeReplyEnvelope(line, &rid, &ok, &err) || !ok) {
        return false;
    }
    Json::Value root;
    if (!ParseJsonLine(line, &root) || !root["w"].isNumeric()) {
        return false;
    }
    *weight = root["w"].asDouble();
    if (id != nullptr) *id = rid;
    return true;
}

}  // namespace XER::VideoEditor::MediaWorker
