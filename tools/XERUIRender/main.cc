// xer_engine_ui_render.exe — remote UI render worker.
//
// A UI application renders through Rendering::StreamRenderer, which
// serializes every draw call onto a Cache::SharedMemoryChannel. This
// process replays the stream onto a real Renderer (selected through the
// Rendering::CreateRenderer provider chain — XER_UI_RENDERER honored,
// Direct2D by default) backed by its own render-surface window, so UI
// event handling / widget logic and GPU presentation run in separate
// processes.
//
//   xer_engine_ui_render.exe --channel <name> [--create]
//                            [--hwnd <hex>] [--width N] [--height N]
//                            [--surface]
//
// The worker is a background process: with no --hwnd it renders into a
// hidden surface (--surface makes it visible for debugging). --hwnd
// lets it present directly into a window owned by the UI process
// (same-session cross-process HWND), which is the normal mode when the
// UI system spawns several render workers behind its windows.
//
// --create makes this process the channel host; otherwise it opens the
// channel created by the UI app. MeasureText requests are answered
// synchronously so layout metrics match exactly what a local renderer
// would return.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include "cache/shared_memory_channel.h"
#include "rendering/render_store.h"
#include "rendering/render_stream.h"
#include "rendering/renderer_provider.h"

namespace {

using XER::Cache::SharedMemoryChannel;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::RenderStore;
using XER::UI::Rendering::TextStyle;
namespace Stream = XER::UI::Rendering::Stream;

constexpr wchar_t kClassName[] = L"XER_UI_RENDER_SURFACE";

LRESULT CALLBACK SurfaceProc(HWND hwnd, UINT msg, WPARAM wParam,
                             LPARAM lParam) {
    if (msg == WM_DESTROY) {
        // The surface is closed when the stream ends; PostQuitMessage is
        // issued by the pump loop, not here, so the channel drain still
        // completes cleanly.
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HWND CreateSurfaceWindow(int width, int height, bool hidden) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SurfaceProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD ex = hidden ? WS_EX_TOOLWINDOW : 0;
    HWND hwnd = CreateWindowExW(ex, kClassName, L"XER Render Surface",
                                style, CW_USEDEFAULT, CW_USEDEFAULT,
                                width, height, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (hwnd != nullptr && !hidden) {
        ShowWindow(hwnd, SW_SHOW);
    }
    return hwnd;
}

void PumpWindowMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Usage() {
    std::fprintf(stderr,
                 "usage: xer_engine_ui_render --channel <name> "
                 "[--create] [--width N] [--height N] [--hidden]\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::string channel_name;
    bool create = false, surface = false;
    HWND foreign_hwnd = nullptr;
    int width = 800, height = 600;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--channel" && i + 1 < argc) {
            channel_name = argv[++i];
        } else if (a == "--create") {
            create = true;
        } else if (a == "--surface") {
            surface = true;
        } else if (a == "--hwnd" && i + 1 < argc) {
            foreign_hwnd = reinterpret_cast<HWND>(static_cast<uintptr_t>(
                std::strtoull(argv[++i], nullptr, 0)));
        } else if (a == "--width" && i + 1 < argc) {
            width = std::atoi(argv[++i]);
        } else if (a == "--height" && i + 1 < argc) {
            height = std::atoi(argv[++i]);
        } else if (a == "--help" || a == "-h") {
            Usage();
            return 0;
        }
    }
    if (channel_name.empty()) {
        Usage();
        return 1;
    }

    // Prefer drawing straight into the UI window's HWND; fall back to
    // an own hidden surface when the foreign handle is rejected. The
    // same path runs on kSetTargetHwnd retargets (pool workers adopt a
    // window when the UI app checks them out).
    // The provider honors XER_UI_RENDERER (inherited from the UI
    // process) and walks the fallback chain to a working backend —
    // Direct2D by default, Vulkan when requested, software last.
    std::unique_ptr<Renderer> renderer;
    HWND own_surface = nullptr;
    auto retarget = [&](HWND foreign, int w, int h, bool show) -> bool {
        HWND target = foreign;
        if (target == nullptr || !IsWindow(target)) {
            target = nullptr;
        }
        if (target != nullptr) {
            try {
                renderer =
                    XER::UI::Rendering::CreateRendererFromEnv(target);
            } catch (...) {
                renderer.reset();
            }
            if (renderer != nullptr) {
                return true;
            }
            target = nullptr;
        }
        // No usable foreign HWND: own surface, hidden unless shown.
        if (own_surface == nullptr) {
            own_surface = CreateSurfaceWindow(w, h, !show);
            if (own_surface == nullptr) {
                std::fprintf(stderr, "render surface window failed\n");
                return false;
            }
        }
        try {
            renderer = XER::UI::Rendering::CreateRendererFromEnv(
                own_surface);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "renderer init failed: %s\n",
                         e.what());
            return false;
        }
        return renderer != nullptr;
    };
    if (!retarget(foreign_hwnd, width, height, surface)) {
        return 1;
    }

    std::string err;
    std::unique_ptr<SharedMemoryChannel> channel =
        create ? SharedMemoryChannel::Create(channel_name, 1 << 20, &err)
               : SharedMemoryChannel::Open(channel_name, &err);
    if (!channel) {
        std::fprintf(stderr, "channel: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stderr, "xer_engine_ui_render: channel '%s' %s\n",
                 channel_name.c_str(), create ? "created" : "opened");

    // Shared pixel store — attached by the UI process via
    // kAttachStore; DrawResource/DrawVideoFrame/DrawSpectrum resolve
    // resource ids here instead of pushing pixels through the channel.
    std::unique_ptr<RenderStore> store;

    bool running = true;
    std::vector<uint8_t> message;
    while (running) {
        PumpWindowMessages();
        if (!channel->Recv(&message, 50)) {
            if (channel->IsClosed()) {
                break;
            }
            continue;
        }
        Stream::Op op;
        if (!Stream::StreamReader::PeekOp(message.data(),
                                          static_cast<uint32_t>(
                                              message.size()),
                                          &op)) {
            continue;
        }
        if (op == Stream::Op::kSetTargetHwnd) {
            // [u32 op][u32 lo][u32 hi] — retarget onto a foreign HWND
            // (0 → own hidden surface).
            if (message.size() >= 12) {
                std::uint32_t lo = 0, hi = 0;
                std::memcpy(&lo, message.data() + 4, 4);
                std::memcpy(&hi, message.data() + 8, 4);
                const auto v = static_cast<std::uint64_t>(lo) |
                               (static_cast<std::uint64_t>(hi) << 32);
                retarget(reinterpret_cast<HWND>(
                             static_cast<uintptr_t>(v)),
                         width, height, surface);
            }
            continue;
        }
        if (!renderer) {
            continue;  // retarget failed; wait for the next command
        }
        if (op == Stream::Op::kMeasureRequest) {
            std::uint32_t req_id = 0;
            std::string text;
            TextStyle style{};
            if (Stream::StreamReader::ParseMeasureRequest(
                    message.data(),
                    static_cast<uint32_t>(message.size()), &req_id,
                    &text, &style)) {
                try {
                    const auto size =
                        renderer->MeasureText(text, style);
                    std::vector<uint8_t> reply;
                    Stream::StreamWriter::MeasureResponse(reply,
                                                          req_id, size);
                    channel->Send(reply.data(),
                                  static_cast<uint32_t>(reply.size()),
                                  50);
                } catch (const std::exception& e) {
                    std::fprintf(stderr,
                                 "xer_engine_ui_render: measure: %s\n",
                                 e.what());
                } catch (...) {
                    std::fprintf(
                        stderr,
                        "xer_engine_ui_render: measure failed\n");
                }
            }
            continue;
        }
        Stream::StreamReader::Handlers handlers;
        handlers.on_shutdown = [&running] { running = false; };
        handlers.on_retarget = [&](std::uint64_t hwnd) {
            retarget(reinterpret_cast<HWND>(
                         static_cast<uintptr_t>(hwnd)),
                     width, height, surface);
        };
        handlers.on_attach_store = [&](std::string_view name) {
            std::string err;
            store = RenderStore::Open(std::string(name), &err);
            if (!store) {
                std::fprintf(stderr,
                             "xer_engine_ui_render: store '%.*s': "
                             "%s\n",
                             static_cast<int>(name.size()),
                             name.data(), err.c_str());
            }
        };
        handlers.on_resource =
            [&store](std::uint32_t id,
                     Stream::StreamReader::Handlers::ResourceMeta*
                         meta) -> const void* {
            if (!store || meta == nullptr) {
                return nullptr;
            }
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
        // Renderer calls throw (RequireFrame on a draw outside a
        // frame, ThrowIfFailed on D2D errors): one bad op must drop
        // the frame, not the worker — log and keep pumping.
        try {
            Stream::StreamReader::Dispatch(*renderer, handlers,
                                           message.data(),
                                           static_cast<uint32_t>(
                                               message.size()));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "xer_engine_ui_render: dispatch: %s\n",
                         e.what());
        } catch (...) {
            std::fprintf(stderr,
                         "xer_engine_ui_render: dispatch failed\n");
        }
    }
    std::fprintf(stderr, "xer_engine_ui_render: stream ended\n");
    return 0;
}
