// XER::Media smoke — exercises the new media API surface:
// session/audio-focus algorithm, muxer codec table + timestamp adapter,
// router bookkeeping, decoder-stream plumbing, and (when ffmpeg.exe is
// available) an end-to-end FFmpegDemuxer decode of a generated clip.

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <filesystem>

#include "media/media.h"
#include "media/muxers/live_webm_muxer.h"
#include "media/decoders/decoder_buffer_queue.h"
#include "media/base/audio_clock.h"
#include "media/cdm/clear_key_decryptor.h"
#include "media/demuxer/async_data_source.h"
#include "media/audio/audio_sync_reader.h"
#include "media/audio/audio_sync_writer.h"
#include "media/audio/wasapi_output_stream.h"
#include "media/renderers/frame_cache_sink.h"
#include "media/demuxer/buffered_data_source.h"
#include "ui/css/document_style.h"
#include "ui/css/style_engine.h"
#include "ui/css/stylesheet_parser.h"
#include "video/clip_frame_cache.h"

using namespace XER::Media;

static int failures = 0;
#define CHECK(cond, label)                                              \
    do {                                                                \
        if (!(cond)) {                                                  \
            ++failures;                                                 \
            std::printf("FAIL: %s\n", label);                           \
        }                                                               \
    } while (0)

static void TestAudioFocus() {
    bool a_duck = false;
    SessionState a_state = SessionState::kInactive;
    MediaSessionImpl a([&](SessionState s, bool duck) {
        a_state = s;
        a_duck = duck;
    });
    bool b_active = false;
    MediaSessionImpl b(
        [&](SessionState s, bool) { b_active = s == SessionState::kActive; });

    auto& mgr = AudioFocusManager::Instance();
    mgr.RequestAudioFocus(&a, AudioFocusType::kPersistent);
    CHECK(a_state == SessionState::kActive && !a_duck,
          "focus: persistent active");
    mgr.RequestAudioFocus(&b, AudioFocusType::kPersistent);
    CHECK(b_active, "focus: second persistent active");
    CHECK(a_state == SessionState::kSuspended, "focus: first suspended");

    bool t_active = false;
    MediaSessionImpl tr(
        [&](SessionState s, bool) { t_active = s == SessionState::kActive; });
    mgr.RequestAudioFocus(&tr, AudioFocusType::kTransient);
    CHECK(t_active, "focus: transient active");

    mgr.AbandonAudioFocus(&tr);
    mgr.AbandonAudioFocus(&b);
    mgr.AbandonAudioFocus(&a);
}

static void TestMuxer() {
    CHECK(ContainerSupportsCodec(Container::kWebm, CodecId::kVP9),
          "muxer: webm+vp9");
    CHECK(ContainerSupportsCodec(Container::kWebm, CodecId::kOpus),
          "muxer: webm+opus");
    CHECK(!ContainerSupportsCodec(Container::kWebm, CodecId::kAAC),
          "muxer: webm !aac");
    CHECK(ContainerSupportsCodec(Container::kMp4, CodecId::kAAC),
          "muxer: mp4+aac");
    CHECK(ContainerSupportsCodec(Container::kMkv, CodecId::kH265),
          "muxer: mkv+h265");
    CHECK(!ContainerSupportsCodec(Container::kWebm, CodecId::kH265),
          "muxer: webm !h265");

    MuxerTimestampAdapter ad;
    CHECK(ad.Normalize(StreamType::kVideo, 5000000) == 0,
          "ts: video origin zero");
    CHECK(ad.Normalize(StreamType::kVideo, 5033333) == 33333,
          "ts: video delta");
    CHECK(ad.Normalize(StreamType::kAudio, 7000000) == 0,
          "ts: audio origin zero");
    CHECK(ad.Normalize(StreamType::kVideo, 4000000) == 33333,
          "ts: monotonic");
    ad.Reset();
    CHECK(ad.Normalize(StreamType::kVideo, 100) == 0, "ts: reset");
}

static void TestCodecNames() {
    CHECK(CodecFromName("avc1") == CodecId::kH264, "codec: avc1->h264");
    CHECK(CodecFromName("hvc1") == CodecId::kH265, "codec: hvc1->h265");
    CHECK(CodecFromName("mp4a") == CodecId::kAAC, "codec: mp4a->aac");
    CHECK(CodecName(CodecId::kOpus) == "opus", "codec: opus name");
}

class FakeRoute final : public MediaRoute {
public:
    FakeRoute(RouteId id, MediaSource src, MediaSink sink)
        : id_(id), src_(std::move(src)), sink_(std::move(sink)) {}
    RouteId id() const override { return id_; }
    const MediaSource& source() const override { return src_; }
    const MediaSink& sink() const override { return sink_; }
    bool SendMessage(const std::string& message, std::string*) override {
        last = message;
        return true;
    }
    void SetMessageHandler(std::function<void(std::string)> cb) override {
        handler_ = std::move(cb);
    }
    bool active() const override { return active_; }

    std::string last;
    bool active_ = true;

private:
    RouteId id_;
    MediaSource src_;
    MediaSink sink_;
    std::function<void(std::string)> handler_;
};

class FakeProvider final : public MediaRouteProvider {
public:
    std::string name() const override { return "fake"; }
    std::vector<MediaSink> ListSinks(const MediaSource&) override {
        return {{"sink-1", "Living Room", "fake"}};
    }
    std::unique_ptr<MediaRoute> CreateRoute(const MediaSource& source,
                                            const MediaSink& sink,
                                            std::string*) override {
        auto r = std::make_unique<FakeRoute>(next_id_++, source, sink);
        last_route_ = r.get();
        return r;
    }
    bool TerminateRoute(RouteId route, std::string*) override {
        if (last_route_) last_route_->active_ = false;
        terminated.push_back(route);
        return true;
    }
    bool RouteSendMessage(RouteId, const std::string& m,
                          std::string*) override {
        if (last_route_) last_route_->SendMessage(m, nullptr);
        return true;
    }
    RouteId next_id_ = 100;
    FakeRoute* last_route_ = nullptr;
    std::vector<RouteId> terminated;
};

static void TestRouter() {
    auto provider_owner = std::make_unique<FakeProvider>();
    auto* provider = provider_owner.get();
    MediaRouter::Instance().AddProvider(std::move(provider_owner));

    MediaSource src{"xer://media/clip.mp4"};
    bool notified = false;
    MediaRouter::Instance().RegisterSinksListener(
        src, [&](std::vector<MediaSink> sinks) {
            notified = !sinks.empty();
        });
    CHECK(notified, "router: sinks notification");

    std::string err;
    RouteId route = MediaRouter::Instance().CreateRoute(
        src, {"sink-1", "Living Room", "fake"}, &err);
    CHECK(route != kInvalidRouteId, "router: route created");
    CHECK(MediaRouter::Instance().SendMessage(route, "PLAY", &err),
          "router: message sent");
    CHECK(provider->last_route_ && provider->last_route_->last == "PLAY",
          "router: message delivered");
    CHECK(MediaRouter::Instance().TerminateRoute(route, &err),
          "router: terminated");
    CHECK(!provider->terminated.empty(), "router: provider saw terminate");
    MediaRouter::Instance().UnregisterSinksListener(src);
}

class FakeStream final : public DemuxerStream {
public:
    StreamType type() const override { return StreamType::kVideo; }
    CodecId codec() const override { return CodecId::kH264; }
    void Read(ReadCallback cb) override {
        ReadResult r;
        if (++reads_ > 3) {
            r.end_of_stream = true;
        } else {
            r.ok = true;
            r.timestamp = reads_ * 33333;
            r.data = {0, 1, 2, 3};
        }
        cb(std::move(r));
    }
    int reads_ = 0;
};

class FakeDecoder final : public VideoDecoder {
public:
    bool Initialize(const DemuxerStream& s, std::string*) override {
        return s.type() == StreamType::kVideo;
    }
    void Decode(const DemuxerStream::ReadResult& u,
                DecodeCallback cb) override {
        DecodedFrame f;
        f.timestamp = u.timestamp;
        f.pixels = u.data;
        cb(PipelineStatus::kOk, std::move(f));
    }
    void Reset() override {}
    bool CanDecode(CodecId c) const override { return c == CodecId::kH264; }
};

static void TestDecoderStream() {
    FakeStream stream;
    FakeDecoder decoder;
    DecoderStream ds(&stream, &decoder);
    std::string err;
    CHECK(ds.Initialize(&err), "ds: init");
    int frames = 0;
    ds.Start([&](DecodedFrame) { ++frames; });
    CHECK(frames == 3, "ds: three frames pumped");
}

static bool GenerateTestClip(std::string* out_path) {
    const char* path = "media_smoke_test.mp4";
    std::string cmd = "ffmpeg -y -loglevel error -f lavfi "
                      "-i testsrc=duration=0.5:size=64x64:rate=10 "
                      "-pix_fmt yuv420p ";
    cmd += path;
    cmd += " 2>media_smoke_ffmpeg.err";
    if (std::system(cmd.c_str()) != 0) return false;
    *out_path = path;
    return true;
}

static void TestFFmpegDemuxer() {
    std::string path;
    if (!GenerateTestClip(&path)) {
        std::printf("SKIP: ffmpeg CLI unavailable\n");
        return;
    }
    FFmpegDemuxer demuxer;
    std::string err;
    CHECK(demuxer.Open(path, &err), "ffmpeg: open");
    if (!demuxer.IsOpen()) {
        std::remove(path.c_str());
        return;
    }
    CHECK(!demuxer.streams().empty(), "ffmpeg: streams");
    auto* vs = demuxer.GetStream(StreamType::kVideo);
    CHECK(vs != nullptr, "ffmpeg: video stream");
    if (vs) {
        int reads = 0;
        for (int i = 0; i < 20; ++i) {
            bool done = false;
            vs->Read([&](DemuxerStream::ReadResult r) {
                if (r.ok) ++reads;
                if (r.end_of_stream) done = true;
            });
            if (done) break;
        }
        CHECK(reads >= 4, "ffmpeg: decoded frames");
    }
    CHECK(demuxer.duration() > 0, "ffmpeg: duration");
    demuxer.Close();
    std::remove(path.c_str());
    std::remove("media_smoke_ffmpeg.err");
}

// ---- base: AudioFifo + ChannelMixer ----

void TestAudioFifoAndMixer() {
    AudioFifo fifo(2, 128);
    auto src = AudioBus::Create(2, 64);
    for (int c = 0; c < 2; ++c)
        for (int f = 0; f < 64; ++f) src.channel(c)[f] = 0.5f;
    if (fifo.Push(src, 64) != 64 || fifo.frames() != 64)
        CHECK(false, "fifo push");
    auto dst = AudioBus::Create(2, 64);
    if (fifo.Pop(&dst, 64) != 64 || dst.channel(0)[10] != 0.5f)
        CHECK(false, "fifo pop");
    if (!fifo.empty()) CHECK(false, "fifo empty");

    ChannelMixer mixer(ChannelLayout::kStereo, ChannelLayout::kMono);
    auto mono = AudioBus::Create(1, 16);
    mixer.Transform(src, &mono);
    if (mono.channel(0)[0] != 0.5f) CHECK(false, "stereo->mono mix");
}

// ---- base: SeekableBuffer ----

void TestSeekableBuffer() {
    SeekableBuffer buf;
    const std::uint8_t a[] = {1, 2, 3, 4};
    const std::uint8_t b[] = {5, 6, 7, 8};
    buf.Append(a, 4, 1000);
    buf.Append(b, 4, 2000);
    std::uint8_t out[8];
    TimeTicks ts = 0;
    if (buf.Read(out, 6, &ts) != 6 || ts != 1000 || out[4] != 5)
        CHECK(false, "seekable read");
    if (!buf.SeekBackward(2000)) CHECK(false, "seek backward");
    if (buf.Read(out, 2, nullptr) != 2 || out[0] != 5)
        CHECK(false, "seekable reread");
}

// ---- muxers: LiveWebmMuxer EBML ----

void TestLiveWebmMuxer() {
    LiveWebmMuxer muxer;
    std::vector<std::uint8_t> stream;
    muxer.SetOutput([&](const std::uint8_t* d, std::size_t n) {
        stream.insert(stream.end(), d, d + n);
    });
    if (!muxer.Open(Container::kWebm, "", nullptr)) CHECK(false, "webm open");
    if (!muxer.AddStream(StreamType::kVideo, CodecId::kVP9, nullptr))
        CHECK(false, "webm add stream");
    MuxedPacket pkt;
    pkt.type = StreamType::kVideo;
    pkt.codec = CodecId::kVP9;
    pkt.timestamp = 0;
    pkt.data = {0xDE, 0xAD, 0xBE, 0xEF};
    if (!muxer.WritePacket(pkt, nullptr)) CHECK(false, "webm write");
    muxer.Close(nullptr);
    // EBML magic + Segment + "webm" doctype must appear.
    if (stream.size() < 40 || stream[0] != 0x1A || stream[1] != 0x45 ||
        stream[2] != 0xDF || stream[3] != 0xA3)
        CHECK(false, "webm ebml magic");
    const std::string s(stream.begin(), stream.end());
    if (s.find("webm") == std::string::npos) CHECK(false, "webm doctype");
    if (s.find("V_VP9") == std::string::npos) CHECK(false, "webm codec id");
}

// ---- decoders: DecoderBufferQueue backpressure ----

void TestDecoderBufferQueue() {
    DecoderBufferQueue q(2);
    DemuxerStream::ReadResult u;
    u.ok = true;
    if (!q.Push(u) || !q.Push(u) || q.Push(u))
        CHECK(false, "queue backpressure");
    DemuxerStream::ReadResult out;
    if (!q.Pop(&out) || !out.ok) CHECK(false, "queue pop");
}

// ---- renderers: AudioClock ----

void TestAudioClock() {
    AudioClock clock(48000);
    clock.WroteAudio(48000);  // exactly 1s
    if (clock.Now() != 1000000) CHECK(false, "audio clock 1s");
    clock.Pause();
    if (clock.Now() != 1000000 || !clock.paused()) CHECK(false, "clock pause");
}

// ---- cdm: ClearKeyDecryptor roundtrip ----

void TestClearKey() {
    ClearKeyDecryptor cdm;
    std::vector<std::uint8_t> key_id(16, 1), key(16, 2), iv(16, 0);
    cdm.AddKey(key_id, key);
    DecryptConfig cfg{key_id, iv, false};
    const std::vector<std::uint8_t> plain = {9, 9, 9, 9};
    // Encrypt = decrypt (XOR keystream is symmetric).
    std::vector<std::uint8_t> enc, dec;
    if (cdm.Decrypt(cfg, plain, &enc, nullptr) != CdmStatus::kOk)
        CHECK(false, "clearkey encrypt");
    if (cdm.Decrypt(cfg, enc, &dec, nullptr) != CdmStatus::kOk ||
        dec != plain)
        CHECK(false, "clearkey roundtrip");
    if (!cdm.IsKeyAvailable(key_id)) CHECK(false, "key availability");
}

// ---- src/ bridges: AsyncDataSource (async_io) ----

void TestAsyncDataSource() {
    const std::string path = "media_smoke_async.bin";
    {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) { CHECK(false, "async: create file"); return; }
        for (int i = 0; i < 256; ++i) std::fputc(i & 0xFF, f);
        std::fclose(f);
    }
    std::string err;
    auto ds = AsyncDataSource::Open(path, &err);
    CHECK(ds != nullptr, "async: open");
    if (!ds) { std::remove(path.c_str()); return; }
    CHECK(ds->size() == 256, "async: size");

    // Sync facade over the async reader.
    std::uint8_t buf[32];
    std::size_t n = 0;
    CHECK(ds->ReadAt(64, buf, sizeof buf, &n) == DataSource::Status::kOk &&
              n == 32 && buf[0] == 64,
          "async: sync ReadAt");

    // Two range reads in flight on the native async path.
    auto f1 = ds->ReadAtAsync(0, 16);
    auto f2 = ds->ReadAtAsync(240, 16);
    auto d1 = f1.get();
    auto d2 = f2.get();
    CHECK(d1.size() == 16 && d1[0] == 0 && d1[15] == 15, "async: read1");
    CHECK(d2.size() == 16 && d2[0] == 240, "async: read2");

    // EOS at end of file.
    CHECK(ds->ReadAt(1024, buf, 4, &n) == DataSource::Status::kEos,
          "async: eos");
    std::remove(path.c_str());
}

// ---- src/ bridges: sync channel over SharedMemoryChannel (cache) ----

void TestSyncChannel() {
    AudioParameters p;
    p.frames_per_buffer = 16;
    AudioSyncWriter writer(p);
    AudioSyncReader reader(p);
    std::string err;
    const std::string chan = "media-smoke-sync";
    CHECK(writer.AttachChannel(chan, &err), "sync: writer channel");
    CHECK(reader.AttachChannel(chan, &err), "sync: reader channel");
    const std::uint8_t block[32] = {1, 2, 3, 4};
    CHECK(writer.Write(block, sizeof block), "sync: write");
    std::uint8_t dest[256] = {};
    CHECK(reader.Read(dest, 2000) == 16 && dest[0] == 1 && dest[3] == 4,
          "sync: read block");
}

// ---- src/ bridges: WasapiOutputStream over AudioEngine ----

void TestEngineStream() {
    AudioParameters p;
    p.format = SampleFormat::kF32;
    p.frames_per_buffer = 64;
    WasapiOutputStream stream(p);
    std::string err;
    CHECK(stream.Open(p, &err), "engine: open");
    struct Puller : AudioOutputStream::AudioSourceCallback {
        int calls = 0;
        int OnMoreData(std::uint8_t* dest, int frames,
                       const AudioParameters& params) override {
            ++calls;
            std::memset(dest, 0,
                        frames * params.channels() *
                            SampleFormatBytes(params.format));
            return frames;
        }
        void OnError(const std::string&) override {}
    } puller;
    stream.Start(&puller);
    for (int i = 0; i < 20 && puller.calls == 0; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    stream.Stop();
    CHECK(puller.calls > 0, "engine: pull callback fired");
}

// ---- src/ bridges: FrameCacheSink (video + cache) ----

void TestFrameCacheSink() {
    const std::string root = "media_smoke_cache";
    int fired = 0;
    {
        FrameCacheSink sink(root, "test-clip");
        sink.Start([&] { ++fired; });
        DecodedFrame f;
        f.width = 4;
        f.height = 4;
        f.timestamp = 33000;
        f.pixels.assign(4 * 4 * 4, 0x7F);
        std::string err;
        CHECK(sink.Render(f, &err), "sink: render 1");
        f.timestamp = 66000;
        CHECK(sink.Render(f, &err), "sink: render 2");
        sink.Stop();
    }
    CHECK(fired == 2, "sink: callbacks");
    // Replay through the real video::ClipFrameCache.
    video::ClipFrameCache cache(root, "test-clip");
    std::shared_ptr<video::Frame> frame;
    std::string err;
    CHECK(cache.ReadFrame(0, &frame, &err), "sink: replay");
    CHECK(frame && frame->Width() == 4 && frame->Pts() == 33000,
          "sink: frame metadata");
    std::filesystem::remove_all(root);
}

// ---- ui/css: style engine (YA.DesginCSS) ----

void TestStyleEngine() {
    namespace C = XER::UI::CSS;
    struct El final : C::CSSElement {
        std::string tag, id;
        std::vector<std::string> classes;
        const C::CSSElement* parent = nullptr;
        std::string_view TagName() const override { return tag; }
        std::string_view Id() const override { return id; }
        bool HasClass(std::string_view n) const override {
            for (const auto& c : classes) if (c == n) return true;
            return false;
        }
        bool HasAttribute(std::string_view) const override { return false; }
        const C::CSSElement* Parent() const override { return parent; }
    };

    C::StyleSheet sheet;
    C::StyleRule r1;
    r1.selector = C::ParseSelector("div");
    r1.declarations.push_back({"color", "red"});
    C::StyleRule r2;
    r2.selector = C::ParseSelector(".c1 div.c2");
    r2.declarations.push_back({"color", "blue"});
    r2.declarations.push_back({"width", "10px"});
    sheet.rules = {r1, r2};

    C::StyleEngine engine;
    engine.AppendActiveStyleSheets(sheet);

    El parent;
    parent.tag = "span";
    parent.classes = {"c1"};
    El child;
    child.tag = "div";
    child.classes = {"c2"};
    child.parent = &parent;

    const auto style = engine.StyleForElement(child);
    const auto* color = style.Get("color");
    CHECK(color && *color == "blue", "css: descendant match");
    const auto* width = style.Get("width");
    CHECK(width && *width == "10px", "css: decl applied");
    // A div with only .c2 (no .c1 ancestor) falls back to tag rule.
    El orphan;
    orphan.tag = "div";
    orphan.classes = {"c2"};
    const auto s2 = engine.StyleForElement(orphan);
    const auto* c2 = s2.Get("color");
    CHECK(c2 && *c2 == "red", "css: tag fallback");
}

// ---- ui/css: invalidation pipeline (design doc example) ----

void TestStyleInvalidation() {
    namespace C = XER::UI::CSS;
    auto root = std::make_unique<C::CSSNode>();
    root->tag = "body";
    auto* parent = root->AddChild(std::make_unique<C::CSSNode>());
    parent->tag = "span";
    parent->classes = {"c1"};  // the class "added" by this DOM change
    auto* target = parent->AddChild(std::make_unique<C::CSSNode>());
    target->tag = "div";
    target->classes = {"c2"};
    auto* other = root->AddChild(std::make_unique<C::CSSNode>());
    other->tag = "p";

    C::TreeScope scope(root.get());
    C::StyleSheet sheet;
    C::StyleRule rule;
    rule.selector = C::ParseSelector(".c1 div.c2");
    rule.declarations.push_back({"color", "blue"});
    sheet.rules = {rule};
    scope.Resolver().AppendActiveStyleSheets(sheet);

    // Design doc: adding class c1 to an element → pending invalidation
    // set for the ancestor key; descendants matching self-features
    // (class c2) get needs_recalc; unrelated nodes don't.
    const auto& sets =
        scope.Resolver().InvalidationSetsForClassChange("c1");
    CHECK(!sets.empty(), "inv: sets for ancestor class");
    scope.Pending().ScheduleInvalidationSetsForNode(
        parent, std::vector<C::InvalidationSet>(sets));

    std::vector<std::pair<C::CSSNode*, C::ComputedStyle>> recalced;
    C::DocumentStyle doc(scope);
    doc.UpdateStyleAndLayoutTree(&recalced);
    CHECK(recalced.size() == 1 && recalced[0].first == target,
          "inv: only c2 descendant recalced");
    CHECK(!other->needs_recalc, "inv: unrelated untouched");
    const auto* color = recalced.empty() ? nullptr
                                         : recalced[0].second.Get("color");
    CHECK(color && *color == "blue", "inv: recalced style applied");
}

// ---- ui/css: stylesheet text parser + pseudo/attr/nth selectors ----

void TestStyleSheetParser() {
    namespace C = XER::UI::CSS;
    auto sheet = C::ParseStyleSheet(
        "/* hi */ div.a, #x:hover { color: red; width: 2px } "
        "ul li:first-child { top: 0 } "
        "input[type=text] { border: 1 } "
        "p:nth-child(2n+1) { z: 9 }");
    CHECK(sheet.rules.size() == 5, "parse: rule count");

    auto root = std::make_unique<C::CSSNode>();
    root->tag = "div";
    auto* a = root->AddChild(std::make_unique<C::CSSNode>());
    a->tag = "div";
    a->classes = {"a"};
    auto* x = root->AddChild(std::make_unique<C::CSSNode>());
    x->tag = "span";
    x->id = "x";
    x->hovered = true;
    auto* inp = root->AddChild(std::make_unique<C::CSSNode>());
    inp->tag = "input";
    inp->attr_values["type"] = "text";

    C::StyleEngine eng;
    eng.AppendActiveStyleSheets(sheet);
    const auto s1 = eng.StyleForElement(*a);
    CHECK(s1.Get("color") && *s1.Get("color") == "red", "parse: .a");
    const auto s2 = eng.StyleForElement(*x);
    CHECK(s2.Get("width") && *s2.Get("width") == "2px",
          "parse: #x:hover");
    CHECK(s2.AffectedByHover(), "parse: AffectedByHover flag");
    const auto s3 = eng.StyleForElement(*inp);
    CHECK(s3.Get("border") && *s3.Get("border") == "1",
          "parse: [type=text]");

    auto ul = std::make_unique<C::CSSNode>();
    ul->tag = "ul";
    auto* li1 = ul->AddChild(std::make_unique<C::CSSNode>());
    li1->tag = "li";
    auto* li2 = ul->AddChild(std::make_unique<C::CSSNode>());
    li2->tag = "li";
    const auto s4 = eng.StyleForElement(*li1);
    CHECK(s4.Get("top") && *s4.Get("top") == "0", "parse: first-child");
    const auto s5 = eng.StyleForElement(*li2);
    CHECK(!s5.Get("top"), "parse: nth index distinguishes");

    auto p = std::make_unique<C::CSSNode>();
    p->tag = "p";
    auto* p1 = p->AddChild(std::make_unique<C::CSSNode>());
    p1->tag = "p";
    auto* p2 = p->AddChild(std::make_unique<C::CSSNode>());
    p2->tag = "p";
    auto* p3 = p->AddChild(std::make_unique<C::CSSNode>());
    p3->tag = "p";
    CHECK(eng.StyleForElement(*p1).Get("z") &&
              *eng.StyleForElement(*p1).Get("z") == "9",
          "parse: nth-child odd 1");
    CHECK(!eng.StyleForElement(*p2).Get("z"),
          "parse: nth-child odd 2 no");
    CHECK(eng.StyleForElement(*p3).Get("z"), "parse: nth-child odd 3");
}

// ---- demuxer: BufferedDataSource windowing ----

void TestBufferedDataSource() {
    namespace M = XER::Media;
    std::string err;
    auto* raw = M::DataSource::CreateFileSource(
        "D:/Developers/XER3/CMakeLists.txt", &err);
    if (!raw) {
        CHECK(false, "bds: file source open");
        return;
    }
    M::BufferedDataSource bds(
        std::unique_ptr<M::DataSource>(raw), 64, 16);
    std::uint8_t buf[64];
    std::size_t n = 0;
    CHECK(bds.Read(buf, 8, &n) == M::BufferedDataSource::Status::kOk &&
              n == 8,
          "bds: first read");
    CHECK(bds.position() == 8, "bds: cursor advanced");
    CHECK(bds.Read(buf, 16, &n) == M::BufferedDataSource::Status::kOk &&
              n == 16,
          "bds: second read in window");
    // Backward seek inside the window is served without re-read.
    bds.Seek(0);
    CHECK(bds.Read(buf, 4, &n) == M::BufferedDataSource::Status::kOk &&
              n == 4,
          "bds: backward seek read");
    // Forward seek beyond window falls back to ranged reads.
    bds.Seek(1 << 20);
    const auto st = bds.Read(buf, 4, &n);
    CHECK(st == M::BufferedDataSource::Status::kEos ||
              st == M::BufferedDataSource::Status::kOk,
          "bds: out-of-range tolerated");
}

int main() {
    TestAudioFocus();
    TestMuxer();
    TestCodecNames();
    TestRouter();
    TestDecoderStream();
    TestFFmpegDemuxer();
    TestAudioFifoAndMixer();
    TestSeekableBuffer();
    TestLiveWebmMuxer();
    TestDecoderBufferQueue();
    TestAudioClock();
    TestClearKey();
    TestAsyncDataSource();
    TestSyncChannel();
    TestEngineStream();
    TestFrameCacheSink();
    TestStyleEngine();
    TestStyleInvalidation();
    TestStyleSheetParser();
    TestBufferedDataSource();
    if (failures == 0) {
        std::printf("media smoke: OK\n");
        return 0;
    }
    std::printf("media smoke: %d failure(s)\n", failures);
    return 1;
}
