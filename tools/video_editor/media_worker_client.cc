// media_worker_client.cc — see media_worker_client.h for the contract.
//
// The protocol is synchronous JSON-lines over anonymous pipes: the
// pool thread writes a request and blocks on a condvar until the
// matching-id reply lands (a dedicated reader thread drains stdout).
// A request that timed out / a dead process poisons the worker; the
// offending PATH is remembered so a respawn is not asked to re-decode
// the file that just killed it (in-process fallback handles it).

#include "media_worker_client.h"

#include "media_worker_protocol.h"

#if defined(_WIN32)

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <windows.h>

#include "frame.h"

namespace XER::VideoEditor {
namespace {

constexpr int kPosterTimeoutMs = 15000;
constexpr int kPeaksTimeoutMs = 120000;
constexpr int kMaxDeaths = 3;      // then the client stays in-process
constexpr std::size_t kMaxSuspects = 256;

// Reads one line-terminated chunk from the pipe. Returns bytes or -1
// (EOF/broken pipe).
int ReadPipe(HANDLE h, char* buf, int cap) {
    DWORD got = 0;
    return ReadFile(h, buf, static_cast<DWORD>(cap), &got, nullptr) &&
                   got > 0
               ? static_cast<int>(got)
               : -1;
}

}  // namespace

struct MediaWorkerClient::Impl {
    std::mutex mu;
    std::condition_variable cv;
    PROCESS_INFORMATION proc{};
    HANDLE child_stdin = nullptr;    // our write end
    HANDLE child_stdout = nullptr;   // our read end
    std::thread reader;
    std::atomic<bool> reader_done{false};
    std::string pending_in;          // unparsed stdout tail
    std::map<std::uint64_t, std::string> replies;
    std::uint64_t next_id = 1;
    std::atomic<int> deaths{0};
    std::set<std::string> suspects;  // paths that timed out / died
    bool spawn_failed = false;
    bool disabled = [] {
        const char* v = std::getenv("XER_VE_MEDIA_WORKER");
        return v != nullptr && v[0] == '0';
    }();

    ~Impl() { Kill(); }

    // The request whose reply stream died with the process — marked as
    // the suspect path and counted as a death ONLY if the reader
    // thread hasn't already (reader death is the authoritative count).
    void MarkDeadLocked(const std::string& suspect_path) {
        if (!suspect_path.empty() &&
            suspects.size() < kMaxSuspects) {
            suspects.insert(suspect_path);
        }
        if (!reader_done.load(std::memory_order_relaxed)) {
            ++deaths;
        }
        replies.clear();
        cv.notify_all();
    }

    void Kill() {
        reader_done.store(true);
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
        if (reader.joinable()) reader.join();
    }

    bool Spawn() {
        if (proc.hProcess != nullptr) return true;
        if (disabled || spawn_failed ||
            deaths.load() >= kMaxDeaths) {
            return false;
        }
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
        // child sees EOF never (extra open copies keep the write end
        // alive in the grandchild).
        SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) +
                           L"\" --media-worker";
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
            /*inherit*/ TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
            &pi);
        CloseHandle(in_read);      // parent never uses these ends
        CloseHandle(out_write);
        if (!ok) {
            CloseHandle(in_write);
            CloseHandle(out_read);
            spawn_failed = true;
            return false;
        }
        proc = pi;
        child_stdin = in_write;
        child_stdout = out_read;
        reader_done.store(false);
        reader = std::thread([this] { ReaderLoop(); });
        return true;
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
        std::lock_guard<std::mutex> lock(mu);
        // A dead reader = dead (or hung) child — fail every waiter.
        replies.clear();
        ++deaths;
        cv.notify_all();
    }

    // Sends a request and waits for the matching reply line. Empty
    // return = timeout/death (caller falls back in-process).
    std::string RoundTrip(const MediaWorker::Request& request,
                          int timeout_ms) {
        std::unique_lock<std::mutex> lock(mu);
        if (suspects.count(request.path) != 0u) return {};
        if (!Spawn()) return {};
        const std::uint64_t id = next_id++;
        MediaWorker::Request req = request;
        req.id = id;
        const std::string line = MediaWorker::EncodeRequest(req) + "\n";
        DWORD wrote = 0;
        if (!WriteFile(child_stdin, line.data(),
                       static_cast<DWORD>(line.size()), &wrote,
                       nullptr) ||
            wrote != line.size()) {
            MarkDeadLocked(request.path);
            return {};
        }
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeout_ms);
        while (!reader_done.load(std::memory_order_relaxed)) {
            const auto it = replies.find(id);
            if (it != replies.end()) {
                std::string reply = std::move(it->second);
                replies.erase(it);
                return reply;
            }
            if (cv.wait_until(lock, deadline) ==
                std::cv_status::timeout) {
                break;
            }
        }
        // Timeout/death mid-request — suspect the path (so a respawn is
        // never asked to re-decode what just killed it). The reader
        // thread already counted an actual process death.
        replies.erase(id);
        if (!request.path.empty() &&
            suspects.size() < kMaxSuspects) {
            suspects.insert(request.path);
        }
        lock.unlock();
        Kill();   // joins an exited reader or kills a hung worker
        return {};
    }
};

MediaWorkerClient& MediaWorkerClient::Shared() {
    static MediaWorkerClient inst;
    return inst;
}

MediaWorkerClient::~MediaWorkerClient() {
    delete impl_;
}

bool MediaWorkerClient::Available() {
    if (impl_ == nullptr) {
        impl_ = new Impl();
    }
    if (impl_->disabled ||
        impl_->deaths.load() >= kMaxDeaths ||
        impl_->spawn_failed) {
        return false;
    }
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->Spawn();
}

bool MediaWorkerClient::PosterAt(const std::string& path,
                                 double seconds, video::Frame* out) {
    if (!Available()) return false;
    MediaWorker::Request req;
    req.kind = MediaWorker::Kind::kPosterAt;
    req.path = path;
    req.seconds = seconds;
    const std::string reply =
        impl_->RoundTrip(req, kPosterTimeoutMs);
    std::uint64_t id = 0;
    return !reply.empty() &&
           MediaWorker::DecodePosterReply(reply, &id, out);
}

bool MediaWorkerClient::Peaks(const std::string& path,
                              AudioPeaks* out) {
    if (!Available()) return false;
    MediaWorker::Request req;
    req.kind = MediaWorker::Kind::kPeaks;
    req.path = path;
    const std::string reply =
        impl_->RoundTrip(req, kPeaksTimeoutMs);
    std::uint64_t id = 0;
    return !reply.empty() &&
           MediaWorker::DecodePeaksReply(reply, &id, out);
}

}  // namespace XER::VideoEditor

#else   // !_WIN32 — the subprocess client is Windows-only; MediaPrep
        // runs the same decodes in-process elsewhere.

namespace XER::VideoEditor {

MediaWorkerClient& MediaWorkerClient::Shared() {
    static MediaWorkerClient inst;
    return inst;
}
MediaWorkerClient::~MediaWorkerClient() = default;
bool MediaWorkerClient::Available() { return false; }
bool MediaWorkerClient::PosterAt(const std::string&, double,
                                 video::Frame*) {
    return false;
}
bool MediaWorkerClient::Peaks(const std::string&, AudioPeaks*) {
    return false;
}

}  // namespace XER::VideoEditor

#endif  // _WIN32
