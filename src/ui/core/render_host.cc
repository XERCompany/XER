#include "render_host.h"

#include <cstdio>
#include <cstdlib>

#include "cache/shared_memory_channel.h"
#include "rendering/render_stream.h"

#ifdef _WIN32
#include <windows.h>

#include "helper/string_utils.h"

namespace XER::UI::Core {
namespace {

constexpr wchar_t kWorkerExe[] = L"xer_engine_ui_render.exe";
constexpr uint32_t kChannelCapacity = 1u << 20;

std::string ChannelName(int channel_id) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "xer.ui.render.%lu.%d",
                  static_cast<unsigned long>(GetCurrentProcessId()),
                  channel_id);
    return buf;
}

}  // namespace

UiRenderHost::~UiRenderHost() { ShutdownAll(); }

UiRenderHost& UiRenderHost::Instance() {
    static UiRenderHost host;
    return host;
}

std::string UiRenderHost::ExePath() const {
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        return {};
    }
    std::wstring dir = path;
    const auto slash = dir.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    return Helper::String::WideToUtf8(
        dir.substr(0, slash + 1) + kWorkerExe);
}

bool UiRenderHost::SpawnWorker(const std::wstring& cmd,
                               std::string* error_out) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;
    // Background worker: no console window, no handle inheritance.
    if (!CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &pi)) {
        if (error_out) {
            *error_out = "CreateProcess failed (" +
                         std::to_string(GetLastError()) + ")";
        }
        return false;
    }
    CloseHandle(pi.hThread);
    ChannelEntry& entry = channels_.back();
    entry.worker_process = pi.hProcess;
    entry.worker_pid = pi.dwProcessId;
    return true;
}

bool UiRenderHost::EnsureWorkers(int count, std::string* error_out,
                                 const std::string&) {
    // Workers are per-channel: each remote renderer owns one hidden
    // render process (spawned in CreateRemoteRenderer). "Ensuring" N
    // workers is therefore a no-op capacity hint — kept for API
    // symmetry; actual processes appear on demand.
    (void)count;
    (void)error_out;
    return true;
}

int UiRenderHost::WorkerCount() const {
    int live = 0;
    for (const auto& e : channels_) {
        if (e.worker_process != nullptr &&
            WaitForSingleObject(e.worker_process, 0) == WAIT_TIMEOUT) {
            ++live;
        }
    }
    return live;
}

std::unique_ptr<Rendering::StreamRenderer>
UiRenderHost::CreateRemoteRenderer(uintptr_t target_hwnd,
                                   std::string* error_out) {
    const std::string exe = ExePath();
    if (exe.empty()) {
        if (error_out) {
            *error_out = "cannot locate xer_engine_ui_render.exe";
        }
        return nullptr;
    }
    const int id = next_channel_id_++;
    const std::string name = ChannelName(id);
    std::string err;
    auto channel =
        Cache::SharedMemoryChannel::Create(name, kChannelCapacity, &err);
    if (!channel) {
        if (error_out) *error_out = std::move(err);
        return nullptr;
    }
    ChannelEntry entry;
    entry.channel = std::move(channel);
    channels_.push_back(std::move(entry));

    // xer_engine_ui_render --channel <name> [--hwnd <hex>] [--surface]
    std::wstring cmd = L"\"" + Helper::String::Utf8ToWide(exe) +
                       L"\" --channel " +
                       Helper::String::Utf8ToWide(name);
    if (target_hwnd != 0) {
        wchar_t hwnd_arg[32];
        std::swprintf(hwnd_arg, 32, L" --hwnd 0x%Ix", target_hwnd);
        cmd += hwnd_arg;
    }
    if (std::getenv("XER_UI_RENDER_SURFACE") != nullptr) {
        cmd += L" --surface";
    }
    if (!SpawnWorker(cmd, error_out)) {
        channels_.pop_back();
        return nullptr;
    }

    // The shared channel outlives the renderer (host keeps a ref), so
    // captured pointers stay valid while the host or any renderer
    // holds it.
    auto ch_ptr = channels_.back().channel;
    auto send = [ch_ptr](std::vector<uint8_t>&& msg) {
        return ch_ptr->Send(msg.data(),
                            static_cast<uint32_t>(msg.size()), 100);
    };
    auto request = [ch_ptr](std::vector<uint8_t>&& msg,
                            std::uint32_t req_id) -> Layout::Size {
        ch_ptr->Send(msg.data(), static_cast<uint32_t>(msg.size()),
                     500);
        const uint64_t deadline = GetTickCount64() + 2000;
        std::vector<uint8_t> reply;
        while (GetTickCount64() < deadline) {
            if (!ch_ptr->Recv(&reply, 100)) {
                if (ch_ptr->IsClosed()) break;
                continue;
            }
            std::uint32_t rid = 0;
            Layout::Size size{};
            if (Rendering::Stream::StreamReader::ParseMeasureResponse(
                    reply.data(), static_cast<uint32_t>(reply.size()),
                    &rid, &size) &&
                rid == req_id) {
                return size;
            }
        }
        return {0.0f, 0.0f};
    };
    return std::make_unique<Rendering::StreamRenderer>(
        std::move(send), std::move(request));
}

void UiRenderHost::ShutdownAll() {
    for (auto& entry : channels_) {
        if (entry.channel) {
            std::vector<uint8_t> bye;
            Rendering::Stream::StreamWriter::Shutdown(bye);
            entry.channel->Send(bye.data(),
                                static_cast<uint32_t>(bye.size()), 50);
            entry.channel->SignalClose();
        }
        if (entry.worker_process != nullptr) {
            if (WaitForSingleObject(entry.worker_process, 1200) ==
                WAIT_TIMEOUT) {
                TerminateProcess(entry.worker_process, 0);
            }
            CloseHandle(entry.worker_process);
            entry.worker_process = nullptr;
        }
    }
    channels_.clear();
}

}  // namespace XER::UI::Core

#else

namespace XER::UI::Core {

UiRenderHost::~UiRenderHost() = default;
UiRenderHost& UiRenderHost::Instance() {
    static UiRenderHost host;
    return host;
}
bool UiRenderHost::EnsureWorkers(int, std::string*, const std::string&) {
    return false;
}
int UiRenderHost::WorkerCount() const { return 0; }
std::unique_ptr<Rendering::StreamRenderer>
UiRenderHost::CreateRemoteRenderer(uintptr_t, std::string*) {
    return nullptr;
}
void UiRenderHost::ShutdownAll() {}

}  // namespace XER::UI::Core

#endif
