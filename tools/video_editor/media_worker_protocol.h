// media_worker_protocol.h — shared wire format between the editor and
// its worker subprocesses (XERVideoLoader.exe today; the `--media-worker`
// self-spawn is the dev-build fallback). One JSON object per line on
// stdin/stdout:
//   req : {"id":N,"kind":"poster"|"peaks"|"ping"|"info"|
//          "sync_plan"|"fx_list"|"fx_validate"|"fx_audio"|
//          "fx_transition", ...}
//   rsp : {"id":N,"ok":true, ...payload...}
//       | {"id":N,"ok":false,"err":"<why>"}
// Poster payload: {"w":W,"h":H,"fmt":"bgra8","b64":"<pixels>"}
// Peaks payload : {"channels":C,"buckets":B,"spp":S,"data":[f,...]}
// Pong payload  : {"pong":true,"role":"<worker exe role>"}
// Info payload  : {"role":"<worker exe role>","serves":"<csv kinds>"}
//
// Engine-role kinds (XERSyncMedia / XERFxScript) carry structured
// bodies instead of path/time — see the SyncPlan*/Fx* structs below.
// Binary pixel payloads ride base64 (posters are capped at a 320 px
// long edge — ~300 KB/line worst case, cheap for a pipe). ping/info
// carry no `path` — they are liveness/discovery probes every worker
// role answers the same way, which lets the parent prove WHICH binary
// (dedicated exe vs self-spawn) replied.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "media_catalog.h"   // AudioPeaks

namespace video { class Frame; }

namespace XER::VideoEditor::MediaWorker {

// Request kinds understood by the workers. kImportPoster stays
// in-process — the row update lives in the parent's catalog DB.
// kPing/kInfo are role-agnostic liveness/discovery probes (no path):
// every worker exe answers them, including the role-stub engines.
enum class Kind {
    kPosterAt, kPeaks, kPing, kInfo,
    // XERSyncMedia: the whole audio-sync compute (peak decode +
    // envelope correlation) runs in the worker.
    kSyncPlan,
    // XERFxScript: script-fx host requests.
    kFxList, kFxValidate, kFxAudio, kFxTransition,
};

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

// Liveness/discovery replies. `role` identifies the worker binary
// (e.g. "XERVideoLoader", "media-worker" for the self-spawn fallback,
// "XERSyncMedia"...); `serves` is the csv kind list the worker handles.
std::string EncodePongReply(std::uint64_t id, const std::string& role);
std::string EncodeInfoReply(std::uint64_t id, const std::string& role,
                            const std::string& serves);
// Parses a pong/info reply envelope; role_out may be nullptr.
bool DecodePongReply(const std::string& line, std::uint64_t* id,
                     std::string* role_out);

// Reads the envelope fields any reply carries.
bool DecodeReplyEnvelope(const std::string& line, std::uint64_t* id,
                         bool* ok, std::string* err);

// ---- sync_plan (XERSyncMedia) --------------------------------------
//
// req : {"id":N,"kind":"sync_plan","ref":R,
//        "clips":[{"id","path","start","in","out","rate",
//                  "peaks"?{channels,buckets,spp,data}}]}
// rsp : {"id":N,"ok":true,"ref":"<clip id>","status":"<summary>",
//        "entries":[{"id","new_start","delta","conf","matched",
//                    "note"}]}
// `peaks` is the test/relay seam: when present the worker uses the
// supplied peaks instead of decoding `path` itself. `ref` is the
// index into `clips` of the sync anchor (-1 → worker's default pick).

struct SyncPlanClip {
    std::string clip_id;
    std::string path;            // UTF-8 media path
    double start = 0.0;          // current timeline seconds
    double in_point = 0.0;       // source in (seconds)
    double out_point = 0.0;      // source out (seconds)
    double rate = 1.0;           // playback rate
    bool has_peaks = false;
    AudioPeaks peaks;            // valid when has_peaks
};

struct SyncPlanRequestData {
    std::vector<SyncPlanClip> clips;
    int ref_index = -1;
};

struct SyncPlanResultEntry {
    std::string clip_id;
    double new_timeline_start = 0.0;
    double delta_seconds = 0.0;
    double confidence = 0.0;
    bool matched = false;
    std::string note;
};

struct SyncPlanResult {
    std::string reference_clip_id;
    std::vector<SyncPlanResultEntry> entries;
    std::string status;
};

std::string EncodeSyncPlanRequest(std::uint64_t id,
                                  const SyncPlanRequestData& data);
bool DecodeSyncPlanRequest(const std::string& line, std::uint64_t* id,
                           SyncPlanRequestData* out);
std::string EncodeSyncPlanReply(std::uint64_t id,
                                const SyncPlanResult& result);
bool DecodeSyncPlanReply(const std::string& line, std::uint64_t* id,
                         SyncPlanResult* out);

// ---- fx_* (XERFxScript) --------------------------------------------
//
// req : {"id":N,"kind":"fx_list","dir":"<fx dir>"}
//       {"id":N,"kind":"fx_validate","dir":"<fx dir>",
//        "src":"<js>" | "fx_id":"<id>"}
//       {"id":N,"kind":"fx_audio","dir":"<fx dir>","fx_id":"<id>",
//        "clip_id":"<id>","frames":F,"channels":C,"rate":R,"time":T,
//        "params":{"0":v,...},"pcm":"<b64 f32>"}
//       {"id":N,"kind":"fx_transition","dir":"<fx dir>",
//        "fx_id":"<id>","t":P}
// rsp : fx_list       → {"id","ok":true,"video":[desc...],
//                        "audio":[desc...],"transitions":[desc...]}
//                        (desc = {id,title,file,params:[{id,label,
//                         def,min,max}]})
//       fx_validate   → {"id","ok":true,"valid":B,"err":"..."}
//       fx_audio      → {"id","ok":true,"pcm":"<b64 f32>"}
//       fx_transition → {"id","ok":true,"w":W}
// `dir` points the worker at the scripts/fx catalog to load; it is
// required for fx_audio/fx_transition and for fx_validate by id.

struct FxParamItem {
    std::string id;
    std::string label;
    float def = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
};
struct FxListItem {
    std::string id;
    std::string title;
    std::string file;
    std::vector<FxParamItem> params;
};
struct FxListReply {
    std::vector<FxListItem> video;
    std::vector<FxListItem> audio;
    std::vector<FxListItem> transitions;
};

// Worker-side parse of any fx_* request into a flat field bag (unused
// fields stay default).
struct FxRequest {
    std::string dir;
    std::string fx_id;
    std::string source;
    std::string clip_id;
    double time_s = 0.0;
    double progress = 0.0;
    int frames = 0;
    int channels = 0;
    int sample_rate = 0;
    std::map<std::uint32_t, float> params;   // ordinal → value
    std::vector<float> pcm;                  // decoded from b64
};

bool DecodeFxRequest(const std::string& line, std::uint64_t* id,
                     FxRequest* out);

std::string EncodeFxListRequest(std::uint64_t id,
                                const std::string& dir);
std::string EncodeFxValidateRequest(std::uint64_t id,
                                    const std::string& dir,
                                    const std::string& fx_id_or_source,
                                    bool is_source);
std::string EncodeFxAudioRequest(
    std::uint64_t id, const std::string& dir,
    const std::string& fx_id, const std::string& clip_id,
    const float* pcm, int frames, int channels, int sample_rate,
    const std::map<std::uint32_t, float>& params, double time_s);
std::string EncodeFxTransitionRequest(std::uint64_t id,
                                      const std::string& dir,
                                      const std::string& fx_id,
                                      double progress);

std::string EncodeFxListReply(std::uint64_t id,
                              const FxListReply& list);
bool DecodeFxListReply(const std::string& line, std::uint64_t* id,
                       FxListReply* out);
std::string EncodeFxValidateReply(std::uint64_t id, bool valid,
                                  const std::string& error);
bool DecodeFxValidateReply(const std::string& line, std::uint64_t* id,
                           bool* valid, std::string* error);
std::string EncodeFxAudioReply(std::uint64_t id, const float* pcm,
                               std::size_t sample_count);
bool DecodeFxAudioReply(const std::string& line, std::uint64_t* id,
                        std::vector<float>* pcm);
std::string EncodeFxTransitionReply(std::uint64_t id, double weight);
bool DecodeFxTransitionReply(const std::string& line,
                             std::uint64_t* id, double* weight);

// ---- tiny base64 (self-contained — no dep on the nettest/openai
// codecs so the core lib links standalone) -------------------------
std::string B64Encode(const void* data, std::size_t size);
bool B64Decode(const std::string& text, std::vector<std::uint8_t>* out);

}  // namespace XER::VideoEditor::MediaWorker
