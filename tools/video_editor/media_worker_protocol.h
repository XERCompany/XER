// media_worker_protocol.h — shared wire format between the editor and
// its `--media-worker` subprocess. One JSON object per line on
// stdin/stdout:
//   req : {"id":N,"kind":"poster"|"peaks","path":"<utf8>","time":S}
//   rsp : {"id":N,"ok":true, ...payload...}
//       | {"id":N,"ok":false,"err":"<why>"}
// Poster payload: {"w":W,"h":H,"fmt":"bgra8","b64":"<pixels>"}
// Peaks payload : {"channels":C,"buckets":B,"spp":S,"data":[f,...]}
// Binary pixel payloads ride base64 (posters are capped at a 320 px
// long edge — ~300 KB/line worst case, cheap for a pipe).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "media_catalog.h"   // AudioPeaks

namespace video { class Frame; }

namespace XER::VideoEditor::MediaWorker {

// Request kinds understood by the worker. kImportPoster stays
// in-process — the row update lives in the parent's catalog DB.
enum class Kind { kPosterAt, kPeaks };

struct Request {
    std::uint64_t id = 0;
    Kind kind = Kind::kPosterAt;
    std::string path;      // UTF-8 media path (raw, worker canonicalizes)
    double seconds = 0.0;  // kPosterAt only
};

// Encodes `request` as one JSON line (no trailing newline).
std::string EncodeRequest(const Request& request);

// Parses one request line; false → malformed (worker ignores).
bool DecodeRequest(const std::string& line, Request* out);

// Poster reply builders/parsers (both directions share the schema).
std::string EncodePosterReply(std::uint64_t id, const video::Frame& frame);
bool DecodePosterReply(const std::string& line, std::uint64_t* id,
                       video::Frame* out);
std::string EncodePeaksReply(std::uint64_t id, const AudioPeaks& peaks);
bool DecodePeaksReply(const std::string& line, std::uint64_t* id,
                      AudioPeaks* out);
std::string EncodeErrorReply(std::uint64_t id, const std::string& err);

// Reads the envelope fields any reply carries.
bool DecodeReplyEnvelope(const std::string& line, std::uint64_t* id,
                         bool* ok, std::string* err);

// ---- tiny base64 (self-contained — no dep on the nettest/openai
// codecs so the core lib links standalone) -------------------------
std::string B64Encode(const void* data, std::size_t size);
bool B64Decode(const std::string& text, std::vector<std::uint8_t>* out);

}  // namespace XER::VideoEditor::MediaWorker
