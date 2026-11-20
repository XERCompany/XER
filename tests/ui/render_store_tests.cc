// RenderStore + extended render-stream coverage: the shared-memory
// resource store round-trips image/video/spectrum payloads, and the
// new stream ops (resource draws, layers, transforms, spectrum)
// serialize and dispatch correctly through StreamReader/Writer.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rendering/renderer.h"
#include "rendering/render_store.h"
#include "rendering/render_stream.h"
#include "test_harness.h"

namespace {

using XER::UI::Layout::Rect;
using XER::UI::Rendering::RenderStore;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::TextStyle;
using XER::UI::Rendering::StreamRenderer;
using XER::UI::Theme::Color;
namespace Stream = XER::UI::Rendering::Stream;

// Renderer that records the calls the dispatch layer makes.
class FakeRenderer final : public Renderer {
public:
    std::vector<std::string> log;
    Rect last_rect{};
    Color last_color{};
    float last_opacity = -1.f;
    float last_radius = -1.f;
    std::uint32_t last_id = 0;
    std::uint32_t last_mode = 0;
    int draw_calls = 0;
    int layer_pushes = 0;
    int layer_pops = 0;
    float tf[6] = {};

    void BeginFrame(Rect) override {}
    void EndFrame() override {}
    void FillRect(Rect r, Color c, float radius) override {
        last_rect = r;
        last_color = c;
        last_radius = radius;
        ++draw_calls;
    }
    void StrokeRect(Rect, Color, float, float) override {}
    void DrawLine(XER::UI::Layout::Point, XER::UI::Layout::Point, Color,
                  float) override {}
    void DrawText(std::string_view, Rect, TextStyle) override {}
    XER::UI::Layout::Size MeasureText(std::string_view,
                                      TextStyle) const override {
        return {};
    }
    void PushLayer(float opacity, std::uint32_t blend,
                   float blur) override {
        last_opacity = opacity;
        last_mode = blend;
        last_radius = blur;
        ++layer_pushes;
    }
    void PopLayer() override { ++layer_pops; }
    void SetTransform(float a, float b, float c, float d, float tx,
                      float ty) override {
        tf[0] = a; tf[1] = b; tf[2] = c;
        tf[3] = d; tf[4] = tx; tf[5] = ty;
        log.push_back("transform");
    }
    void ResetTransform() override { log.push_back("reset_tf"); }
    bool DrawResourcePixels(const void* bgra, std::uint32_t id,
                            int width, int height, Rect dst,
                            float opacity,
                            float corner_radius) override {
        last_id = id;
        last_rect = dst;
        last_opacity = opacity;
        last_radius = corner_radius;
        last_color = bgra ? Color{1, 1, 1, 1} : Color{};
        (void)width;
        (void)height;
        log.push_back("res");
        return true;
    }
    bool DrawVideoPixels(const void* bgra, std::uint32_t id, int width,
                         int height, Rect dst, float opacity,
                         float corner_radius) override {
        last_id = id;
        last_rect = dst;
        log.push_back("video");
        return bgra != nullptr && width > 0 && height > 0 &&
               opacity > 0.f && corner_radius >= 0.f;
    }
    bool DrawSpectrumPixels(const float* levels, std::uint32_t count,
                            Rect dst, Color color,
                            std::uint32_t mode) override {
        last_id = count;
        last_mode = mode;
        last_rect = dst;
        last_color = color;
        log.push_back("spectrum");
        return levels != nullptr;
    }
};

Stream::StreamReader::Handlers::ResourceMeta* g_meta_out = nullptr;

std::unique_ptr<RenderStore> MakeStore(std::string* err) {
    static int n = 0;
    char name[64];
    std::snprintf(name, sizeof(name), "xer.test.store.%d", ++n);
    return RenderStore::Create(name, 1 << 20, err);
}

Stream::StreamReader::Handlers StoreHandlers(
    RenderStore* store,
    std::string* attached = nullptr) {
    Stream::StreamReader::Handlers h;
    if (attached) {
        h.on_attach_store = [attached](std::string_view name) {
            *attached = std::string(name);
        };
    }
    if (store) {
        h.on_resource =
            [store](std::uint32_t id,
                    Stream::StreamReader::Handlers::ResourceMeta* meta)
            -> const void* {
            const auto v = store->Read(id);
            if (v.data == nullptr) {
                return nullptr;
            }
            meta->width = v.info.width;
            meta->height = v.info.height;
            meta->payload = v.info.payload;
            meta->kind = static_cast<std::uint32_t>(v.info.kind);
            return v.data;
        };
    }
    return h;
}

XER_UI_TEST(RenderStore_WriteReadRoundTrip) {
    std::string err;
    auto store = MakeStore(&err);
    XER::UI::Test::Expect(store != nullptr, "store created", __FILE__,
                          __LINE__);
    if (!store) {
        return;
    }
    const std::uint8_t px[16] = {1, 2, 3, 4, 5, 6, 7, 8,
                                 9, 10, 11, 12, 13, 14, 15, 16};
    XER::UI::Test::Expect(
        store->Write(7, RenderStore::Kind::kImageBgra, 2, 2, px, 16),
        "write", __FILE__, __LINE__);
    const auto v = store->Read(7);
    XER::UI::Test::Expect(v.data != nullptr, "read", __FILE__,
                          __LINE__);
    XER::UI::Test::Expect(v.info.kind == RenderStore::Kind::kImageBgra &&
                              v.info.width == 2 &&
                              v.info.payload == 16,
                          "meta", __FILE__, __LINE__);
    XER::UI::Test::Expect(
        std::memcmp(v.data, px, 16) == 0, "bytes", __FILE__, __LINE__);
    XER::UI::Test::Expect(store->Read(99).data == nullptr,
                          "missing id", __FILE__, __LINE__);
    store->Free(7);
    XER::UI::Test::Expect(store->Read(7).data == nullptr, "freed",
                          __FILE__, __LINE__);
}

XER_UI_TEST(RenderStore_VideoFrameSeqlock) {
    std::string err;
    auto store = MakeStore(&err);
    if (!store) return;
    std::vector<std::uint8_t> frame(4 * 4 * 4, 0xAA);
    XER::UI::Test::Expect(
        store->Write(1, RenderStore::Kind::kVideoFrame, 4, 4,
                     frame.data(),
                     static_cast<std::uint32_t>(frame.size())),
        "frame 1", __FILE__, __LINE__);
    const auto s1 = store->Seq(1);
    std::fill(frame.begin(), frame.end(), 0xBB);
    store->Write(1, RenderStore::Kind::kVideoFrame, 4, 4, frame.data(),
                 static_cast<std::uint32_t>(frame.size()));
    XER::UI::Test::Expect(store->Seq(1) > s1, "seq bumped", __FILE__,
                          __LINE__);
    const auto v = store->Read(1);
    XER::UI::Test::Expect(
        v.data != nullptr &&
            static_cast<const std::uint8_t*>(v.data)[0] == 0xBB,
        "latest bytes", __FILE__, __LINE__);
    // Reused slot — no fresh arena allocation for same-size frames.
    XER::UI::Test::Expect(store->Read(1).info.payload == frame.size(),
                          "payload stable", __FILE__, __LINE__);
}

XER_UI_TEST(RenderStream_ResourceOpsDispatch) {
    std::string err;
    auto store = MakeStore(&err);
    if (!store) return;
    const std::uint8_t px[4 * 4 * 4] = {0};
    store->Write(3, RenderStore::Kind::kImageBgra, 4, 4, px, sizeof(px));

    FakeRenderer renderer;
    std::string attached;
    auto handlers = StoreHandlers(store.get(), &attached);

    std::vector<std::uint8_t> msg;
    Stream::StreamWriter::AttachStore(msg, "xer.ui.res.1234");
    Stream::StreamReader::Dispatch(renderer, handlers, msg.data(),
                                   static_cast<std::uint32_t>(
                                       msg.size()));
    XER::UI::Test::ExpectEqual(std::string("xer.ui.res.1234"), attached,
                               "attach name", __FILE__, __LINE__);

    const Rect dst{10, 20, 100, 50};
    Stream::StreamWriter::DrawResource(msg, 3, dst, 0.8f, 6.0f);
    Stream::StreamReader::Dispatch(renderer, handlers, msg.data(),
                                   static_cast<std::uint32_t>(
                                       msg.size()));
    XER::UI::Test::ExpectEqual(std::string("res"), renderer.log.back(),
                               "draw res", __FILE__, __LINE__);
    XER::UI::Test::Expect(renderer.last_id == 3 &&
                              renderer.last_rect.width == 100 &&
                              renderer.last_opacity == 0.8f &&
                              renderer.last_radius == 6.0f,
                          "res args", __FILE__, __LINE__);

    // Video path: rewrite the slot as a frame, draw via id.
    store->Write(3, RenderStore::Kind::kVideoFrame, 4, 4, px,
                 sizeof(px));
    Stream::StreamWriter::DrawVideoFrame(msg, 3, dst, 1.0f, 0.0f);
    Stream::StreamReader::Dispatch(renderer, handlers, msg.data(),
                                   static_cast<std::uint32_t>(
                                       msg.size()));
    XER::UI::Test::ExpectEqual(std::string("video"),
                               renderer.log.back(), "video op",
                               __FILE__, __LINE__);

    // Missing id resolves to a no-op (consumed, no draw).
    const auto calls_before = renderer.log.size();
    Stream::StreamWriter::DrawVideoFrame(msg, 77, dst, 1.0f, 0.0f);
    Stream::StreamReader::Dispatch(renderer, handlers, msg.data(),
                                   static_cast<std::uint32_t>(
                                       msg.size()));
    XER::UI::Test::Expect(renderer.log.size() == calls_before,
                          "missing id skipped", __FILE__, __LINE__);
}

XER_UI_TEST(RenderStream_LayerTransformSpectrum) {
    std::string err;
    auto store = MakeStore(&err);
    if (!store) return;
    const float levels[4] = {0.2f, 0.6f, 1.0f, 0.4f};
    store->Write(9, RenderStore::Kind::kSpectrumF32, 4, 1, levels,
                 sizeof(levels));

    FakeRenderer renderer;
    auto handlers = StoreHandlers(store.get());
    std::vector<std::uint8_t> msg;
    const auto bytes = [&] { return static_cast<std::uint32_t>(msg.size()); };

    Stream::StreamWriter::PushLayer(msg, 0.5f, 0, 3.0f);
    Stream::StreamReader::Dispatch(renderer, handlers, msg.data(),
                                   bytes());
    Stream::StreamWriter::PopLayer(msg);
    Stream::StreamReader::Dispatch(renderer, handlers, msg.data(),
                                   bytes());
    XER::UI::Test::Expect(renderer.layer_pushes == 1 &&
                              renderer.layer_pops == 1 &&
                              renderer.last_opacity == 0.5f &&
                              renderer.last_radius == 3.0f,
                          "layer", __FILE__, __LINE__);

    Stream::StreamWriter::SetTransform(msg, 2.f, 0.f, 0.f, 2.f, 5.f,
                                       7.f);
    Stream::StreamReader::Dispatch(renderer, handlers, msg.data(),
                                   bytes());
    Stream::StreamWriter::ResetTransform(msg);
    Stream::StreamReader::Dispatch(renderer, handlers, msg.data(),
                                   bytes());
    XER::UI::Test::Expect(renderer.tf[0] == 2.f &&
                              renderer.tf[3] == 2.f &&
                              renderer.tf[4] == 5.f &&
                              renderer.tf[5] == 7.f &&
                              renderer.log.back() == "reset_tf",
                          "transform", __FILE__, __LINE__);

    const Rect dst{0, 0, 80, 30};
    const Color green{0.f, 1.f, 0.f, 1.f};
    Stream::StreamWriter::DrawSpectrum(msg, 9, dst, green, 2);
    Stream::StreamReader::Dispatch(renderer, handlers, msg.data(),
                                   bytes());
    XER::UI::Test::ExpectEqual(std::string("spectrum"),
                               renderer.log.back(), "spectrum op",
                               __FILE__, __LINE__);
    XER::UI::Test::Expect(renderer.last_id == 4 &&
                              renderer.last_mode == 2 &&
                              renderer.last_color.g == 1.f,
                          "spectrum args", __FILE__, __LINE__);
}

XER_UI_TEST(StreamRenderer_EmitsNewOps) {
    std::vector<std::uint8_t> sent;
    const auto grab = [&](std::vector<std::uint8_t>&& m) {
        sent.insert(sent.end(), m.begin(), m.end());
        return true;
    };
    StreamRenderer r(grab,
                     [](std::vector<uint8_t>&&, std::uint32_t) {
                         return XER::UI::Layout::Size{};
                     });
    r.AttachStore("xer.ui.res.1");
    r.PushLayer(0.5f, 0, 1.0f);
    r.SetTransform(1, 0, 0, 1, 2, 3);
    const bool drew = r.DrawResource(5, {0, 0, 10, 10}, 1.0f, 0.0f);
    r.DrawVideoFrame(6, {0, 0, 10, 10}, 1.0f, 0.0f);
    r.DrawSpectrum(7, {0, 0, 10, 10}, {0, 1, 0, 1}, 0);
    r.FreeResource(5);
    r.ResetTransform();
    r.PopLayer();
    XER::UI::Test::Expect(drew, "emit ok", __FILE__, __LINE__);
    // First 4 bytes = op id.
    std::uint32_t op = 0;
    std::memcpy(&op, sent.data(), 4);
    XER::UI::Test::Expect(op == static_cast<std::uint32_t>(
                                  Stream::Op::kAttachStore),
                          "first op attach", __FILE__, __LINE__);
    XER::UI::Test::Expect(sent.size() > 60, "all ops emitted",
                          __FILE__, __LINE__);
}

}  // namespace
