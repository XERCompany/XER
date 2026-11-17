// audio_mixer.cc — see audio_mixer.h for the contract. All decode work
// goes through IMediaDecoder (FFmpeg bridge in production, fakes in
// tests); the only FFmpeg type touched is AudioFrameInfo (full def via
// ffmpeg_engine_bridge.h).
#include "audio_mixer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <vector>

#include "generated_source.h"                  // IsGeneratedSource
#include "string_utils.h"                      // ToLowerAscii
#include "vst/vst3_chain.h"                    // Vst3Chain
#include "vst/vst3_factory.h"                  // InstantiateVst3Plugin
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"  // AudioFrameInfo def

namespace XER::VideoEditor {
namespace {

namespace FFmpeg = XER::Bridge::FFmpeg;
namespace Str = XER::Helper::String;

constexpr std::int64_t kNoPts =
    (std::numeric_limits<std::int64_t>::min)();  // AV_NOPTS_VALUE
// Sanity cap on NextFrame+ReadAudioFrames iterations per chunk —
// protects against decoders that never report AudioDrained (sparse-PTS
// / broken-timestamp pathologies). Real clips need a handful.
constexpr int kMaxPumpIterations = 20000;
// Blocks decoded per ReadAudioFrames call (brief-mandated batch).
constexpr int kReadBatch = 64;
// Seek lead-in at session open — land the decoder this far before the
// first needed media time so forward decode stays short (Seek is
// keyframe-backward only). Mirrors the compositor's 0.25 s.
constexpr double kSeekLeadSeconds = 0.25;
constexpr double kPi = 3.14159265358979323846;
// Window margins (seconds of media time):
//   kCoverageMargin — pump until pcm_media_end >= needed_end + margin
//     (covers linear-interpolation headroom + timestamp rounding).
//   kTrimMargin — keep this much audio BEFORE the earliest needed
//     source time so interpolation's left neighbor is always stored.
constexpr double kCoverageMargin = 0.10;
constexpr double kTrimMargin = 0.10;
// Adjacency tolerance for transition endpoint validation — same value
// as the timeline model's internal kEpsilon / compositor's.
constexpr double kAdjacencyEpsilon = 1e-6;
// Cap on zero-fill for a timestamp gap — a larger hole means corrupt
// timestamps; failing the clip beats allocating unbounded silence.
constexpr double kMaxGapFillSeconds = 30.0;

// ------------------------------------------------------------------
// Session state — one per eligible clip; decoder opens lazily.
// ------------------------------------------------------------------

// A validated transition endpoint reference stored on a session:
// `is_from` selects the weight role (from -> 1-w, to -> w).
struct TransitionRef {
    const Transition* transition = nullptr;
    double cut = 0.0;      // ClipEndSeconds(from_clip) — window center
    bool is_from = false;
};

struct AudioClipSession {
    const Clip* clip = nullptr;
    MediaDecoderPtr decoder;
    bool open_attempted = false;
    bool open_ok = false;
    bool dead = false;        // skipped: open/no-audio/bad-format -> silence
    bool audio_done = false;  // AudioDrained() reached (EOF)
    bool closed = false;      // decoder Close()d after ext_end passed
    bool has_pcm = false;     // window ever received samples
    double ext_start = 0.0;   // contribution window (incl. handles), s
    double ext_end = 0.0;
    double rate = 1.0;        // clip.playback_rate (guarded finite > 0)
    double pan_l = 1.0;       // equal-power stereo balance gains
    double pan_r = 1.0;
    double track_gain_lin = 1.0;   // pow(10, track.gain_db / 20)
    // Per-chunk effective linear gain: track_gain_lin *
    // 10^(EffectiveGainDbAt/20) — GainDb keyframes evaluate once per
    // chunk at its clip-local start.
    double mix_gain_lin = 1.0;
    // Nominal clip bounds + fades (fades never extend into handles).
    double clip_start = 0.0;
    double clip_end = 0.0;
    double fade_in = 0.0;
    double fade_out = 0.0;
    // Decoder-side state (captured at Open).
    double audio_tb = 0.0;       // GetAudioTimeBase() — s per pts tick
    double audio_origin = 0.0;   // GetAudioStartSeconds() — pts-less anchor
    double src_hi =
        std::numeric_limits<double>::infinity();  // src_media upper clamp
    // Stream format (from the first decoded block — constant per file).
    int src_rate = 0;
    // Media-time-indexed stereo float window @src_rate: pcm[2*j],
    // pcm[2*j+1] are the L/R samples whose media time is
    // pcm_media_start + j/src_rate. Gaps are stored as zeros, so the
    // window is always contiguous over [pcm_media_start, pcm_media_end).
    std::deque<float> pcm;
    double pcm_media_start = 0.0;
    double pcm_media_end = 0.0;
    std::vector<TransitionRef> transitions;
    // Per-clip plugin chain (clip.audio_fx) — null when the list is
    // empty. Nodes instantiate lazily on the first processed chunk;
    // Sync() rebuilds only when the fx signature changes.
    std::unique_ptr<Vst::Vst3Chain> fx_chain;
};

// Timeline tau -> source media time:
//   in_point + (tau - timeline_start) * playback_rate
// Handles are timeline time, so transition-handle regions map
// rate-scaled like the nominal range — same rule as the compositor.
double SourceTimeAt(const AudioClipSession& session, double tau) {
    return session.clip->in_point +
           (tau - session.clip->timeline_start) * session.rate;
}

// ------------------------------------------------------------------
// Sample-format conversion — AudioFrameInfo -> stereo float @src_rate.
// ------------------------------------------------------------------

enum class PcmKind { kU8, kS16, kS32, kFlt, kDbl };

struct PcmLayout {
    PcmKind kind = PcmKind::kS16;
    int bytes_per_sample = 2;
    bool planar = false;
};

// Parses "u8"/"s16"/"s32"/"flt"/"dbl" plus the FFmpeg "…p" planar
// suffix (the `planar` flag is honored too — either marks planar).
// Returns false on unknown formats.
bool ParsePcmLayout(const FFmpeg::AudioFrameInfo& frame,
                    PcmLayout* out) {
    std::string fmt = Str::ToLowerAscii(frame.sample_format);
    bool planar = frame.planar;
    if (!fmt.empty() && fmt.back() == 'p') {
        planar = true;
        fmt.pop_back();
    }
    PcmLayout layout;
    layout.planar = planar;
    if (fmt == "u8") {
        layout.kind = PcmKind::kU8;
        layout.bytes_per_sample = 1;
    } else if (fmt == "s16") {
        layout.kind = PcmKind::kS16;
        layout.bytes_per_sample = 2;
    } else if (fmt == "s32") {
        layout.kind = PcmKind::kS32;
        layout.bytes_per_sample = 4;
    } else if (fmt == "flt") {
        layout.kind = PcmKind::kFlt;
        layout.bytes_per_sample = 4;
    } else if (fmt == "dbl") {
        layout.kind = PcmKind::kDbl;
        layout.bytes_per_sample = 8;
    } else {
        return false;
    }
    *out = layout;
    return true;
}

float ReadPcmSample(PcmKind kind, const std::uint8_t* p) {
    switch (kind) {
        case PcmKind::kU8:
            return (static_cast<float>(*p) - 128.0f) / 128.0f;
        case PcmKind::kS16: {
            std::int16_t v;
            std::memcpy(&v, p, sizeof(v));
            return static_cast<float>(v) / 32768.0f;
        }
        case PcmKind::kS32: {
            std::int32_t v;
            std::memcpy(&v, p, sizeof(v));
            return static_cast<float>(v) / 2147483648.0f;
        }
        case PcmKind::kFlt: {
            float v;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }
        case PcmKind::kDbl: {
            double v;
            std::memcpy(&v, p, sizeof(v));
            return static_cast<float>(v);
        }
    }
    return 0.0f;
}

// Downmix channel `c` of frame `i` to [-1,1] float. channels==1
// duplicates ch0 to both outputs; >=2 uses ch0= L, ch1=R (first two —
// for 5.1 layouts FFmpeg orders FL,FR first, acceptable for v1).
// Returns false when the channel index is unavailable.
inline bool ChannelValue(const FFmpeg::AudioFrameInfo& frame,
                         const PcmLayout& layout, int frame_index,
                         int channel, float* out) {
    const int bps = layout.bytes_per_sample;
    const std::uint8_t* base = frame.data.data();
    if (layout.planar) {
        if (channel >= static_cast<int>(frame.plane_sizes.size())) {
            return false;
        }
        std::size_t offset = 0;
        for (int c = 0; c < channel; ++c) {
            offset += static_cast<std::size_t>(frame.plane_sizes[c]);
        }
        const std::size_t idx = offset +
            static_cast<std::size_t>(frame_index) * bps;
        if (idx + static_cast<std::size_t>(bps) > frame.data.size()) {
            return false;
        }
        *out = ReadPcmSample(layout.kind, base + idx);
        return true;
    }
    const std::size_t idx =
        (static_cast<std::size_t>(frame_index) * frame.channels +
         channel) *
            static_cast<std::size_t>(bps);
    if (idx + static_cast<std::size_t>(bps) > frame.data.size()) {
        return false;
    }
    *out = ReadPcmSample(layout.kind, base + idx);
    return true;
}

// ------------------------------------------------------------------
// Window append — media-time gap/overlap handling (sparse-PTS rule:
// gaps become zeros, never shifted audio; priming/overlap rule:
// overlapping prefixes are dropped).
// ------------------------------------------------------------------

// Merges `frames` stereo-float samples (interleaved L,R, `count`
// frames) into the session window so frame j of the block lands at
// media time block_start + j/src_rate. Returns false on malformed
// input (caller marks the session dead).
bool AppendBlock(AudioClipSession& session,
                 const FFmpeg::AudioFrameInfo& frame, double block_start,
                 std::string* error_out) {
    const int count = frame.sample_count;
    const int channels = frame.channels;
    if (count <= 0 || channels <= 0 || frame.sample_rate <= 0 ||
        frame.data.empty()) {
        if (error_out != nullptr) {
            *error_out = "malformed audio frame";
        }
        return false;
    }
    // Rate consistency: the window is indexed at ONE source rate —
    // a mid-stream rate change cannot be represented.
    if (session.src_rate == 0) {
        session.src_rate = frame.sample_rate;
    } else if (frame.sample_rate != session.src_rate) {
        if (error_out != nullptr) {
            *error_out = "audio sample rate changed mid-stream";
        }
        return false;
    }
    const double rate = static_cast<double>(session.src_rate);

    PcmLayout layout;
    if (!ParsePcmLayout(frame, &layout)) {
        if (error_out != nullptr) {
            *error_out = "unsupported audio sample format '" +
                         frame.sample_format + "'";
        }
        return false;
    }
    // Payload validation (encoder-style strictness): planar needs one
    // sized plane per channel; interleaved needs count*ch*bps bytes.
    const int bps = layout.bytes_per_sample;
    if (layout.planar) {
        if (frame.plane_sizes.size() <
                static_cast<std::size_t>(channels)) {
            if (error_out != nullptr) {
                *error_out = "planar audio frame missing plane sizes";
            }
            return false;
        }
        std::size_t need = 0;
        for (int c = 0; c < channels; ++c) {
            const std::size_t plane_bytes =
                static_cast<std::size_t>(count) *
                static_cast<std::size_t>(bps);
            if (static_cast<std::size_t>(
                    frame.plane_sizes[static_cast<std::size_t>(c)]) <
                plane_bytes) {
                if (error_out != nullptr) {
                    *error_out = "planar audio frame plane truncated";
                }
                return false;
            }
            need += static_cast<std::size_t>(
                frame.plane_sizes[static_cast<std::size_t>(c)]);
        }
        if (frame.data.size() < need) {
            if (error_out != nullptr) {
                *error_out = "planar audio frame payload truncated";
            }
            return false;
        }
    } else {
        const std::size_t need =
            static_cast<std::size_t>(count) *
            static_cast<std::size_t>(channels) *
            static_cast<std::size_t>(bps);
        if (frame.data.size() < need) {
            if (error_out != nullptr) {
                *error_out = "interleaved audio frame payload truncated";
            }
            return false;
        }
    }

    // Convert to stereo float. Mono duplicates ch0; >=2 takes ch0/ch1.
    std::vector<float> stereo(
        static_cast<std::size_t>(count) * 2u);
    for (int i = 0; i < count; ++i) {
        float l = 0.0f;
        float r = 0.0f;
        if (!ChannelValue(frame, layout, i, 0, &l)) {
            if (error_out != nullptr) {
                *error_out = "audio channel read out of bounds";
            }
            return false;
        }
        if (channels == 1) {
            r = l;
        } else if (!ChannelValue(frame, layout, i, 1, &r)) {
            if (error_out != nullptr) {
                *error_out = "audio channel read out of bounds";
            }
            return false;
        }
        stereo[static_cast<std::size_t>(i) * 2u] = l;
        stereo[static_cast<std::size_t>(i) * 2u + 1u] = r;
    }

    const double block_end =
        block_start + static_cast<double>(count) / rate;
    if (!session.has_pcm) {
        // First block ever: anchor the window at the block's media
        // start — earlier media times read as silence via pos < 0.
        session.pcm_media_start = block_start;
        session.pcm_media_end = block_start;
        session.has_pcm = true;
    }
    if (block_end <= session.pcm_media_end + 1e-9) {
        return true;   // fully covered already (duplicate/overlap tail)
    }

    std::size_t skip = 0;      // leading stereo frames to drop
    std::size_t gap_frames = 0;  // zero-fill frames for a timestamp hole
    if (block_start > session.pcm_media_end + 1e-9) {
        // Bound the hole in SECONDS before converting to a frame count
        // — a garbage pts must not overflow llround into a huge alloc.
        const double gap_seconds =
            block_start - session.pcm_media_end;
        if (gap_seconds > kMaxGapFillSeconds) {
            if (error_out != nullptr) {
                *error_out =
                    "excessive audio timestamp gap (>30 s)";
            }
            return false;
        }
        gap_frames = static_cast<std::size_t>(
            std::llround(gap_seconds * rate));
    } else if (block_start < session.pcm_media_end - 1e-9) {
        const double overlap = session.pcm_media_end - block_start;
        const long long drop =
            std::llround(overlap * rate);
        if (drop > 0) {
            skip = static_cast<std::size_t>(
                std::min<long long>(drop, count));
        }
    }
    if (skip >= static_cast<std::size_t>(count)) {
        return true;   // entire block already covered
    }

    if (gap_frames > 0) {
        session.pcm.insert(session.pcm.end(),
                           gap_frames * 2u, 0.0f);
        session.pcm_media_end +=
            static_cast<double>(gap_frames) / rate;
    }
    const std::size_t keep =
        static_cast<std::size_t>(count) - skip;
    session.pcm.insert(session.pcm.end(),
                       stereo.begin() + skip * 2u,
                       stereo.begin() + (skip + keep) * 2u);
    session.pcm_media_end += static_cast<double>(keep) / rate;
    return true;
}

// Drops leading window frames below `trim_below` media seconds,
// keeping kTrimMargin of headroom for interpolation's left neighbor.
void TrimWindow(AudioClipSession& session, double need_src_start) {
    if (!session.has_pcm || session.src_rate <= 0) {
        return;
    }
    const double frame_dur = 1.0 / session.src_rate;
    const double trim_below = need_src_start - kTrimMargin;
    const std::size_t frames = session.pcm.size() / 2u;
    std::size_t drop = 0;
    // Frame j covers [start + j*fd, start + (j+1)*fd) — drop while the
    // frame's END is at/below the trim line.
    while (drop < frames &&
           session.pcm_media_start +
                   static_cast<double>(drop + 1) * frame_dur <=
               trim_below) {
        ++drop;
    }
    if (drop == 0) {
        return;
    }
    session.pcm.erase(session.pcm.begin(),
                      session.pcm.begin() +
                          static_cast<std::ptrdiff_t>(drop * 2u));
    session.pcm_media_start +=
        static_cast<double>(drop) * frame_dur;
}

// ------------------------------------------------------------------
// Fade / transition envelopes (per output sample, timeline time tau).
// ------------------------------------------------------------------

double FadeEnvelope(const AudioClipSession& session, double tau) {
    double env = 1.0;
    if (session.fade_in > 0.0) {
        env *= std::clamp(
            (tau - session.clip_start) / session.fade_in, 0.0, 1.0);
    }
    if (session.fade_out > 0.0) {
        env *= std::clamp(
            (session.clip_end - tau) / session.fade_out, 0.0, 1.0);
    }
    return env;
}

// Product of per-transition weights — identical math to the
// compositor's TransitionWeightAt (w ramps 0->1 across the centered
// window [cut - d/2, cut + d/2)): from-clip gets 1-w, to-clip gets w.
// Outside every window the product stays 1.
double TransitionWeight(const AudioClipSession& session, double tau) {
    if (session.transitions.empty()) {
        return 1.0;
    }
    double weight = 1.0;
    for (const TransitionRef& ref : session.transitions) {
        const double d = ref.transition->duration;
        const double wlo = ref.cut - d * 0.5;
        const double whi = ref.cut + d * 0.5;
        if (tau < wlo || tau >= whi) {
            continue;   // outside -> factor 1
        }
        const double w = (tau - wlo) / d;
        weight *= ref.is_from ? (1.0 - w) : w;
    }
    return weight;
}

}  // namespace

// ------------------------------------------------------------------
// AudioMixer::Impl
// ------------------------------------------------------------------
struct AudioMixer::Impl {
    Project project_;
    int out_rate = 48000;
    int out_channels = 2;
    int chunk_samples = 1024;
    double duration_ = 0.0;
    std::size_t chunk_count_ = 0;
    MediaDecoderFactory decoder_factory;
    Vst::PluginInstanceFactory plugin_factory;
    bool initialized = false;
    std::atomic<bool> cancelled{false};
    std::vector<std::string> warnings;
    std::set<std::string> warned_once;
    std::map<std::string, AudioClipSession> sessions;
    // Sequential-production bookkeeping: index == last returns the
    // cached chunk; index < last is an error (sessions have moved on).
    bool has_last_ = false;
    std::size_t last_index_ = 0;
    FFmpeg::AudioFrameInfo last_chunk_;
    // UI meter state: unclamped |L|,|R| peaks of the last rendered
    // chunk (LastChunkPeaks()).
    float last_peak_l = 0.0f;
    float last_peak_r = 0.0f;

    bool Fail(std::string* error_out, const std::string& message) {
        if (error_out != nullptr) {
            *error_out = message;
        }
        return false;
    }

    void Warn(const std::string& message) {
        if (warned_once.insert(message).second) {
            warnings.push_back(message);
        }
    }

    // ---- Init ---------------------------------------------------------

    bool Init(const Project& project, const AudioMixerOptions& options,
              std::string* error_out,
              std::vector<std::string>* warnings_out) {
        Shutdown();
        if (options.sample_rate <= 0) {
            return Fail(error_out, "invalid mixer sample rate");
        }
        if (options.channels != 2) {
            return Fail(error_out,
                        "audio mixer v1 outputs stereo only");
        }
        if (options.chunk_samples <= 0) {
            return Fail(error_out, "invalid mixer chunk size");
        }
        project_ = project;   // snapshot — later edits never affect us
        out_rate = options.sample_rate;
        out_channels = options.channels;
        chunk_samples = options.chunk_samples;
        duration_ = project_.duration();
        chunk_count_ = static_cast<std::size_t>(std::ceil(
            duration_ * static_cast<double>(out_rate) /
            static_cast<double>(chunk_samples)));
        decoder_factory = options.decoder_factory;
        if (!decoder_factory) {
            decoder_factory = DefaultDecoderFactory();
        }
        // Null -> production VST3 host (dual-mono per node); tests
        // inject IPluginInstance fakes through options.plugin_factory.
        plugin_factory = options.plugin_factory
            ? options.plugin_factory
            : Vst::PluginInstanceFactory(&Vst::InstantiateVst3Plugin);
        cancelled.store(false, std::memory_order_relaxed);

        // Sessions for every clip passing the mute/track/hidden gates.
        // Decoder open + HasAudio() are deferred to first use (lazy).
        for (const Clip& clip : project_.clips) {
            // Quiet skips first — muting/hiding is normal editing.
            if (clip.muted) {
                continue;
            }
            const Track* track = FindTrack(project_, clip.track);
            if (track == nullptr) {
                Warn("clip '" + clip.id +
                     "' references missing track " +
                     std::to_string(clip.track) + "; skipped");
                continue;
            }
            // track.muted applies to AUDIO-kind tracks only (video-track
            // `muted` is ignored per the model contract); track.hidden
            // removes a video track AND its embedded audio, and is
            // ignored on audio tracks.
            if (track->kind == TrackKind::Audio && track->muted) {
                continue;
            }
            if (track->kind == TrackKind::Video && track->hidden) {
                continue;
            }

            AudioClipSession session;
            session.clip = &clip;
            // playback_rate scales the timeline->source map (see
            // SourceTimeAt); non-finite/non-positive falls back to 1.
            session.rate =
                std::isfinite(clip.playback_rate) &&
                        clip.playback_rate > 0.0
                    ? clip.playback_rate
                    : 1.0;
            // Equal-power stereo balance: the L/R gain RATIO follows
            // the constant-power cos/sin law at angle
            // (pan+1)*pi/4, normalized so the dominant channel stays
            // at unity — pan=0 is an exact no-op (no 3 dB program
            // drop), full pan mutes the opposite channel. Out-of-range
            // or non-finite pans clamp to center.
            const double pan = std::clamp(
                std::isfinite(static_cast<double>(clip.pan))
                    ? static_cast<double>(clip.pan)
                    : 0.0,
                -1.0, 1.0);
            const double angle = (pan + 1.0) * kPi * 0.25;
            const double gl = std::cos(angle);
            const double gr = std::sin(angle);
            const double gmax = std::max(gl, gr);  // >= ~0.7, never 0
            session.pan_l = gl / gmax;
            session.pan_r = gr / gmax;
            // Track gain_db folds into the per-chunk effective gain.
            session.track_gain_lin =
                std::isfinite(static_cast<double>(track->gain_db))
                    ? std::pow(
                          10.0,
                          static_cast<double>(track->gain_db) / 20.0)
                    : 1.0;
            session.clip_start = clip.timeline_start;
            session.clip_end = ClipEndSeconds(clip);
            session.fade_in = clip.fade_in;
            session.fade_out = clip.fade_out;
            const auto handles =
                ClipTransitionHandles(project_, clip.id);
            session.ext_start = clip.timeline_start - handles.first;
            session.ext_end = ClipEndSeconds(clip) + handles.second;
            // Per-clip plugin chain — instantiated lazily on the first
            // processed chunk; missing/broken plugins warn once and
            // bypass, never failing the mix.
            if (!clip.audio_fx.empty()) {
                session.fx_chain = std::make_unique<Vst::Vst3Chain>(
                    static_cast<double>(out_rate),
                    static_cast<std::uint32_t>(chunk_samples),
                    plugin_factory,
                    [this](const std::string& message) {
                        Warn(message);
                    });
                session.fx_chain->Sync(clip.audio_fx);
            }
            sessions.emplace(clip.id, std::move(session));
        }

        // Wire validated transitions onto their endpoint sessions —
        // same validity rules as the compositor (both endpoints exist,
        // same VIDEO track, adjacent within epsilon). Weight applies
        // even when the OTHER endpoint has no audio session (a lone
        // clip still fades through the window).
        for (const Transition& transition : project_.transitions) {
            const Clip* from = FindClip(project_, transition.from_clip);
            const Clip* to = FindClip(project_, transition.to_clip);
            const bool ok =
                from != nullptr && to != nullptr &&
                from->track == to->track && from->track >= 0 &&
                from->track <
                    static_cast<int>(project_.tracks.size()) &&
                project_.tracks[static_cast<std::size_t>(from->track)]
                        .kind == TrackKind::Video &&
                std::fabs(to->timeline_start - ClipEndSeconds(*from)) <=
                    kAdjacencyEpsilon;
            if (!ok) {
                Warn("transition '" + transition.from_clip + "' -> '" +
                     transition.to_clip +
                     "' is not between adjacent clips on a video "
                     "track; ignored");
                continue;
            }
            if (!(transition.duration > 0.0)) {
                continue;   // degenerate: weights would be unreachable
            }
            const double cut = ClipEndSeconds(*from);
            auto from_it = sessions.find(transition.from_clip);
            if (from_it != sessions.end()) {
                from_it->second.transitions.push_back(
                    {&transition, cut, true});
            }
            auto to_it = sessions.find(transition.to_clip);
            if (to_it != sessions.end()) {
                to_it->second.transitions.push_back(
                    {&transition, cut, false});
            }
        }

        initialized = true;
        if (warnings_out != nullptr) {
            warnings_out->insert(warnings_out->end(), warnings.begin(),
                                 warnings.end());
        }
        return true;
    }

    void Shutdown() {
        for (auto& entry : sessions) {
            if (entry.second.decoder != nullptr) {
                entry.second.decoder->Close();
            }
        }
        sessions.clear();
        warnings.clear();
        warned_once.clear();
        has_last_ = false;
        last_chunk_ = FFmpeg::AudioFrameInfo{};
        last_peak_l = 0.0f;
        last_peak_r = 0.0f;
        cancelled.store(false, std::memory_order_relaxed);
        initialized = false;
        duration_ = 0.0;
        chunk_count_ = 0;
    }

    // ---- session lifecycle --------------------------------------------

    // Lazy open on the first chunk overlapping the clip's extended
    // range. Soft failures (missing file, no audio stream, bad format)
    // warn + mark dead — the clip contributes silence. A null factory
    // result is a config error -> hard fail.
    bool EnsureOpen(AudioClipSession& session, double need_src_start,
                    std::string* error_out) {
        if (session.open_ok || session.dead || session.closed) {
            return true;
        }
        if (session.open_attempted) {
            // A previous EnsureOpen hard-failed (null factory) — the
            // export is already aborting; keep failing deterministically.
            return Fail(error_out,
                        "audio decoder unavailable for '" +
                            session.clip->id + "'");
        }
        session.open_attempted = true;
        if (IsGeneratedSource(session.clip->source)) {
            // Procedural clips (matte/bars/text) have no media file —
            // contribute silence without touching the decoder.
            session.dead = true;
            return true;
        }
        MediaDecoderPtr decoder = decoder_factory();
        if (decoder == nullptr) {
            return Fail(error_out,
                        "audio mixer decoder factory returned null");
        }
        decoder->EnableAudioTap(true);   // BEFORE Open — hard contract
        std::string err;
        if (!decoder->Open(session.clip->source_path, &err)) {
            Warn("clip '" + session.clip->id +
                 "': audio decoder open failed: " + err);
            session.dead = true;
            return true;
        }
        if (!decoder->HasAudio()) {
            Warn("clip '" + session.clip->id +
                 "' has no audio stream; skipped");
            session.dead = true;
            decoder->Close();
            return true;
        }
        session.audio_tb = decoder->GetAudioTimeBase();
        session.audio_origin = decoder->GetAudioStartSeconds();
        const double media_duration = decoder->GetDurationSeconds();
        // Decoder duration is unreliable on encoder-produced MP4s —
        // use it as an upper read clamp only when positive, and never
        // let it truncate below what the clip's own bounds require
        // (in_point + duration + out_handle).
        const double clip_need =
            session.clip->in_point +
            (session.ext_end - session.clip->timeline_start) *
                session.rate;
        session.src_hi =
            std::max(media_duration > 0.0 ? media_duration : 0.0,
                     clip_need);
        if (session.src_hi <= 0.0) {
            session.src_hi =
                std::numeric_limits<double>::infinity();
        }
        session.decoder = std::move(decoder);
        session.open_ok = true;

        // Land the decoder ~kSeekLeadSeconds before the first needed
        // media time (Seek is keyframe-backward only). A failed seek
        // just means forward decode from 0 — still correct, slower.
        const double target = std::max(
            0.0, need_src_start - kSeekLeadSeconds);
        if (target > 0.01 && !session.decoder->Seek(target)) {
            Warn("clip '" + session.clip->id +
                 "': audio seek failed; decoding from start");
        }
        return true;
    }

    // Pump the decoder until the PCM window covers `need_src_end`
    // (+margin) or the stream drains. Sequential forward only.
    bool EnsureCoverage(AudioClipSession& session,
                        double need_src_end, double need_src_start,
                        std::string* error_out) {
        if (session.dead || session.closed) {
            return true;
        }
        if (!session.open_ok &&
            !EnsureOpen(session, need_src_start, error_out)) {
            return false;
        }
        if (session.dead) {
            return true;   // open/has-audio soft-fail -> silence
        }
        TrimWindow(session, need_src_start);
        int guard = 0;
        // Reused across iterations — NextFrame resizes `bgra` in place
        // and ReadAudioFrames appends (cleared each pass).
        std::vector<std::uint8_t> bgra;
        std::vector<FFmpeg::AudioFrameInfo> frames;
        int width = 0;
        int height = 0;
        while (session.pcm_media_end <
                   need_src_end + kCoverageMargin &&
               !session.audio_done) {
            if (cancelled.load(std::memory_order_relaxed)) {
                return Fail(error_out, "export cancelled");
            }
            if (guard++ >= kMaxPumpIterations) {
                return Fail(error_out,
                            "audio decode starvation for '" +
                                session.clip->source_path + "'");
            }
            // NextFrame pumps demux packets into the audio tap. For
            // audio-only containers it ALWAYS returns false yet MUST
            // still be called (bounded batch per call); for A/V files
            // it decodes one video frame we discard — the separate
            // audio session's spec-mandated cost. Its return value is
            // intentionally ignored: audio completion is governed by
            // AudioDrained(), not video EOF.
            session.decoder->NextFrame(&bgra, &width, &height);

            frames.clear();
            // false means "no decode codec" or a decode error — the
            // former still reaches AudioDrained() (documented OK),
            // the latter spins until the starvation guard fires.
            const bool read_ok = session.decoder->ReadAudioFrames(
                &frames, kReadBatch);
            if (!read_ok && session.decoder->AudioDrained()) {
                session.audio_done = true;
                break;
            }
            for (const FFmpeg::AudioFrameInfo& frame : frames) {
                // Block media start: best_effort_timestamp preferred,
                // else pts, both * audio time base; missing timestamps
                // continue from the PCM media end (anchored at
                // GetAudioStartSeconds for the first block — the AAC
                // priming / stream-offset case).
                std::int64_t stamp = frame.best_effort_timestamp;
                if (stamp == kNoPts) {
                    stamp = frame.pts;
                }
                double block_start;
                if (stamp != kNoPts && session.audio_tb > 0.0) {
                    block_start =
                        static_cast<double>(stamp) * session.audio_tb;
                } else {
                    block_start = session.has_pcm
                                      ? session.pcm_media_end
                                      : session.audio_origin;
                }
                std::string convert_error;
                if (!AppendBlock(session, frame, block_start,
                                 &convert_error)) {
                    Warn("clip '" + session.clip->id +
                         "': " + convert_error + "; skipped");
                    session.dead = true;
                    return true;
                }
            }
            if (session.dead) {
                return true;
            }
            if (frames.empty() && session.decoder->AudioDrained()) {
                session.audio_done = true;
                if (!session.has_pcm) {
                    Warn("clip '" + session.clip->id +
                         "': audio stream yielded no frames");
                }
            }
        }
        return true;
    }

    // ---- mixing --------------------------------------------------------

    // Interpolates one session's stereo window at media time `src`.
    // Returns false when the sample is unavailable — callers output
    // silence for that clip (never a frozen last sample).
    bool SampleSession(const AudioClipSession& session, double src,
                       float* out_l, float* out_r) const {
        if (session.src_rate <= 0 || session.pcm.size() < 4u) {
            return false;
        }
        const double rate = session.src_rate;
        const double pos =
            (src - session.pcm_media_start) * rate;
        const std::size_t frames = session.pcm.size() / 2u;
        // Need BOTH interpolation neighbors inside the window:
        // pos in [0, frames-1) keeps i0+1 <= frames-1.
        if (!(pos >= 0.0) || pos >=
                                 static_cast<double>(frames) - 1.0) {
            return false;
        }
        const std::size_t i0 = static_cast<std::size_t>(pos);
        const double frac = pos - static_cast<double>(i0);
        const std::size_t o0 = i0 * 2u;
        const std::size_t o1 = o0 + 2u;
        // deque::operator[] — the window is NOT contiguous storage.
        *out_l = session.pcm[o0] +
                 static_cast<float>(frac) *
                     (session.pcm[o1] - session.pcm[o0]);
        *out_r = session.pcm[o0 + 1u] +
                 static_cast<float>(frac) *
                     (session.pcm[o1 + 1u] - session.pcm[o0 + 1u]);
        return true;
    }

    bool GetChunk(std::size_t index, FFmpeg::AudioFrameInfo* out,
                  std::string* error_out) {
        if (cancelled.load(std::memory_order_relaxed)) {
            return Fail(error_out, "export cancelled");
        }
        if (!initialized) {
            return Fail(error_out, "audio mixer not initialized");
        }
        if (out == nullptr) {
            return Fail(error_out, "out is required");
        }
        if (index >= chunk_count_) {
            return Fail(error_out, "chunk index out of range");
        }
        if (has_last_) {
            if (index < last_index_) {
                return Fail(error_out,
                            "chunk index out of order "
                            "(sequential production)");
            }
            if (index == last_index_) {
                *out = last_chunk_;   // cached — non-decreasing allows it
                return true;
            }
        }

        const double chunk_dur =
            static_cast<double>(chunk_samples) / out_rate;
        const double t0 =
            static_cast<double>(index) * chunk_dur;
        double t1 = t0 + chunk_dur;
        if (t1 > duration_) {
            t1 = duration_;
        }
        int n = static_cast<int>(std::llround((t1 - t0) * out_rate));
        n = std::clamp(n, 1, chunk_samples);

        // Drive every session whose extended range overlaps this
        // chunk: close finished ones, lazily open + pump the rest.
        for (auto& entry : sessions) {
            AudioClipSession& session = entry.second;
            if (session.dead || session.closed) {
                continue;
            }
            if (session.ext_end <= t0) {
                // Contribution window fully passed — release the
                // decoder; later chunks never need it again.
                if (session.decoder != nullptr) {
                    session.decoder->Close();
                    session.decoder.reset();
                }
                session.closed = true;
                continue;
            }
            if (session.ext_start >= t1) {
                continue;   // future clip — stays unopened
            }
            // Overlapping chunk: coverage needed up to the later of
            // T1 and the session's ext_end (clamped), mapped to source
            // media time.
            const double tau_hi = std::min(t1, session.ext_end);
            const double tau_lo = std::max(t0, session.ext_start);
            const double need_src_end = SourceTimeAt(session, tau_hi);
            const double need_src_start = SourceTimeAt(session, tau_lo);
            if (!EnsureCoverage(session, need_src_end, need_src_start,
                                error_out)) {
                return false;
            }
        }

        // Mix: each contributing session renders interpolated stereo
        // samples * gain * fades * transition weight * pan into a
        // per-clip contribution buffer, optionally through its
        // clip.audio_fx plugin chain, then accumulates into the mix.
        out->stream_index = -1;
        out->sample_rate = out_rate;
        out->channels = out_channels;
        out->sample_count = n;
        out->planar = false;
        out->sample_format = "s16";
        out->pts = static_cast<std::int64_t>(index) * chunk_samples;
        out->best_effort_timestamp = out->pts;
        out->plane_sizes.clear();
        out->data.assign(static_cast<std::size_t>(n) * 4u, 0);
        {
            // Per-chunk effective gains: GainDb keyframes evaluate
            // once per chunk at the chunk's clip-local start
            // (chunk-rate automation — the contract fixes u at t0),
            // folded with the static track gain. Dead/closed sessions
            // keep a stale mix_gain_lin but are skipped by the mix
            // loop anyway.
            for (auto& entry : sessions) {
                AudioClipSession& session = entry.second;
                if (session.dead || session.closed) {
                    continue;
                }
                const double u0 = t0 - session.clip_start;
                const double gain_db = static_cast<double>(
                    EffectiveGainDbAt(*session.clip, u0));
                session.mix_gain_lin =
                    session.track_gain_lin *
                    std::pow(
                        10.0,
                        (std::isfinite(gain_db) ? gain_db : 0.0) / 20.0);
            }
            // Per-clip contribution pass: each session renders its
            // gain/pan/fade/transition-weighted stereo floats into
            // `contrib`, runs its plugin chain (if any) in place, then
            // accumulates into `mix`. The chain insertion point is
            // exactly "after this clip's gain/pan/fades, before
            // accumulation into the mix" — Phase-2 clip.audio_fx.
            std::vector<double> mix(
                static_cast<std::size_t>(n) * 2u, 0.0);
            std::vector<float> contrib;
            for (auto& entry : sessions) {
                AudioClipSession& session = entry.second;
                if (session.dead || session.closed ||
                    !session.open_ok || !session.has_pcm ||
                    session.src_rate <= 0) {
                    continue;
                }
                if (t1 <= session.ext_start ||
                    t0 >= session.ext_end) {
                    continue;   // no overlap with this chunk
                }
                contrib.assign(static_cast<std::size_t>(n) * 2u, 0.0f);
                bool any = false;
                for (int i = 0; i < n; ++i) {
                    const double tau = t0 +
                        static_cast<double>(i) / out_rate;
                    if (tau < session.ext_start ||
                        tau >= session.ext_end) {
                        continue;
                    }
                    const double src = SourceTimeAt(session, tau);
                    if (src < 0.0 || src > session.src_hi) {
                        continue;  // outside media -> silence, no freeze
                    }
                    float l = 0.0f;
                    float r = 0.0f;
                    if (!SampleSession(session, src, &l, &r)) {
                        continue;
                    }
                    const double env =
                        session.mix_gain_lin *
                        FadeEnvelope(session, tau) *
                        TransitionWeight(session, tau);
                    // Per-sample equal-power pan on top of gain/fade/
                    // transition weighting.
                    contrib[static_cast<std::size_t>(i) * 2u] =
                        static_cast<float>(
                            static_cast<double>(l) * env *
                            session.pan_l);
                    contrib[static_cast<std::size_t>(i) * 2u + 1u] =
                        static_cast<float>(
                            static_cast<double>(r) * env *
                            session.pan_r);
                    any = true;
                }
                if (!any) {
                    continue;
                }
                // Sync() is a cheap no-op unless clip.audio_fx changed
                // (signature compare); ProcessInterleaved leaves
                // silence untouched through bypassed nodes.
                if (session.fx_chain != nullptr) {
                    session.fx_chain->Sync(session.clip->audio_fx);
                    session.fx_chain->ProcessInterleaved(
                        contrib.data(),
                        static_cast<std::uint32_t>(n));
                }
                for (std::size_t i = 0; i < mix.size(); ++i) {
                    mix[i] += contrib[i];
                }
            }
            // Build into a typed buffer, then copy bytes — avoids
            // alignment/aliasing concerns on the uint8_t payload.
            std::vector<std::int16_t> pcm16(
                static_cast<std::size_t>(n) * 2u);
            double peak_l = 0.0;
            double peak_r = 0.0;
            for (int i = 0; i < n; ++i) {
                const double acc_l = mix[static_cast<std::size_t>(i) * 2u];
                const double acc_r =
                    mix[static_cast<std::size_t>(i) * 2u + 1u];
                // clamp -> *32767 -> round -> interleaved s16 (LE on
                // the Windows targets this ships on).
                pcm16[static_cast<std::size_t>(i) * 2u] =
                    static_cast<std::int16_t>(
                        std::lrint(std::clamp(acc_l, -1.0, 1.0) *
                                   32767.0));
                pcm16[static_cast<std::size_t>(i) * 2u + 1u] =
                    static_cast<std::int16_t>(
                        std::lrint(std::clamp(acc_r, -1.0, 1.0) *
                                   32767.0));
                // Meter peaks come from the unclamped accumulators so
                // overs show as > 1.0 to the UI.
                peak_l = std::max(peak_l, std::fabs(acc_l));
                peak_r = std::max(peak_r, std::fabs(acc_r));
            }
            std::memcpy(out->data.data(), pcm16.data(),
                        static_cast<std::size_t>(n) * 4u);
            last_peak_l = static_cast<float>(peak_l);
            last_peak_r = static_cast<float>(peak_r);
        }

        last_chunk_ = *out;
        last_index_ = index;
        has_last_ = true;
        return true;
    }
};

// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

AudioMixer::AudioMixer() : impl_(std::make_unique<Impl>()) {}
AudioMixer::~AudioMixer() = default;

bool AudioMixer::Init(const Project& project,
                      const AudioMixerOptions& options,
                      std::string* error_out,
                      std::vector<std::string>* warnings_out) {
    return impl_->Init(project, options, error_out, warnings_out);
}

void AudioMixer::Shutdown() { impl_->Shutdown(); }

double AudioMixer::DurationSeconds() const {
    return impl_->duration_;
}

std::size_t AudioMixer::ChunkCount() const {
    return impl_->chunk_count_;
}

bool AudioMixer::GetChunk(std::size_t index,
                          XER::Bridge::FFmpeg::AudioFrameInfo* out,
                          std::string* error_out) {
    return impl_->GetChunk(index, out, error_out);
}

std::pair<float, float> AudioMixer::LastChunkPeaks() const {
    return {impl_->last_peak_l, impl_->last_peak_r};
}

void AudioMixer::Cancel() {
    impl_->cancelled.store(true, std::memory_order_relaxed);
}

bool AudioMixer::Cancelled() const {
    return impl_->cancelled.load(std::memory_order_relaxed);
}

const std::vector<std::string>& AudioMixer::Warnings() const {
    return impl_->warnings;
}

}  // namespace XER::VideoEditor
