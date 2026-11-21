// worker_pipe.cc — see worker_pipe.h for the contract. The transport
// is synchronous JSON-lines over anonymous pipes: the caller writes a
// request and blocks on a condvar until the matching-id reply lands
// (a dedicated reader thread drains stdout). This is the same
// machinery MediaWorkerClient originally carried, generalized so the
// named role exes (XERSyncMedia, XERFxScript) share one spawn/pipe
// implementation instead of each growing a copy.
//
// Death accounting lives in exactly one place: the reader thread's
// exit path counts a death unless the teardown was a deliberate
// KillWorker() (cancel). Write failures and timeouts both tear the
// child down through Kill(), so a crashed worker respawns on the next
// request until kMaxDeaths is reached — after which the pipe reports
// unavailable forever and callers stay on their in-process path.

#include "worker_pipe.h"

#include "media_worker_protocol.h"

#if defined(_WIN32)

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

#include "helper/win_job.h"

namespace XER::VideoEditor {
namespace {

constexpr int kMaxDeaths = 3;   // then the pipe stays down
// Bounded queue for reverse-channel request lines the callback
// reader has drained but the pump hasn't consumed yet — a stalled
// pump must not grow memory without limit; the oldest line is
// dropped (a dropped host_call leaves its worker-side waiter
// parked, which is the documented stall, not a memory leak).
constexpr std::size_t kMaxCallbackLines = 4096;

// Reads one chunk from the pipe. Returns bytes or -1 (EOF/broken).
int ReadPipe(HANDLE h, char* buf, int cap) {
    DWORD got = 0;
    return ReadFile(h, buf, static_cast<DWORD>(cap), &got, nullptr) &&
                   got > 0
               ? static_cast<int>(got)
               : -1;
}

// spec.dedicated_exe is ASCII — widen per-char (no MBCS needed).
std::wstring Widen(const char* text) {
    return std::wstring(text, text + std::strlen(text));
}

}  // namespace

struct WorkerPipe::Impl {
    explicit Impl(WorkerPipeSpec s)
        : spec(s),
          disabled(s.env_disable != nullptr &&
                   std::getenv(s.env_disable) != nullptr &&
                   std::getenv(s.env_disable)[0] == '0') {}

    ~Impl() { Kill(/*deliberate=*/true); }

    // Tears down the child. `deliberate` suppresses the death count
    // the reader's exit path would otherwise record — a cancel must
    // not burn one of the kMaxDeaths retries. Joins the reader OUTSIDE
    // the mutex: its exit path takes mu for the reply-table clear.
    void Kill(bool deliberate) {
        std::thread to_join;
        std::thread cb_to_join;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (deliberate && proc.hProcess != nullptr) {
                kill_was_deliberate = true;
            }
            if (proc.hProcess != nullptr) {
                TerminateProcess(proc.hProcess, 0);
                CloseHandle(proc.hProcess);
                proc.hProcess = nullptr;
            }
            if (proc.hThread != nullptr) {
                CloseHandle(proc.hThread);
                proc.hThread = nullptr;
            }
            if (child_stdin != nullptr) {
                CloseHandle(child_stdin);
                child_stdin = nullptr;
            }
            if (child_stdout != nullptr) {
                CloseHandle(child_stdout);
                child_stdout = nullptr;
            }
            if (cb_read != nullptr) {
                CloseHandle(cb_read);
                cb_read = nullptr;
            }
            if (cb_write != nullptr) {
                CloseHandle(cb_write);
                cb_write = nullptr;
            }
            cb_lines.clear();
            reader_done.store(true);
            cb_reader_done.store(true);
            if (reader.joinable()) {
                to_join = std::move(reader);
            }
            if (cb_reader.joinable()) {
                cb_to_join = std::move(cb_reader);
            }
        }
        cv.notify_all();   // wake waiters — no reply is coming
        if (to_join.joinable()) to_join.join();
        if (cb_to_join.joinable()) cb_to_join.join();
    }

    // mu held. Spawns the child on first use (or after a teardown) and
    // starts the stdout reader thread.
    bool SpawnLocked() {
        if (proc.hProcess != nullptr) return true;
        if (disabled || spawn_failed ||
            deaths.load(std::memory_order_relaxed) >= kMaxDeaths) {
            return false;
        }
        kill_was_deliberate = false;   // fresh child, fresh accounting
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        HANDLE in_read = nullptr;
        HANDLE out_write = nullptr;
        HANDLE out_read = nullptr;
        HANDLE in_write = nullptr;
        if (!CreatePipe(&in_read, &in_write, &sa, 1 << 20) ||
            !CreatePipe(&out_read, &out_write, &sa, 1 << 20)) {
            spawn_failed = true;
            return false;
        }
        // The PARENT ends of each pipe must not be inherited or the
        // child never sees EOF (extra copies keep the write end alive
        // in the grandchild).
        SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        // Prefer the dedicated worker binary shipped BESIDE this exe —
        // resolved from the module's own directory, never PATH/CWD.
        // Dev builds without it may fall back to self-spawning
        // (`--media-worker` for the decode role).
        std::wstring dir(exe);
        const auto sep = dir.find_last_of(L"\\/");
        std::wstring cmd;
        if (sep != std::wstring::npos) {
            dir.erase(sep + 1);
            if (spec.dedicated_exe != nullptr &&
                spec.dedicated_exe[0] != '\0') {
                const std::wstring candidate =
                    dir + Widen(spec.dedicated_exe);
                const DWORD attr = GetFileAttributesW(candidate.c_str());
                if (attr != INVALID_FILE_ATTRIBUTES &&
                    (attr & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                    cmd = L"\"" + candidate + L"\"";
                }
            }
        }
        if (cmd.empty() && spec.self_spawn_arg != nullptr &&
            spec.self_spawn_arg[0] != '\0') {
            cmd = L"\"" + std::wstring(exe) + L"\" " +
                  Widen(spec.self_spawn_arg);
        }
        if (cmd.empty()) {
            CloseHandle(in_read);
            CloseHandle(in_write);
            CloseHandle(out_read);
            CloseHandle(out_write);
            spawn_failed = true;
            return false;
        }
        // Optional reverse channel (spec.callback_env): two more
        // anonymous pipes. The child inherits the ends it uses and
        // learns their handle values through environment variables —
        // anonymous-pipe handles can't be named, so an env handoff is
        // the simplest side-band that survives an unchanged command
        // line. Channel failure degrades to "no channel" (the worker
        // falls back to its queued-event path); it never fails the
        // spawn itself.
        HANDLE cb_req_write = nullptr;   // child writes requests here
        HANDLE cb_rsp_read = nullptr;    // child reads replies here
        HANDLE cb_req_read = nullptr;    // parent drains this
        HANDLE cb_rsp_write = nullptr;   // parent answers on this
        const bool want_cb =
            spec.callback_env != nullptr && spec.callback_env[0] != '\0';
        const bool have_cb =
            want_cb &&
            CreatePipe(&cb_req_read, &cb_req_write, &sa, 1 << 20) &&
            CreatePipe(&cb_rsp_read, &cb_rsp_write, &sa, 1 << 20);
        if (have_cb) {
            // Parent ends must not be inherited (same EOF rule as
            // stdin/stdout), child ends already are.
            SetHandleInformation(cb_req_read, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(cb_rsp_write, HANDLE_FLAG_INHERIT, 0);
        } else if (want_cb) {
            // Partial creation — close whatever exists.
            for (HANDLE h : {cb_req_read, cb_req_write, cb_rsp_read,
                             cb_rsp_write}) {
                if (h != nullptr) CloseHandle(h);
            }
            cb_req_read = cb_req_write = cb_rsp_read = cb_rsp_write =
                nullptr;
        }
        // Child environment: the parent's block plus the two handle
        // advertisements. A nullptr lpEnvironment would just inherit —
        // with a channel we must build the block ourselves.
        std::vector<wchar_t> env_block;
        DWORD create_flags = CREATE_NO_WINDOW;
        if (have_cb) {
            if (const wchar_t* parent_env = GetEnvironmentStringsW();
                parent_env != nullptr) {
                const wchar_t* p = parent_env;
                while (*p != L'\0') {
                    const std::size_t len = std::wcslen(p) + 1;
                    env_block.insert(env_block.end(), p, p + len);
                    p += len;
                }
                FreeEnvironmentStringsW(
                    const_cast<wchar_t*>(parent_env));
            }
            const std::wstring prefix = Widen(spec.callback_env);
            const auto push_var = [&env_block](const std::wstring& v) {
                env_block.insert(env_block.end(), v.begin(), v.end());
                env_block.push_back(L'\0');
            };
            // REQ = the handle the worker WRITES host requests to;
            // RSP = the handle it READS editor replies from.
            push_var(prefix + L"_REQ=" +
                     std::to_wstring(reinterpret_cast<std::uintptr_t>(
                         cb_req_write)));
            push_var(prefix + L"_RSP=" +
                     std::to_wstring(reinterpret_cast<std::uintptr_t>(
                         cb_rsp_read)));
            env_block.push_back(L'\0');   // double-NUL terminator
            create_flags |= CREATE_UNICODE_ENVIRONMENT;
        }
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = in_read;
        si.hStdOutput = out_write;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessW(
            nullptr, cmd.data(), nullptr, nullptr,
            /*inherit*/ TRUE, create_flags,
            env_block.empty() ? nullptr : env_block.data(), nullptr,
            &si, &pi);
        CloseHandle(in_read);      // parent never uses these ends
        CloseHandle(out_write);
        if (have_cb) {             // nor the child's channel ends
            CloseHandle(cb_req_write);
            CloseHandle(cb_rsp_read);
        }
        if (!ok) {
            CloseHandle(in_write);
            CloseHandle(out_read);
            if (have_cb) {
                CloseHandle(cb_req_read);
                CloseHandle(cb_rsp_write);
            }
            spawn_failed = true;
            return false;
        }
        proc = pi;
        // Group the worker with the editor: a killed/crashed editor
        // must not leave a headless worker holding locks.
        XER::Helper::WinJobGroup::Shared().Assign(pi.hProcess);
        child_stdin = in_write;
        child_stdout = out_read;
        reader_done.store(false);
        reader = std::thread([this] { ReaderLoop(); });
        if (have_cb) {
            cb_read = cb_req_read;
            cb_write = cb_rsp_write;
            cb_reader_done.store(false);
            cb_reader = std::thread([this] { CallbackReaderLoop(); });
        }
        return true;
    }

    // Drains the reverse channel's request pipe (worker -> editor),
    // queueing complete JSON lines for PopCallbackLine. Death
    // accounting stays with ReaderLoop only — a broken callback pipe
    // just marks the channel done; the stdout reader reports the
    // child exit.
    void CallbackReaderLoop() {
        char buf[65536];
        std::string carry;
        for (;;) {
            const int n = ReadPipe(cb_read, buf, sizeof(buf));
            if (n <= 0) break;
            carry.append(buf, buf + n);
            for (;;) {
                const auto nl = carry.find('\n');
                if (nl == std::string::npos) break;
                std::string line = carry.substr(0, nl);
                carry.erase(0, nl + 1);
                std::lock_guard<std::mutex> lock(mu);
                if (cb_lines.size() >= kMaxCallbackLines) {
                    cb_lines.pop_front();   // bounded — see constant
                }
                cb_lines.push_back(std::move(line));
            }
        }
        cb_reader_done.store(true);
    }

    void ReaderLoop() {
        char buf[65536];
        std::string carry;
        for (;;) {
            const int n = ReadPipe(child_stdout, buf, sizeof(buf));
            if (n <= 0) break;
            carry.append(buf, buf + n);
            for (;;) {
                const auto nl = carry.find('\n');
                if (nl == std::string::npos) break;
                std::string line = carry.substr(0, nl);
                carry.erase(0, nl + 1);
                std::uint64_t id = 0;
                bool ok = false;
                std::string err;
                if (MediaWorker::DecodeReplyEnvelope(line, &id, &ok,
                                                     &err)) {
                    std::lock_guard<std::mutex> lock(mu);
                    replies.emplace(id, std::move(line));
                    cv.notify_all();
                }
            }
        }
        reader_done.store(true);
        // The sole death counter: a child that dies (or hangs until
        // killed) is counted here — UNLESS the teardown was an
        // explicit KillWorker() cancel. Deliberate kills leave the
        // retry budget intact.
        const bool deliberate = [this] {
            std::lock_guard<std::mutex> lock(mu);
            return kill_was_deliberate;
        }();
        {
            std::lock_guard<std::mutex> lock(mu);
            replies.clear();
            if (!deliberate) {
                deaths.fetch_add(1, std::memory_order_relaxed);
            }
        }
        cv.notify_all();   // fail every waiter
    }

    // mu held → writes; released → waits. Sends the already-encoded
    // line and waits for the reply carrying `id`.
    bool RoundTripImpl(std::uint64_t id, const std::string& line,
                       int timeout_ms, std::string* reply) {
        std::unique_lock<std::mutex> lock(mu);
        if (!SpawnLocked()) return false;
        const std::string wire = line + "\n";
        DWORD wrote = 0;
        if (!WriteFile(child_stdin, wire.data(),
                       static_cast<DWORD>(wire.size()), &wrote,
                       nullptr) ||
            wrote != wire.size()) {
            lock.unlock();
            // Broken child — full teardown so the next call respawns.
            Kill(/*deliberate=*/false);
            return false;
        }
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeout_ms);
        while (!reader_done.load(std::memory_order_relaxed)) {
            const auto it = replies.find(id);
            if (it != replies.end()) {
                *reply = std::move(it->second);
                replies.erase(it);
                return true;
            }
            if (cv.wait_until(lock, deadline) ==
                std::cv_status::timeout) {
                break;
            }
        }
        replies.erase(id);
        lock.unlock();
        Kill(/*deliberate=*/false);   // hung or dead: tear it down
        return false;
    }

    const WorkerPipeSpec spec;
    const bool disabled = false;
    std::mutex mu;
    std::condition_variable cv;
    PROCESS_INFORMATION proc{};
    HANDLE child_stdin = nullptr;    // our write end
    HANDLE child_stdout = nullptr;   // our read end
    std::thread reader;
    std::atomic<bool> reader_done{false};
    // Reverse channel (spec.callback_env): cb_read drains the
    // worker's host requests (cb_reader thread queues them in
    // cb_lines under mu), cb_write carries our reply lines. Both null
    // when the role has no channel.
    HANDLE cb_read = nullptr;
    HANDLE cb_write = nullptr;
    std::thread cb_reader;
    std::atomic<bool> cb_reader_done{false};
    std::deque<std::string> cb_lines;   // under mu
    std::map<std::uint64_t, std::string> replies;
    std::atomic<std::uint64_t> next_id{1};
    std::atomic<int> deaths{0};
    bool kill_was_deliberate = false;   // under mu
    bool spawn_failed = false;          // under mu
};

WorkerPipe::WorkerPipe(WorkerPipeSpec spec)
    : impl_(std::make_unique<Impl>(spec)) {}
WorkerPipe::~WorkerPipe() = default;

bool WorkerPipe::Available() {
    if (impl_->disabled ||
        impl_->deaths.load(std::memory_order_relaxed) >= kMaxDeaths ||
        impl_->spawn_failed) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->SpawnLocked();
}

std::uint64_t WorkerPipe::NextId() {
    return impl_->next_id.fetch_add(1, std::memory_order_relaxed);
}

bool WorkerPipe::RoundTrip(std::uint64_t id,
                           const std::string& request_line,
                           int timeout_ms, std::string* reply_line) {
    if (reply_line == nullptr) return false;
    return impl_->RoundTripImpl(id, request_line, timeout_ms,
                                reply_line);
}

bool WorkerPipe::Ping(std::string* role_out, int timeout_ms) {
    if (!Available()) return false;
    const std::uint64_t id = NextId();
    MediaWorker::Request req;
    req.id = id;
    req.kind = MediaWorker::Kind::kPing;
    std::string reply;
    if (!RoundTrip(id, MediaWorker::EncodeRequest(req), timeout_ms,
                   &reply)) {
        return false;
    }
    std::uint64_t rid = 0;
    return MediaWorker::DecodePongReply(reply, &rid, role_out) &&
           rid == id;
}

void WorkerPipe::KillWorker() {
    impl_->Kill(/*deliberate=*/true);
}

int WorkerPipe::Deaths() const {
    return impl_->deaths.load(std::memory_order_relaxed);
}

bool WorkerPipe::HasCallbackChannel() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->cb_read != nullptr && impl_->cb_write != nullptr;
}

bool WorkerPipe::PopCallbackLine(std::string* line) {
    if (line == nullptr) return false;
    std::lock_guard<std::mutex> lock(impl_->mu);
    if (impl_->cb_lines.empty()) return false;
    *line = std::move(impl_->cb_lines.front());
    impl_->cb_lines.pop_front();
    return true;
}

bool WorkerPipe::WriteCallbackLine(const std::string& line) {
    std::unique_lock<std::mutex> lock(impl_->mu);
    if (impl_->cb_write == nullptr) return false;
    const std::string wire = line + "\n";
    DWORD wrote = 0;
    if (WriteFile(impl_->cb_write, wire.data(),
                  static_cast<DWORD>(wire.size()), &wrote, nullptr) &&
        wrote == wire.size()) {
        return true;
    }
    lock.unlock();
    // Broken reverse channel = broken child — same teardown a failed
    // RoundTrip performs so the next request respawns cleanly.
    impl_->Kill(/*deliberate=*/false);
    return false;
}

WorkerPipe& SyncMediaPipe() {
    static WorkerPipe pipe(WorkerPipeSpec{
        "XERSyncMedia.exe", nullptr, "XER_VE_SYNC_WORKER"});
    return pipe;
}

WorkerPipe& FxScriptPipe() {
    static WorkerPipe pipe(WorkerPipeSpec{
        "XERFxScript.exe", nullptr, "XER_VE_FX_WORKER"});
    return pipe;
}

WorkerPipe& ExtRuntimePipe() {
    static WorkerPipe pipe(WorkerPipeSpec{
        "VE.ExtensionsRuntime.exe", nullptr, "XER_VE_EXT_WORKER",
        "XER_VE_EXT_HOST"});
    return pipe;
}

}  // namespace XER::VideoEditor

#else   // !_WIN32 — subprocess workers are Windows-only; callers run
        // their in-process path elsewhere.

namespace XER::VideoEditor {

struct WorkerPipe::Impl {};

WorkerPipe::WorkerPipe(WorkerPipeSpec)
    : impl_(std::make_unique<Impl>()) {}
WorkerPipe::~WorkerPipe() = default;
bool WorkerPipe::Available() { return false; }
std::uint64_t WorkerPipe::NextId() { return 0; }
bool WorkerPipe::RoundTrip(std::uint64_t, const std::string&, int,
                           std::string*) {
    return false;
}
bool WorkerPipe::Ping(std::string*, int) { return false; }
void WorkerPipe::KillWorker() {}
int WorkerPipe::Deaths() const { return 0; }
bool WorkerPipe::HasCallbackChannel() const { return false; }
bool WorkerPipe::PopCallbackLine(std::string*) { return false; }
bool WorkerPipe::WriteCallbackLine(const std::string&) { return false; }

WorkerPipe& SyncMediaPipe() {
    static WorkerPipe pipe(WorkerPipeSpec{});
    return pipe;
}
WorkerPipe& FxScriptPipe() {
    static WorkerPipe pipe(WorkerPipeSpec{});
    return pipe;
}
WorkerPipe& ExtRuntimePipe() {
    static WorkerPipe pipe(WorkerPipeSpec{});
    return pipe;
}

}  // namespace XER::VideoEditor

#endif  // _WIN32
