// media_catalog.cc — see media_catalog.h for the contract. Follows
// node_graph_project_store.cc patterns: anonymous-namespace Value
// helpers, one Execute() per schema statement (SQLiteDatabase::Execute
// prepares a single statement only), all SQL values bound — never
// interpolated.
#include "media_catalog.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <json/json.h>

#include "audio/audio_core/audio_bit_depth_converter.h"
#include "clip_poster.h"
#include "content/video/video_info.h"
#include "io/still_image.h"
#include "frame.h"
#include "helper/env_utils.h"
#include "helper/path_utils.h"
#include "sql/content/value.h"
#include "sql/storage/sqlite_database.h"
#include "string_utils.h"                           // ToLowerAscii
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"   // AudioFrameInfo def
#include "wrapper/ffmpeg/ffmpeg_video_decoder.h"

namespace XER::VideoEditor {
namespace {

namespace Path = XER::Helper::Path;  // UTF-8 <-> fs::path helpers
namespace Video = XER::Content::Video;
namespace FFmpeg = XER::Bridge::FFmpeg;
using XER::SQL::Content::Blob;
using XER::SQL::Content::Value;
using XER::SQL::Storage::Row;
using XER::SQL::Storage::SQLiteDatabase;

bool Fail(std::string* error_out, const std::string& message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
    return false;
}

Value VStr(const std::string& s) { return Value{s}; }
Value VInt(std::int64_t v) { return Value{v}; }
Value VReal(double v) { return Value{v}; }
Value VBlob(const Blob& b) { return Value{b}; }
// SQL::Content::Value has no bool alternative — has_audio is an INTEGER
// column, so booleans bind as 0/1 int64.
Value VBool(bool b) { return Value{static_cast<std::int64_t>(b ? 1 : 0)}; }

std::string AsString(const Value& v) {
    return std::holds_alternative<std::string>(v) ? std::get<std::string>(v)
                                                  : std::string{};
}
std::int64_t AsInt(const Value& v) {
    if (std::holds_alternative<std::int64_t>(v)) {
        return std::get<std::int64_t>(v);
    }
    if (std::holds_alternative<double>(v)) {
        return static_cast<std::int64_t>(std::get<double>(v));
    }
    return 0;
}
double AsReal(const Value& v) {
    if (std::holds_alternative<double>(v)) {
        return std::get<double>(v);
    }
    if (std::holds_alternative<std::int64_t>(v)) {
        return static_cast<double>(std::get<std::int64_t>(v));
    }
    return 0.0;
}
bool AsBool(const Value& v) { return AsInt(v) != 0; }
Blob AsBlob(const Value& v) {
    return std::holds_alternative<Blob>(v) ? std::get<Blob>(v) : Blob{};
}

std::int64_t NowUnixSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// NOTE: SQLiteDatabase::Execute prepares a single statement — a multi-
// statement script would silently run only the first CREATE TABLE. Keep
// every schema statement separate (same rule as ProjectStore).
constexpr const char* kSchemaStatements[] = {
    R"sql(CREATE TABLE IF NOT EXISTS media (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL COLLATE NOCASE,
    duration REAL NOT NULL DEFAULT 0,
    width INTEGER NOT NULL DEFAULT 0,
    height INTEGER NOT NULL DEFAULT 0,
    fps REAL NOT NULL DEFAULT 0,
    video_codec TEXT NOT NULL DEFAULT '',
    audio_codec TEXT NOT NULL DEFAULT '',
    has_audio INTEGER NOT NULL DEFAULT 0,
    file_size INTEGER NOT NULL DEFAULT 0,
    mtime INTEGER NOT NULL DEFAULT 0,
    probe_json TEXT NOT NULL DEFAULT '',
    poster_path TEXT NOT NULL DEFAULT '',
    added_at INTEGER NOT NULL DEFAULT 0
))sql",
    // NOCASE path dedup: the column collation makes both `path = ?`
    // lookups and this unique index ASCII case-insensitive, matching
    // the Windows case-insensitive filesystem ("D:/a.MP4" ==
    // "d:/a.mp4"). Non-ASCII case edges are accepted — NOCASE folds
    // ASCII only.
    "CREATE UNIQUE INDEX IF NOT EXISTS media_path_uq ON media(path)",
    // Waveform peaks cache (Phase 2 spec §5): one row per canonical
    // media path; `mtime` invalidates the cached `data` blob when the
    // file is rewritten. `data` holds AudioPeaks::data as raw
    // little-endian IEEE-754 float32 — channels * buckets * 2 floats.
    R"sql(CREATE TABLE IF NOT EXISTS peaks (
    path TEXT PRIMARY KEY COLLATE NOCASE,
    mtime INTEGER NOT NULL DEFAULT 0,
    channels INTEGER NOT NULL DEFAULT 0,
    spp INTEGER NOT NULL DEFAULT 0,
    buckets INTEGER NOT NULL DEFAULT 0,
    data BLOB NOT NULL DEFAULT X''
))sql",
};

constexpr const char* kSelectColumns =
    "id, path, duration, width, height, fps, video_codec, audio_codec,"
    " has_audio, file_size, mtime, probe_json, poster_path, added_at";

// Canonical absolute UTF-8 form of a media path: NormalizeUtf8Path
// (legacy ANSI bytes -> UTF-8), FromUtf8 -> weakly_canonical (absolute,
// "." / ".." resolved; nonexistent tails stay normalized so ProbeVideo
// reports not-found), ToUtf8 back. weakly_canonical uses the
// error_code overload — a canonicalize failure falls back to absolute +
// lexical normalization rather than throwing.
bool CanonicalizeMediaPath(const std::string& utf8_path,
                           std::string* canon_out,
                           std::string* error_out) {
    const std::string normalized = Path::NormalizeUtf8Path(utf8_path);
    if (normalized.empty()) {
        return Fail(error_out, "empty media path");
    }
    std::error_code ec;
    std::filesystem::path canon =
        std::filesystem::weakly_canonical(Path::FromUtf8(normalized), ec);
    if (ec || canon.empty()) {
        canon = Path::FromUtf8(normalized);
        std::error_code aec;
        if (canon.is_relative()) {
            canon = std::filesystem::absolute(canon, aec);
        }
        if (aec) {
            return Fail(error_out,
                        "cannot resolve media path: " + utf8_path);
        }
        canon = canon.lexically_normal();
    }
    *canon_out = Path::ToUtf8(canon);
    return true;
}

// File mtime as unix seconds; 0 when the file is missing/unstattable.
// std::chrono::clock_cast is not implemented by libc++; converts
// through the relative offset between the file and system clocks.
std::int64_t FileMtimeUnix(const std::filesystem::path& fs_path) {
    std::error_code ec;
    const auto ftime = std::filesystem::last_write_time(fs_path, ec);
    if (ec) {
        return 0;
    }
    const auto sys =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - decltype(ftime)::clock::now() +
            std::chrono::system_clock::now());
    return std::chrono::duration_cast<std::chrono::seconds>(
               sys.time_since_epoch())
        .count();
}

// Serialized VideoInfo snapshot stored in probe_json — compact JSON,
// same writer settings as ProjectStore.
std::string ProbeJsonFor(const std::string& canon,
                         const Video::VideoInfo& info,
                         std::int64_t file_size) {
    Json::Value probe(Json::objectValue);
    probe["path"] = canon;
    probe["width"] = info.width;
    probe["height"] = info.height;
    probe["fps"] = info.fps;
    probe["duration_seconds"] = info.duration_seconds;
    probe["video_codec"] = info.video_codec;
    probe["audio_codec"] = info.audio_codec;
    probe["has_audio"] = info.has_audio;
    probe["file_size_bytes"] = static_cast<Json::Int64>(file_size);
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, probe);
}

CatalogEntry EntryFromRow(const Row& row) {
    CatalogEntry e;
    if (row.columns.size() < 14) {
        return e;
    }
    e.id = AsInt(row.columns[0]);
    e.path = AsString(row.columns[1]);
    e.duration_seconds = AsReal(row.columns[2]);
    e.width = static_cast<int>(AsInt(row.columns[3]));
    e.height = static_cast<int>(AsInt(row.columns[4]));
    e.fps = AsReal(row.columns[5]);
    e.video_codec = AsString(row.columns[6]);
    e.audio_codec = AsString(row.columns[7]);
    e.has_audio = AsBool(row.columns[8]);
    e.file_size_bytes = AsInt(row.columns[9]);
    e.mtime_unix = AsInt(row.columns[10]);
    e.probe_json = AsString(row.columns[11]);
    e.poster_path = AsString(row.columns[12]);
    e.added_at_unix = AsInt(row.columns[13]);
    return e;
}

// ------------------------------------------------------------------
// Audio peaks decode — the SAME pipeline AudioMixer runs: one
// FFmpeg::VideoDecoder with EnableAudioTap() BEFORE Open();
// NextFrame() pumps demux packets into the tap queue (its return is
// ignored — audio-only containers always return false yet MUST still
// be called), ReadAudioFrames() drains decoded AudioFrameInfo blocks
// until AudioDrained() reports EOF. The PCM sample-format handling
// mirrors audio_mixer.cc's (u8/s16/s32/flt/dbl, planar + interleaved)
// but keeps EVERY channel instead of downmixing to stereo.
// ------------------------------------------------------------------

constexpr std::int64_t kNoPts =
    (std::numeric_limits<std::int64_t>::min)();  // AV_NOPTS_VALUE
constexpr int kPeaksSamplesPerPeak = 256;
// Decode cap: at most the first 10 minutes of audio are scanned — a
// peaks miss is synchronous, so this bounds the worst-case cost on
// multi-hour files (documented on GetAudioPeaks).
constexpr double kMaxPeaksDecodeSeconds = 600.0;
// Pump-loop guards — same values as the mixer.
constexpr int kMaxPumpIterations = 20000;
constexpr int kReadBatch = 64;
// Timestamp holes larger than this are treated as corrupt — same
// bound as the mixer's kMaxGapFillSeconds.
constexpr double kMaxGapFillSeconds = 30.0;

// The raw -> float sample map is delegated to
// AudioCore::AudioBitDepthConverter (same math as audio_mixer.cc —
// u8 bias, int32 scale, double cast); ParsePcmLayout only maps the
// FFmpeg format name onto the audio-core enum.
using XER::Audio::Core::AudioBitDepth;
using XER::Audio::Core::AudioBitDepthConverter;

struct PcmLayout {
    AudioBitDepth depth = AudioBitDepth::BITS_16;
    int bytes_per_sample = 2;
    bool planar = false;
};

// Parses "u8"/"s16"/"s32"/"flt"/"dbl" plus the FFmpeg "...p" planar
// suffix — identical rules to audio_mixer.cc's ParsePcmLayout.
bool ParsePcmLayout(const FFmpeg::AudioFrameInfo& frame,
                    PcmLayout* out) {
    std::string fmt = XER::Helper::String::ToLowerAscii(
        frame.sample_format);
    bool planar = frame.planar;
    if (!fmt.empty() && fmt.back() == 'p') {
        planar = true;
        fmt.pop_back();
    }
    PcmLayout layout;
    layout.planar = planar;
    if (fmt == "u8") {
        layout.depth = AudioBitDepth::BITS_8;
    } else if (fmt == "s16") {
        layout.depth = AudioBitDepth::BITS_16;
    } else if (fmt == "s32") {
        layout.depth = AudioBitDepth::BITS_32;
    } else if (fmt == "flt") {
        layout.depth = AudioBitDepth::FLOAT_32;
    } else if (fmt == "dbl") {
        layout.depth = AudioBitDepth::FLOAT_64;
    } else {
        return false;
    }
    layout.bytes_per_sample = static_cast<int>(
        AudioBitDepthConverter::GetBytesPerSample(layout.depth));
    *out = layout;
    return true;
}

// One decoded block converted to float — planar keeps a vector per
// channel (the declared plane_sizes stride is honored), interleaved a
// single count*channels block. The peaks keep EVERY channel (no
// downmix), so planar converts each declared plane.
struct FloatBlock {
    bool planar = false;
    int channels = 0;
    std::vector<float> interleaved;
    std::vector<std::vector<float>> planes;

    float Sample(int frame_index, int channel) const {
        return planar
            ? planes[static_cast<std::size_t>(channel)]
                    [static_cast<std::size_t>(frame_index)]
            : interleaved[static_cast<std::size_t>(frame_index) *
                              static_cast<std::size_t>(channels) +
                          static_cast<std::size_t>(channel)];
    }
};

// Converts the (already payload-validated) frame to float through the
// audio-core converter. False only if the converter rejects a depth —
// unreachable for the formats ParsePcmLayout accepts.
bool ToFloatBlock(const FFmpeg::AudioFrameInfo& frame,
                  const PcmLayout& layout, FloatBlock* out) {
    const int count = frame.sample_count;
    const int channels = frame.channels;
    AudioBitDepthConverter converter;
    out->planar = layout.planar;
    out->channels = channels;
    if (layout.planar) {
        out->planes.resize(static_cast<std::size_t>(channels));
        std::size_t offset = 0;
        for (int c = 0; c < channels; ++c) {
            const std::uint8_t* plane = frame.data.data() + offset;
            offset += static_cast<std::size_t>(
                frame.plane_sizes[static_cast<std::size_t>(c)]);
            std::vector<float>& dst =
                out->planes[static_cast<std::size_t>(c)];
            dst.assign(static_cast<std::size_t>(count), 0.0f);
            if (!converter.ConvertBuffer(
                    plane, layout.depth, dst.data(),
                    AudioBitDepth::FLOAT_32,
                    static_cast<std::size_t>(count))) {
                return false;
            }
        }
        return true;
    }
    out->interleaved.assign(
        static_cast<std::size_t>(count) *
            static_cast<std::size_t>(channels), 0.0f);
    return converter.ConvertBuffer(
        frame.data.data(), layout.depth, out->interleaved.data(),
        AudioBitDepth::FLOAT_32, out->interleaved.size());
}

// Running per-channel min/max accumulation over the absolute sample
// timeline (sample 0 = first decoded block's media-time anchor).
struct PeakBuilder {
    int channels = 0;
    int rate = 0;
    double audio_tb = 0.0;      // s per pts tick (0 = untimestamped)
    double audio_origin = 0.0;  // GetAudioStartSeconds
    bool have_anchor = false;
    double anchor = 0.0;        // media seconds of absolute sample 0
    std::int64_t emitted = 0;   // samples folded into buckets so far
    std::int64_t cap_samples =
        std::numeric_limits<std::int64_t>::max();
    bool capped = false;        // 10-minute decode cap reached
    std::size_t buckets = 0;
    std::vector<std::vector<float>> mins;  // [channel][bucket]
    std::vector<std::vector<float>> maxs;
};

void EnsureBucket(PeakBuilder& b, std::size_t bucket) {
    while (b.buckets <= bucket) {
        for (int c = 0; c < b.channels; ++c) {
            b.mins[static_cast<std::size_t>(c)].push_back(
                std::numeric_limits<float>::max());
            b.maxs[static_cast<std::size_t>(c)].push_back(
                -std::numeric_limits<float>::max());
        }
        ++b.buckets;
    }
}

// Zero-fill for a timestamp hole: silence still tightens min/max —
// a gap must draw as flat, not as a hole in the waveform.
void EmitZeroRun(PeakBuilder& b, std::int64_t count) {
    const std::int64_t room = b.cap_samples - b.emitted;
    if (count > room) {
        count = room;
        b.capped = true;
    }
    if (count <= 0) {
        return;
    }
    const std::size_t first =
        static_cast<std::size_t>(b.emitted) / kPeaksSamplesPerPeak;
    const std::size_t last =
        static_cast<std::size_t>(b.emitted + count - 1) /
        kPeaksSamplesPerPeak;
    for (std::size_t bucket = first; bucket <= last; ++bucket) {
        EnsureBucket(b, bucket);
        for (int c = 0; c < b.channels; ++c) {
            float& mn = b.mins[static_cast<std::size_t>(c)][bucket];
            float& mx = b.maxs[static_cast<std::size_t>(c)][bucket];
            if (mn > 0.0f) mn = 0.0f;
            if (mx < 0.0f) mx = 0.0f;
        }
    }
    b.emitted += count;
}

void EmitSample(PeakBuilder& b, const FloatBlock& fb, int frame_index) {
    const std::size_t bucket =
        static_cast<std::size_t>(b.emitted) / kPeaksSamplesPerPeak;
    EnsureBucket(b, bucket);
    for (int c = 0; c < b.channels; ++c) {
        const float v = fb.Sample(frame_index, c);
        float& mn = b.mins[static_cast<std::size_t>(c)][bucket];
        float& mx = b.maxs[static_cast<std::size_t>(c)][bucket];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    ++b.emitted;
}

// Folds one decoded block into the peak timeline. Returns false on
// malformed input (unparseable format, truncated payload, absurd
// timestamp gap, mid-stream format change) — the caller then fails
// the whole peaks request rather than caching a half-decoded file.
bool ConsumeAudioBlock(PeakBuilder& b,
                       const FFmpeg::AudioFrameInfo& frame,
                       std::string* error_out) {
    const int count = frame.sample_count;
    const int channels = frame.channels;
    if (count <= 0 || channels <= 0 || frame.sample_rate <= 0 ||
        frame.data.empty()) {
        return Fail(error_out, "malformed audio frame");
    }
    // One rate/channel layout per file — a mid-stream change cannot be
    // represented in fixed-resolution buckets (same rule as the mixer).
    if (b.rate == 0) {
        b.rate = frame.sample_rate;
        b.channels = channels;
        b.cap_samples = static_cast<std::int64_t>(std::llround(
            kMaxPeaksDecodeSeconds * static_cast<double>(b.rate)));
        b.mins.assign(static_cast<std::size_t>(channels), {});
        b.maxs.assign(static_cast<std::size_t>(channels), {});
    } else if (frame.sample_rate != b.rate ||
               channels != b.channels) {
        return Fail(error_out,
                    "audio sample rate/channels changed mid-stream");
    }
    const double rate = static_cast<double>(b.rate);

    PcmLayout layout;
    if (!ParsePcmLayout(frame, &layout)) {
        return Fail(error_out, "unsupported audio sample format '" +
                               frame.sample_format + "'");
    }
    // Payload validation — same strictness as the mixer: planar needs
    // one sized plane per channel; interleaved needs count*ch*bps bytes.
    const int bps = layout.bytes_per_sample;
    if (layout.planar) {
        if (frame.plane_sizes.size() <
            static_cast<std::size_t>(channels)) {
            return Fail(error_out,
                        "planar audio frame missing plane sizes");
        }
        std::size_t need = 0;
        for (int c = 0; c < channels; ++c) {
            const std::size_t plane_bytes =
                static_cast<std::size_t>(count) *
                static_cast<std::size_t>(bps);
            if (static_cast<std::size_t>(
                    frame.plane_sizes[static_cast<std::size_t>(c)]) <
                plane_bytes) {
                return Fail(error_out,
                            "planar audio frame plane truncated");
            }
            need += plane_bytes;
        }
        if (frame.data.size() < need) {
            return Fail(error_out,
                        "planar audio frame payload truncated");
        }
    } else {
        const std::size_t need =
            static_cast<std::size_t>(count) *
            static_cast<std::size_t>(channels) *
            static_cast<std::size_t>(bps);
        if (frame.data.size() < need) {
            return Fail(error_out,
                        "interleaved audio frame payload truncated");
        }
    }

    // Convert the whole block to float once — every channel feeds the
    // per-channel peak accumulation below.
    FloatBlock fb;
    if (!ToFloatBlock(frame, layout, &fb)) {
        return Fail(error_out, "audio sample conversion failed");
    }

    // Block media start: best_effort_timestamp preferred, else pts,
    // both * the audio time base; missing timestamps continue from the
    // emitted end (first block anchors at GetAudioStartSeconds — the
    // AAC priming / stream-offset case). Same rules as the mixer.
    std::int64_t stamp = frame.best_effort_timestamp;
    if (stamp == kNoPts) {
        stamp = frame.pts;
    }
    double block_start;
    if (stamp != kNoPts && b.audio_tb > 0.0) {
        block_start = static_cast<double>(stamp) * b.audio_tb;
    } else {
        block_start = b.have_anchor
                          ? b.anchor +
                                static_cast<double>(b.emitted) / rate
                          : b.audio_origin;
    }
    if (!b.have_anchor) {
        b.anchor = block_start;
        b.have_anchor = true;
    }

    // Map onto the absolute sample timeline: forward gaps zero-fill
    // (bounded), backward overlaps drop the already-covered prefix.
    std::int64_t target = static_cast<std::int64_t>(
        std::llround((block_start - b.anchor) * rate));
    if (target < 0) {
        target = 0;
    }
    std::size_t skip = 0;
    if (target > b.emitted) {
        const double gap_seconds =
            static_cast<double>(target - b.emitted) / rate;
        if (gap_seconds > kMaxGapFillSeconds) {
            return Fail(error_out,
                        "excessive audio timestamp gap (>30 s)");
        }
        EmitZeroRun(b, target - b.emitted);
    } else if (target < b.emitted) {
        const std::int64_t drop = b.emitted - target;
        skip = static_cast<std::size_t>(std::min<std::int64_t>(
            drop, count));
    }

    for (std::size_t i = skip; i < static_cast<std::size_t>(count);
         ++i) {
        if (b.emitted >= b.cap_samples) {
            b.capped = true;
            break;
        }
        EmitSample(b, fb, static_cast<int>(i));
    }
    return true;
}

bool FinalizePeaks(const PeakBuilder& b, AudioPeaks* out) {
    if (b.channels <= 0 || b.emitted <= 0 || b.buckets == 0) {
        return false;
    }
    out->channels = b.channels;
    out->samples_per_peak = kPeaksSamplesPerPeak;
    out->buckets = b.buckets;
    out->data.assign(
        static_cast<std::size_t>(b.channels) * b.buckets * 2u, 0.0f);
    for (int c = 0; c < b.channels; ++c) {
        const std::size_t cc = static_cast<std::size_t>(c);
        for (std::size_t i = 0; i < b.buckets; ++i) {
            float mn = b.mins[cc][i];
            float mx = b.maxs[cc][i];
            if (!(mn <= mx)) {
                mn = 0.0f;   // untouched sentinel — defensive only:
                mx = 0.0f;   // gap/overlap handling covers all buckets
            }
            const std::size_t o = (cc * b.buckets + i) * 2u;
            out->data[o] = mn;
            out->data[o + 1u] = mx;
        }
    }
    return true;
}

// AudioPeaks::data <-> raw little-endian float32 blob (explicit LE —
// the cache must read back identically on any host).
Blob SerializePeaks(const AudioPeaks& peaks) {
    Blob blob(peaks.data.size() * sizeof(float));
    for (std::size_t i = 0; i < peaks.data.size(); ++i) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &peaks.data[i], sizeof(bits));
        blob[i * 4u + 0u] = static_cast<std::uint8_t>(bits & 0xffu);
        blob[i * 4u + 1u] =
            static_cast<std::uint8_t>((bits >> 8u) & 0xffu);
        blob[i * 4u + 2u] =
            static_cast<std::uint8_t>((bits >> 16u) & 0xffu);
        blob[i * 4u + 3u] =
            static_cast<std::uint8_t>((bits >> 24u) & 0xffu);
    }
    return blob;
}

// Poster-file writer shared by Import and ExtractPoster: still images
// (still_image.cc-decoded) write the PNG directly; FFmpeg containers
// go through ClipPoster::Extract. `poster_path` is the
// posters/<id>.png target — callers create the directory first.
// Best-effort contract: false on any decode/write failure, never
// throws.
bool WritePosterFile(const std::string& canon, bool is_still,
                     double seek_seconds,
                     const std::string& poster_path,
                     std::string* error_out) {
    if (is_still) {
        // The still path decodes it — write the PNG poster directly.
        const Io::Image still = Io::ReadImage(Path::FromUtf8(canon));
        return still.ok &&
               Io::WriteImage(Path::FromUtf8(poster_path),
                              still.bgra.data(), still.width,
                              still.height);
    }
    return ClipPoster::Extract(canon, seek_seconds, poster_path,
                               error_out);
}

bool DeserializePeaks(std::int64_t channels, std::int64_t spp,
                      std::int64_t buckets, const Blob& blob,
                      AudioPeaks* out) {
    if (channels <= 0 || spp <= 0 || buckets <= 0) {
        return false;
    }
    const std::size_t floats =
        static_cast<std::size_t>(channels) *
        static_cast<std::size_t>(buckets) * 2u;
    if (blob.size() != floats * sizeof(float)) {
        return false;
    }
    out->channels = static_cast<int>(channels);
    out->samples_per_peak = static_cast<int>(spp);
    out->buckets = static_cast<std::size_t>(buckets);
    out->data.resize(floats);
    for (std::size_t i = 0; i < floats; ++i) {
        const std::uint32_t bits =
            static_cast<std::uint32_t>(blob[i * 4u + 0u]) |
            (static_cast<std::uint32_t>(blob[i * 4u + 1u]) << 8u) |
            (static_cast<std::uint32_t>(blob[i * 4u + 2u]) << 16u) |
            (static_cast<std::uint32_t>(blob[i * 4u + 3u]) << 24u);
        std::memcpy(&out->data[i], &bits, sizeof(float));
    }
    return true;
}

}  // namespace

// Full-stream decode -> AudioPeaks. Synchronous; the caller owns the
// "10-minute cap" documentation. Exported for the --media-worker
// subprocess (media_worker_server.cc) — helpers above stay file-local.
bool DecodeAudioPeaks(const std::string& canon_path, AudioPeaks* out,
                      std::string* error_out) {
    FFmpeg::VideoDecoder decoder;
    decoder.EnableAudioTap(true);   // BEFORE Open — hard contract
    if (!decoder.Open(canon_path, error_out)) {
        return false;
    }
    if (!decoder.HasAudio()) {
        return Fail(error_out, "no audio stream: " + canon_path);
    }
    PeakBuilder b;
    b.audio_tb = decoder.GetAudioTimeBase();
    b.audio_origin = decoder.GetAudioStartSeconds();

    // Same pump pattern as the mixer's EnsureCoverage: NextFrame feeds
    // the tap queue (audio-only files return false but MUST still be
    // called), ReadAudioFrames drains decoded blocks. Completion is
    // governed by AudioDrained() / the decode cap, not NextFrame.
    std::vector<std::uint8_t> bgra;
    int width = 0;
    int height = 0;
    std::vector<FFmpeg::AudioFrameInfo> frames;
    int guard = 0;
    while (!decoder.AudioDrained() && !b.capped) {
        if (guard++ >= kMaxPumpIterations) {
            return Fail(error_out,
                        "audio decode starvation: " + canon_path);
        }
        decoder.NextFrame(bgra, width, height);
        frames.clear();
        decoder.ReadAudioFrames(&frames, kReadBatch);
        for (const FFmpeg::AudioFrameInfo& frame : frames) {
            if (!ConsumeAudioBlock(b, frame, error_out)) {
                return false;
            }
            if (b.capped) {
                break;
            }
        }
    }
    if (!FinalizePeaks(b, out)) {
        return Fail(error_out,
                    "audio stream yielded no samples: " + canon_path);
    }
    return true;
}

MediaCatalog::MediaCatalog(std::string utf8_db_path)
    : db_path_(utf8_db_path.empty() ? DefaultDbPath()
                                    : std::move(utf8_db_path)) {}

MediaCatalog::~MediaCatalog() { Close(); }

void MediaCatalog::Close() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    db_.reset();
}

bool MediaCatalog::IsOpen() const {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return db_ != nullptr;
}

std::string MediaCatalog::DefaultDbPath() {
    // UTF-8 safe env chain: LOCALAPPDATA arrives ANSI on non-EN Windows
    // -> NormalizeUtf8Path; XDG_DATA_HOME / HOME/.local/share cover
    // Linux/macOS; the temp dir is the last resort. Generic '/' separators
    // are appended — SQLite and Windows both accept them.
    std::string base =
        Path::NormalizeUtf8Path(XER::Helper::GetEnv("LOCALAPPDATA"));
    if (base.empty()) {
        base = Path::NormalizeUtf8Path(XER::Helper::GetEnv("XDG_DATA_HOME"));
    }
    if (base.empty()) {
        const std::string home =
            Path::NormalizeUtf8Path(XER::Helper::GetEnv("HOME"));
        if (!home.empty()) {
            base = home + "/.local/share";
        }
    }
    if (base.empty()) {
        std::error_code ec;
        base = Path::ToUtf8(std::filesystem::temp_directory_path(ec));
    }
    if (base.empty()) {
        base = ".";
    }
    return base + "/XER/VideoEditor/catalog.db";
}

bool MediaCatalog::Open(std::string* error_out) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    db_.reset();
    if (db_path_.empty()) {
        db_path_ = DefaultDbPath();
    }
    if (db_path_.empty()) {
        return Fail(error_out, "empty database path");
    }
    const std::filesystem::path parent =
        Path::FromUtf8(db_path_).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return Fail(error_out,
                        "cannot create database directory " +
                            Path::ToUtf8(parent) + ": " + ec.message());
        }
    }
    db_ = SQLiteDatabase::Open(db_path_, error_out);
    if (db_ == nullptr) {
        return false;
    }
    if (!EnsureSchema(error_out)) {
        db_.reset();
        return false;
    }
    return true;
}

// db_mutex_ held (called from Open only).
bool MediaCatalog::EnsureSchema(std::string* error_out) {
    for (const char* sql : kSchemaStatements) {
        if (!db_->Execute(sql, {}, error_out)) {
            return false;
        }
    }
    return true;
}

std::string MediaCatalog::PosterDir() const {
    return Path::ToUtf8(
        Path::FromUtf8(db_path_).parent_path() / "posters");
}

bool MediaCatalog::Import(const std::string& utf8_path,
                          std::int64_t* id_out,
                          bool extract_poster,
                          std::string* error_out) {
    // Canonicalize + probe run outside the DB lock — only the SQL
    // statements below serialize on db_mutex_.
    std::string canon;
    if (!CanonicalizeMediaPath(utf8_path, &canon, error_out)) {
        return false;
    }

    Video::VideoInfo info;
    bool is_still = false;
    if (!Video::ProbeVideo(canon, &info, nullptr)) {
        // Still images (EXR/DPX/TIFF/RAW/PSD/HDR/...) aren't FFmpeg
        // containers — fall back to the still-image path and
        // synthesize the probe. The decode itself is the accept test,
        // not an extension list: any file the still path can rasterize
        // is a valid still, and anything it can't was never media (the
        // *.* dialog filter must be able to pick everything).
        const Io::Image still = Io::ReadImage(Path::FromUtf8(canon));
        if (!still.ok || still.width <= 0 || still.height <= 0) {
            return Fail(error_out, "probe failed: " + canon);
        }
        info.valid = true;
        info.width = still.width;
        info.height = still.height;
        info.duration_seconds = kStillDefaultSeconds;
        info.video_codec = "image";
        info.has_audio = false;
        is_still = true;
    }
    // Classification rule (documented on CatalogEntry::HasVideo):
    //   w/h > 0          -> video-track media, stills included
    //   else has_audio   -> audio-track media (ProbeVideo reports
    //                       audio-only containers as w/h = 0; it also
    //                       reports a file whose video stream failed
    //                       to describe dimensions — w/h = 0 — the
    //                       same way, so a broken picture never
    //                       shadows usable audio)
    //   else             -> not media at all: reject now rather than
    //                       store a zero-stream row the bin/timeline
    //                       would draw as a broken audio entry.
    if (!is_still && !info.has_audio &&
        (info.width <= 0 || info.height <= 0)) {
        return Fail(error_out,
                    "no decodable audio/video streams: " + canon);
    }
    // FFmpeg-demuxed stills (PNG/JPEG/WebP/BMP/TGA/...) arrive as a
    // one-frame "video" whose whole length is a single frame — the
    // file IS the picture. Normalize to the same still default the
    // still-image path uses so they place as a 5 s clip instead of a ~40 ms
    // sliver. Audio-bearing files keep their real duration (and a
    // cover-art stream never reaches this branch — ProbeVideo already
    // classified the file as audio).
    if (!is_still && !info.has_audio && info.width > 0 &&
        info.height > 0 && info.duration_seconds > 0.0 &&
        info.duration_seconds <= kProbedStillMaxSeconds) {
        info.duration_seconds = kStillDefaultSeconds;
    }
    const std::filesystem::path canon_fs = Path::FromUtf8(canon);

    const std::int64_t mtime_unix = FileMtimeUnix(canon_fs);

    // ProbeVideo already fills file_size_bytes; fall back to a direct
    // stat when it could not (e.g. permission quirks on the probe path).
    std::int64_t file_size = info.file_size_bytes;
    if (file_size <= 0) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(canon_fs, ec);
        if (!ec) {
            file_size = static_cast<std::int64_t>(size);
        }
    }

    const std::string probe_json = ProbeJsonFor(canon, info, file_size);
    const std::int64_t now_unix = NowUnixSeconds();
    std::int64_t id = 0;
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!IsOpenLocked()) {
            return Fail(error_out, "catalog is not open");
        }
        // Upsert by canonical path: refresh the probe fields on
        // conflict, keep added_at (and poster_path until re-extraction
        // below).
        if (!db_->Execute(
                "UPDATE media SET duration = ?, width = ?, height = ?,"
                " fps = ?, video_codec = ?, audio_codec = ?,"
                " has_audio = ?, file_size = ?, mtime = ?,"
                " probe_json = ? WHERE path = ?",
                {VReal(info.duration_seconds), VInt(info.width),
                 VInt(info.height), VReal(info.fps),
                 VStr(info.video_codec), VStr(info.audio_codec),
                 VBool(info.has_audio), VInt(file_size),
                 VInt(mtime_unix), VStr(probe_json), VStr(canon)},
                error_out)) {
            return false;
        }
        if (db_->Changes() == 0) {
            if (!db_->Execute(
                    "INSERT INTO media(path, duration, width, height,"
                    " fps, video_codec, audio_codec, has_audio,"
                    " file_size, mtime, probe_json, added_at)"
                    " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    {VStr(canon), VReal(info.duration_seconds),
                     VInt(info.width), VInt(info.height), VReal(info.fps),
                     VStr(info.video_codec), VStr(info.audio_codec),
                     VBool(info.has_audio), VInt(file_size),
                     VInt(mtime_unix), VStr(probe_json),
                     VInt(now_unix)},
                    error_out)) {
                return false;
            }
        }

        std::vector<Row> id_rows;
        if (!db_->Query("SELECT id FROM media WHERE path = ?",
                        {VStr(canon)}, id_rows, error_out) ||
            id_rows.empty() || id_rows.front().columns.empty()) {
            return Fail(error_out,
                        "catalog row missing after upsert: " + canon);
        }
        id = AsInt(id_rows.front().columns.front());
        if (id <= 0) {
            return Fail(error_out,
                        "catalog row has invalid id: " + canon);
        }
    }

    // Poster extraction is best-effort and non-fatal: the row already
    // upserted, and poster_path keeps its previous/empty value on
    // failure. Only files with video get posters (ClipPoster rejects
    // audio-only anyway; the width>0 gate just skips the attempt).
    // Runs OUTSIDE the DB lock — callers wanting it off the UI thread
    // pass extract_poster=false and enqueue ExtractPoster on
    // media_prep instead.
    if (extract_poster && info.width > 0) {
        const double seek = info.duration_seconds > 0.0
                                ? info.duration_seconds * 0.5
                                : 0.0;
        const std::string poster_dir = PosterDir();
        {
            std::error_code ec;
            std::filesystem::create_directories(
                Path::FromUtf8(poster_dir), ec);
        }
        const std::string poster_path =
            poster_dir + "/" + std::to_string(id) + ".png";
        std::string poster_error;
        if (WritePosterFile(canon, is_still, seek, poster_path,
                            &poster_error)) {
            std::lock_guard<std::mutex> lock(db_mutex_);
            // Failure here is also non-fatal — the poster file exists
            // but the path simply will not be recorded.
            if (IsOpenLocked()) {
                db_->Execute(
                    "UPDATE media SET poster_path = ? WHERE id = ?",
                    {VStr(poster_path), VInt(id)}, nullptr);
            }
        }
    }

    if (id_out != nullptr) {
        *id_out = id;
    }
    return true;
}

bool MediaCatalog::FindByPath(const std::string& utf8_path,
                              CatalogEntry* out,
                              std::string* error_out) const {
    if (out == nullptr) {
        return Fail(error_out, "null catalog out-param");
    }
    std::string canon;
    if (!CanonicalizeMediaPath(utf8_path, &canon, error_out)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!IsOpenLocked()) {
        return Fail(error_out, "catalog is not open");
    }
    std::vector<Row> rows;
    // `path = ?` compares under the column's COLLATE NOCASE.
    if (!db_->Query(
            std::string("SELECT ") + kSelectColumns +
                " FROM media WHERE path = ?",
            {VStr(canon)}, rows, error_out)) {
        return false;
    }
    if (rows.empty()) {
        return Fail(error_out, "media not found: " + canon);
    }
    *out = EntryFromRow(rows.front());
    return true;
}

bool MediaCatalog::FindById(std::int64_t id,
                            CatalogEntry* out,
                            std::string* error_out) const {
    if (out == nullptr) {
        return Fail(error_out, "null catalog out-param");
    }
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!IsOpenLocked()) {
        return Fail(error_out, "catalog is not open");
    }
    std::vector<Row> rows;
    if (!db_->Query(
            std::string("SELECT ") + kSelectColumns +
                " FROM media WHERE id = ?",
            {VInt(id)}, rows, error_out)) {
        return false;
    }
    if (rows.empty()) {
        return Fail(error_out, "media not found: id " + std::to_string(id));
    }
    *out = EntryFromRow(rows.front());
    return true;
}

std::vector<CatalogEntry> MediaCatalog::List(std::string* error_out) const {
    std::vector<CatalogEntry> out;
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (!IsOpenLocked()) {
        Fail(error_out, "catalog is not open");
        return out;
    }
    std::vector<Row> rows;
    if (!db_->Query(
            std::string("SELECT ") + kSelectColumns +
                " FROM media ORDER BY id",
            {}, rows, error_out)) {
        return {};
    }
    out.reserve(rows.size());
    for (const auto& row : rows) {
        out.push_back(EntryFromRow(row));
    }
    return out;
}

bool MediaCatalog::Remove(std::int64_t id,
                          bool remove_poster_file,
                          std::string* error_out) {
    std::string poster_path;
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!IsOpenLocked()) {
            return Fail(error_out, "catalog is not open");
        }
        // Read poster_path first so the file can be removed after the row
        // is gone (the path is not recoverable once the row is deleted).
        std::vector<Row> rows;
        if (!db_->Query("SELECT poster_path FROM media WHERE id = ?",
                        {VInt(id)}, rows, error_out)) {
            return false;
        }
        if (!rows.empty() && !rows.front().columns.empty()) {
            poster_path = AsString(rows.front().columns.front());
        }
        if (!db_->Execute("DELETE FROM media WHERE id = ?",
                          {VInt(id)}, error_out)) {
            return false;
        }
    }
    if (remove_poster_file && !poster_path.empty()) {
        // Best-effort: a missing/locked poster file is not an error.
        std::error_code ec;
        std::filesystem::remove(Path::FromUtf8(poster_path), ec);
    }
    return true;
}

bool MediaCatalog::GetAudioPeaks(const std::string& utf8_path,
                                 AudioPeaks* out,
                                 std::string* error_out) {
    if (out == nullptr) {
        return Fail(error_out, "null peaks out-param");
    }
    std::string canon;
    if (!CanonicalizeMediaPath(utf8_path, &canon, error_out)) {
        return false;
    }
    const std::filesystem::path canon_fs = Path::FromUtf8(canon);
    const std::int64_t mtime_unix = FileMtimeUnix(canon_fs);

    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!IsOpenLocked()) {
            return Fail(error_out, "catalog is not open");
        }
        {
            std::error_code ec;
            if (ec || !std::filesystem::exists(canon_fs, ec)) {
                // A cached row for a vanished file is stale — drop it
                // (best-effort) so the table tracks the filesystem.
                db_->Execute("DELETE FROM peaks WHERE path = ?",
                             {VStr(canon)}, nullptr);
                return Fail(error_out,
                            "media file not found: " + canon);
            }
        }

        // Cache hit: a row with the CURRENT mtime and a blob that
        // round-trips. A stale-mtime or corrupt row falls through to
        // re-decode.
        std::vector<Row> rows;
        if (!db_->Query(
                "SELECT mtime, channels, spp, buckets, data"
                " FROM peaks WHERE path = ?",
                {VStr(canon)}, rows, error_out)) {
            return false;
        }
        if (!rows.empty() && rows.front().columns.size() >= 5u &&
            AsInt(rows.front().columns[0]) == mtime_unix) {
            const auto& cols = rows.front().columns;
            AudioPeaks cached;
            if (DeserializePeaks(AsInt(cols[1]), AsInt(cols[2]),
                                 AsInt(cols[3]), AsBlob(cols[4]),
                                 &cached)) {
                *out = std::move(cached);
                return true;
            }
        }
    }

    // Cache miss / stale / corrupt: decode (10-minute cap — see the
    // header doc) OUTSIDE the DB lock so worker decodes never stall UI
    // catalog queries. Decode failures are NOT cached.
    AudioPeaks computed;
    if (!DecodeAudioPeaks(canon, &computed, error_out)) {
        return false;
    }
    const Blob blob = SerializePeaks(computed);
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!IsOpenLocked()) {
            // Catalog closed mid-decode — the peaks are still valid;
            // just skip persisting them.
            *out = std::move(computed);
            return true;
        }
        if (!db_->Execute(
                "INSERT OR REPLACE INTO peaks(path, mtime, channels,"
                " spp, buckets, data) VALUES(?, ?, ?, ?, ?, ?)",
                {VStr(canon), VInt(mtime_unix), VInt(computed.channels),
                 VInt(computed.samples_per_peak),
                 VInt(static_cast<std::int64_t>(computed.buckets)),
                 VBlob(blob)},
                error_out)) {
            return false;
        }
    }
    *out = std::move(computed);
    return true;
}

bool MediaCatalog::ExtractPoster(const std::string& utf8_path,
                                 std::string* error_out) {
    std::string canon;
    if (!CanonicalizeMediaPath(utf8_path, &canon, error_out)) {
        return false;
    }

    // Row lookup under the DB lock; decode + file write outside it.
    std::int64_t id = 0;
    double duration = 0.0;
    int width = 0;
    std::string video_codec;
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (!IsOpenLocked()) {
            return Fail(error_out, "catalog is not open");
        }
        std::vector<Row> rows;
        if (!db_->Query(
                "SELECT id, duration, width, video_codec"
                " FROM media WHERE path = ?",
                {VStr(canon)}, rows, error_out)) {
            return false;
        }
        if (rows.empty() || rows.front().columns.size() < 4u) {
            return Fail(error_out,
                        "media not in catalog (import first): " + canon);
        }
        const auto& cols = rows.front().columns;
        id = AsInt(cols[0]);
        duration = AsReal(cols[1]);
        width = static_cast<int>(AsInt(cols[2]));
        video_codec = AsString(cols[3]);
    }
    if (id <= 0) {
        return Fail(error_out, "catalog row has invalid id: " + canon);
    }
    if (width <= 0) {
        return Fail(error_out, "no video stream: " + canon);
    }

    const std::string poster_dir = PosterDir();
    {
        std::error_code ec;
        std::filesystem::create_directories(Path::FromUtf8(poster_dir),
                                            ec);
    }
    const std::string poster_path =
        poster_dir + "/" + std::to_string(id) + ".png";
    const double seek = duration > 0.0 ? duration * 0.5 : 0.0;
    // Import() tags still-image files with video_codec = "image".
    const bool is_still = video_codec == "image";
    if (!WritePosterFile(canon, is_still, seek, poster_path,
                         error_out)) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (IsOpenLocked()) {
            db_->Execute("UPDATE media SET poster_path = ? WHERE id = ?",
                         {VStr(poster_path), VInt(id)}, nullptr);
        }
    }
    return true;
}

bool MediaCatalog::GetPosterAt(const std::string& utf8_path,
                               double seconds, video::Frame* frame_out,
                               std::string* error_out) {
    std::string canon;
    if (!CanonicalizeMediaPath(utf8_path, &canon, error_out)) {
        return false;
    }
    return ClipPoster::GetPosterAt(canon, seconds, frame_out, error_out);
}

}  // namespace XER::VideoEditor
