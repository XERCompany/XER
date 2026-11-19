// xer_engine_ui_render.exe — remote UI render worker.
//
// A UI application renders through Rendering::StreamRenderer, which
// serializes every draw call onto a Cache::SharedMemoryChannel. This
// process replays the stream onto a real Direct2DRenderer backed by its
// own render-surface window, so UI event handling / widget logic and GPU
// presentation run in separate processes.
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
#include <cstdlib>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include "cache/shared_memory_channel.h"
#include "rendering/render_stream.h"
#include "rendering/windows/direct2d_renderer.h"

namespace {

using XER::Cache::SharedMemoryChannel;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::TextStyle;
using XER::UI::Rendering::Windows::Direct2DRenderer;
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
    // an own hidden surface when the foreign handle is rejected.
    HWND target = foreign_hwnd;
    if (target == nullptr || !IsWindow(target)) {
        target = nullptr;
    }
    std::unique_ptr<Direct2DRenderer> renderer;
    if (target != nullptr) {
        try {
            renderer = std::make_unique<Direct2DRenderer>(target);
        } catch (...) {
            renderer.reset();
            target = nullptr;
        }
    }
    if (target == nullptr) {
        // No usable foreign HWND: own surface, hidden unless --surface.
        target = CreateSurfaceWindow(width, height, !surface);
        if (target == nullptr) {
            std::fprintf(stderr, "render surface window failed\n");
            return 1;
        }
        try {
            renderer = std::make_unique<Direct2DRenderer>(target);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "renderer init failed: %s\n",
                         e.what());
            return 1;
        }
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
        if (op == Stream::Op::kMeasureRequest) {
            std::uint32_t req_id = 0;
            std::string text;
            TextStyle style{};
            if (Stream::StreamReader::ParseMeasureRequest(
                    message.data(),
                    static_cast<uint32_t>(message.size()), &req_id,
                    &text, &style)) {
                const auto size = renderer->MeasureText(text, style);
                std::vector<uint8_t> reply;
                Stream::StreamWriter::MeasureResponse(reply, req_id,
                                                      size);
                channel->Send(reply.data(),
                              static_cast<uint32_t>(reply.size()), 50);
            }
            continue;
        }
        Stream::StreamReader::Handlers handlers;
        handlers.on_shutdown = [&running] { running = false; };
        Stream::StreamReader::Dispatch(*renderer, handlers,
                                       message.data(),
                                       static_cast<uint32_t>(
                                           message.size()));
    }
    std::fprintf(stderr, "xer_engine_ui_render: stream ended\n");
    return 0;
}
