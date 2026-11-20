// media_worker_protocol.cc — see media_worker_protocol.h for the wire
// contract. JsonCpp is already a core-lib dep (project_store uses it).
#include "media_worker_protocol.h"

#include <array>
#include <cstring>
#include <sstream>

#include <json/json.h>

#include "frame.h"

namespace XER::VideoEditor::MediaWorker {
namespace {

const char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string KindName(Kind k) {
    return k == Kind::kPeaks ? "peaks" : "poster";
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
    out->kind = kind == "peaks" ? Kind::kPeaks : Kind::kPosterAt;
    out->path = root.get("path", "").asString();
    out->seconds = root.get("time", 0.0).asDouble();
    return !out->path.empty();
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
    Json::Value root(Json::objectValue);
    root["id"] = Json::UInt64(id);
    root["ok"] = true;
    root["channels"] = peaks.channels;
    root["buckets"] = Json::UInt64(peaks.buckets);
    root["spp"] = peaks.samples_per_peak;
    Json::Value data(Json::arrayValue);
    for (const float f : peaks.data) {
        data.append(f);
    }
    root["data"] = data;
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, root);
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
    std::string errs;
    Json::CharReaderBuilder r;
    std::istringstream in(line);
    if (!Json::parseFromStream(r, in, &root, &errs)) return false;
    const int channels = root.get("channels", 0).asInt();
    const auto buckets =
        static_cast<std::size_t>(root.get("buckets", 0).asUInt64());
    const Json::Value& data = root["data"];
    if (channels <= 0 || buckets == 0 || !data.isArray() ||
        data.size() != channels * static_cast<int>(buckets) * 2) {
        return false;
    }
    if (out == nullptr) return false;
    out->channels = channels;
    out->buckets = buckets;
    out->samples_per_peak = root.get("spp", 256).asInt();
    out->data.resize(data.size());
    for (Json::ArrayIndex i = 0; i < data.size(); ++i) {
        out->data[i] = data[i].asFloat();
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
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return Json::writeString(w, root);
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

}  // namespace XER::VideoEditor::MediaWorker
