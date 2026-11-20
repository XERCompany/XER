#include "render_host.h"

#include <cstdio>
#include <cstdlib>

#include "cache/shared_memory_channel.h"
#include "rendering/render_store.h"
#include "rendering/render_stream.h"

#ifdef _WIN32
#include <windows.h>

#include "helper/string_utils.h"

namespace XER::UI::Core {
namespace {

constexpr wchar_t kWorkerExe[] = L"xer_engine_ui_render.exe";
constexpr uint32_t kChannelCapacity = 1u << 20;
constexpr uint32_t kDefaultStoreCapacity = 64u << 20;

std::string ChannelName(int channel_id) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "xer.ui.render.%lu.%d",
                  static_cast<unsigned long>(GetCurrentProcessId()),
                  channel_id);
    return buf;
}

std::string StoreName() {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "xer.ui.res.%lu",
                  static_cast<unsigned long>(GetCurrentProcessId()));
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
                               std::string* error_out,
                               ChannelEntry* entry) {
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
    entry->worker_process = pi.hProcess;
    entry->worker_pid = pi.dwProcessId;
    return true;
}

bool UiRenderHost::SpawnPooledWorker(ChannelEntry* entry,
                                     std::string* error_out) {
    const std::string exe = ExePath();
    if (exe.empty()) {
        if (error_out) {
            *error_out = "cannot locate xer_engine_ui_render.exe";
        }
        return false;
    }
    std::wstring cmd = L"\"" + Helper::String::Utf8ToWide(exe) +
                       L"\" --channel " +
                       Helper::String::Utf8ToWide(
                           ChannelName(entry->channel_id));
    if (!worker_arg_.empty()) {
        cmd += L' ';
        cmd += Helper::String::Utf8ToWide(worker_arg_);
    }
    if (std::getenv("XER_UI_RENDER_SURFACE") != nullptr) {
        cmd += L" --surface";
    }
    return SpawnWorker(cmd, error_out, entry);
}

void UiRenderHost::CloseWorkerHandle(ChannelEntry* entry) {
    if (entry->worker_process != nullptr) {
        CloseHandle(entry->worker_process);
        entry->worker_process = nullptr;
        entry->worker_pid = 0;
    }
}

bool UiRenderHost::EnsureWorkers(int count, std::string* error_out,
                                 const std::string& worker_arg) {
    // Spawn |count| hidden workers up-front — each owns a channel and
    // waits for commands; CreateRemoteRenderer checks them out.
    warm_target_ = count;
    worker_arg_ = worker_arg;
    int spawned = 0;
    for (int i = 0; i < count; ++i) {
        ChannelEntry entry;
        entry.channel_id = next_channel_id_++;
        std::string err;
        entry.channel = Cache::SharedMemoryChannel::Create(
            ChannelName(entry.channel_id), kChannelCapacity, &err);
        if (!entry.channel) {
            if (error_out) *error_out = std::move(err);
            break;
        }
        channels_.push_back(std::move(entry));
        if (!SpawnPooledWorker(&channels_.back(), error_out)) {
            channels_.pop_back();
            break;
        }
        ++spawned;
    }
    // count==0 is a valid "pool disabled" request — vacuous success.
    // Partial spawns report failure but keep the workers they got.
    return spawned == count;
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

int UiRenderHost::WarmWorkerCount() const {
    int warm = 0;
    for (const auto& e : channels_) {
        if (!e.in_use && e.worker_process != nullptr &&
            WaitForSingleObject(e.worker_process, 0) == WAIT_TIMEOUT) {
            ++warm;
        }
    }
    return warm;
}

int UiRenderHost::ReapAndRespawn() {
    int live_warm = 0;
    for (auto& e : channels_) {
        if (!e.in_use && e.worker_process != nullptr &&
            WaitForSingleObject(e.worker_process, 0) == WAIT_TIMEOUT)
            ++live_warm;
    }
    int restarted = 0;
    for (auto& e : channels_) {
        if (e.worker_process == nullptr) continue;
        if (WaitForSingleObject(e.worker_process, 0) == WAIT_TIMEOUT)
            continue;  // still running
        CloseWorkerHandle(&e);
        // Dead pool slot: respawn while below the configured target.
        if (!e.in_use && live_warm < warm_target_) {
            std::string err;
            if (SpawnPooledWorker(&e, &err)) {
                ++restarted;
                ++live_warm;
            }
        }
    }
    return restarted;
}

std::unique_ptr<Rendering::StreamRenderer>
UiRenderHost::CreateRemoteRenderer(uintptr_t target_hwnd,
                                   std::string* error_out) {
    ReapAndRespawn();
    ChannelEntry* entry = nullptr;
    for (auto& e : channels_) {
        if (!e.in_use && e.worker_process != nullptr &&
            WaitForSingleObject(e.worker_process, 0) == WAIT_TIMEOUT) {
            entry = &e;
            break;
        }
    }
    if (entry == nullptr) {
        // Pool exhausted: spawn a dedicated worker on demand.
        ChannelEntry fresh;
        fresh.channel_id = next_channel_id_++;
        std::string err;
        fresh.channel = Cache::SharedMemoryChannel::Create(
            ChannelName(fresh.channel_id), kChannelCapacity, &err);
        if (!fresh.channel) {
            if (error_out) *error_out = std::move(err);
            return nullptr;
        }
        channels_.push_back(std::move(fresh));
        entry = &channels_.back();
        if (!SpawnPooledWorker(entry, error_out)) {
            channels_.pop_back();
            return nullptr;
        }
    }
    entry->in_use = true;

    // Adopt the window: the pre-spawned worker retargets its surface
    // via the stream command (0 keeps its hidden surface).
    std::vector<uint8_t> retarget;
    Rendering::Stream::StreamWriter::SetTargetHwnd(
        retarget, static_cast<std::uint64_t>(target_hwnd));
    entry->channel->Send(retarget.data(),
                         static_cast<uint32_t>(retarget.size()), 100);

    // Attach the shared resource store when the app already created
    // one — DrawResource/DrawVideoFrame/DrawSpectrum resolve ids
    // against it.
    if (store_) {
        std::vector<uint8_t> attach;
        Rendering::Stream::StreamWriter::AttachStore(attach,
                                                   store_->name());
        entry->channel->Send(attach.data(),
                             static_cast<uint32_t>(attach.size()),
                             100);
    }

    // The shared channel outlives the renderer (host keeps a ref), so
    // captured pointers stay valid while the host or any renderer
    // holds it.
    auto ch_ptr = entry->channel;
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
    // Teardown hook: entry is captured by channel_id, not pointer —
    // channels_ (a vector) may reallocate on a later on-demand spawn.
    const int channel_id = entry->channel_id;
    auto release = [this, channel_id] { ReleaseChannel(channel_id); };
    return std::make_unique<Rendering::StreamRenderer>(
        std::move(send), std::move(request), std::move(release));
}

void UiRenderHost::ReleaseChannel(int channel_id) {
    for (auto& e : channels_) {
        if (e.channel_id != channel_id) {
            continue;
        }
        e.in_use = false;
        if (e.worker_process != nullptr &&
            WaitForSingleObject(e.worker_process, 0) == WAIT_TIMEOUT) {
            // Live worker — park it on its own hidden surface so it
            // goes back to the warm pool for the next checkout.
            if (e.channel) {
                std::vector<uint8_t> msg;
                Rendering::Stream::StreamWriter::SetTargetHwnd(msg, 0);
                e.channel->Send(msg.data(),
                                static_cast<uint32_t>(msg.size()), 100);
            }
        } else {
            // Died while checked out: drop the handle and refill the
            // pool slot while we're below the configured target (a
            // nullptr process is skipped by ReapAndRespawn, so the
            // respawn has to happen here).
            CloseWorkerHandle(&e);
            if (WarmWorkerCount() < warm_target_) {
                std::string err;
                SpawnPooledWorker(&e, &err);
            }
        }
        return;
    }
}

Rendering::RenderStore* UiRenderHost::Store() {
    if (!store_) {
        std::uint32_t capacity = kDefaultStoreCapacity;
        if (const char* env = std::getenv("XER_UI_STORE_MB")) {
            const long mb = std::strtol(env, nullptr, 10);
            if (mb > 0 && mb <= 2048) {
                capacity = static_cast<std::uint32_t>(mb) << 20;
            }
        }
        std::string err;
        store_ = Rendering::RenderStore::Create(StoreName(), capacity,
                                                &err);
        if (!store_) {
            std::fprintf(stderr, "UiRenderHost: store: %s\n",
                         err.c_str());
        }
    }
    return store_.get();
}

bool UiRenderHost::UploadImage(std::uint32_t id, const void* bgra,
                               int width, int height) {
    if (bgra == nullptr || width <= 0 || height <= 0 ||
        Store() == nullptr) {
        return false;
    }
    const std::uint32_t bytes = static_cast<std::uint32_t>(width) *
                                static_cast<std::uint32_t>(height) * 4u;
    return store_->Write(id, Rendering::RenderStore::Kind::kImageBgra,
                         static_cast<std::uint32_t>(width),
                         static_cast<std::uint32_t>(height), bgra,
                         bytes);
}

bool UiRenderHost::WriteVideoFrame(std::uint32_t id, const void* bgra,
                                   int width, int height) {
    if (bgra == nullptr || width <= 0 || height <= 0 ||
        Store() == nullptr) {
        return false;
    }
    const std::uint32_t bytes = static_cast<std::uint32_t>(width) *
                                static_cast<std::uint32_t>(height) * 4u;
    return store_->Write(id, Rendering::RenderStore::Kind::kVideoFrame,
                         static_cast<std::uint32_t>(width),
                         static_cast<std::uint32_t>(height), bgra,
                         bytes);
}

bool UiRenderHost::WriteSpectrum(std::uint32_t id, const float* levels,
                                 std::uint32_t count) {
    if (levels == nullptr || count == 0 || Store() == nullptr) {
        return false;
    }
    return store_->Write(id, Rendering::RenderStore::Kind::kSpectrumF32,
                         count, 1, levels, count * sizeof(float));
}

void UiRenderHost::FreeResource(std::uint32_t id) {
    if (store_) {
        store_->Free(id);
    }
    std::vector<std::uint8_t> msg;
    Rendering::Stream::StreamWriter::FreeResource(msg, id);
    Broadcast(msg);
}

void UiRenderHost::Broadcast(const std::vector<std::uint8_t>& msg) {
    for (auto& e : channels_) {
        if (e.channel) {
            e.channel->Send(msg.data(),
                            static_cast<std::uint32_t>(msg.size()), 25);
        }
    }
}

void UiRenderHost::ShutdownAll() {
    store_.reset();
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
    warm_target_ = 0;
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
    // Remote rendering is Windows-only for now; report success so
    // Application::Initialize doesn't treat the stub as an error.
    return true;
}
int UiRenderHost::WorkerCount() const { return 0; }
int UiRenderHost::WarmWorkerCount() const { return 0; }
int UiRenderHost::ReapAndRespawn() { return 0; }
std::unique_ptr<Rendering::StreamRenderer>
UiRenderHost::CreateRemoteRenderer(uintptr_t, std::string*) {
    return nullptr;
}
void UiRenderHost::ReleaseChannel(int) {}
void UiRenderHost::ShutdownAll() {}
Rendering::RenderStore* UiRenderHost::Store() { return nullptr; }
bool UiRenderHost::UploadImage(std::uint32_t, const void*, int, int) {
    return false;
}
bool UiRenderHost::WriteVideoFrame(std::uint32_t, const void*, int,
                                   int) {
    return false;
}
bool UiRenderHost::WriteSpectrum(std::uint32_t, const float*,
                                 std::uint32_t) {
    return false;
}
void UiRenderHost::FreeResource(std::uint32_t) {}
void UiRenderHost::Broadcast(const std::vector<std::uint8_t>&) {}

}  // namespace XER::UI::Core

#endif
