#include "watch/watch_live_updatex_script.h"

#include "watch/watch_change_detector.h"
#include "watch/watch_cli_support.h"
#include "watch/watch_file_scanner.h"
#include "watch/watch_recompile_service.h"

#include <csignal>
#include <filesystem>
#include <absl/strings/str_cat.h>
#include "flux/terminal/terminal_output_renderer.h"
#include <uv.h>

std::atomic<bool> WatchLiveUpdatexScript::stop_requested_{false};

namespace {

void HandleWatchSignal(int) {
    WatchLiveUpdatexScript::RequestStop();
}

struct WatchLoopState {
    WatchFileScanner scanner;
    WatchChangeDetector::SnapshotMap previous;
    WatchRecompileService service;
    std::string script_path;
    std::filesystem::path root_path;
    int max_cycles = -1;
    int cycles = 0;
    bool last_ok = true;
};

void WriteWatchLine(XER::Flux::Terminal::OutputStream stream, const std::string& text) {
	XER::Flux::Terminal::WriteLine(stream, text);
}

void TriggerRestart(WatchLoopState* state, const std::string& reason) {
    ++state->cycles;
	WriteWatchLine(XER::Flux::Terminal::OutputStream::kStdout,
		WatchCliSupport::CycleMessage(state->cycles, reason));
    state->previous = WatchChangeDetector::BuildMap(state->scanner.Scan());

    std::string compile_error;
    state->last_ok = state->service.Recompile(state->script_path, &compile_error);
    if (!state->last_ok && !compile_error.empty()) {
		WriteWatchLine(XER::Flux::Terminal::OutputStream::kStderr,
			absl::StrCat("[watch] ", compile_error));
    }
}

void WatchTick(uv_timer_t* timer) {
    auto* state = static_cast<WatchLoopState*>(timer->data);
    if (state == nullptr) {
        uv_stop(timer->loop);
        return;
    }

    if (WatchLiveUpdatexScript::StopRequested()) {
        uv_stop(timer->loop);
        return;
    }

    const auto current = WatchChangeDetector::BuildMap(state->scanner.Scan());
    std::string reason;
    if (!WatchChangeDetector::HasChanges(state->previous, current, &reason)) {
        return;
    }

    TriggerRestart(state, reason);

    if (state->max_cycles > 0 && state->cycles >= state->max_cycles) {
        uv_stop(timer->loop);
    }
}

void WatchFsEvent(uv_fs_event_t* handle,
                  const char* filename,
                  int events,
                  int status) {
    (void)events;
    auto* state = static_cast<WatchLoopState*>(handle->data);
    if (state == nullptr || status < 0) {
        return;
    }

    if (WatchLiveUpdatexScript::StopRequested()) {
        return;
    }

    std::string reason = "filesystem event";
    if (filename != nullptr && filename[0] != '\0') {
        const std::filesystem::path changed = state->root_path / filename;
        if (!WatchFileScanner::IsWatchedExtension(changed)) {
            return;
        }
        reason = "modified file: " + changed.string();
    }

    TriggerRestart(state, reason);

    if (state->max_cycles > 0 && state->cycles >= state->max_cycles) {
        uv_stop(handle->loop);
    }
}

}  // namespace

WatchLiveUpdatexScript::WatchLiveUpdatexScript(WatchLiveUpdatexScriptConfig config)
    : config_(std::move(config)) {}

int WatchLiveUpdatexScript::Run() {
    stop_requested_.store(false);
    std::signal(SIGINT, HandleWatchSignal);
    std::signal(SIGTERM, HandleWatchSignal);

    XER::Flux::Terminal::Write(
        XER::Flux::Terminal::OutputStream::kStdout,
        WatchCliSupport::Header(config_.script_path, config_.poll_interval_ms));

    const std::filesystem::path script_path(config_.script_path);
    const std::filesystem::path root = script_path.parent_path().empty()
        ? std::filesystem::current_path()
        : script_path.parent_path();

    WatchLoopState state{WatchFileScanner(root), {}, WatchRecompileService(), config_.script_path, root, config_.max_cycles, 0, true};
    state.previous = WatchChangeDetector::BuildMap(state.scanner.Scan());

    std::string compile_error;
    state.last_ok = state.service.Recompile(config_.script_path, &compile_error);
    if (!state.last_ok && !compile_error.empty()) {
        WriteWatchLine(XER::Flux::Terminal::OutputStream::kStderr,
            absl::StrCat("[watch] ", compile_error));
    }

    uv_loop_t loop;
    uv_loop_init(&loop);

    uv_timer_t timer;
    uv_timer_init(&loop, &timer);
    timer.data = &state;
    uv_timer_start(
        &timer,
        WatchTick,
        static_cast<uint64_t>(config_.poll_interval_ms),
        static_cast<uint64_t>(config_.poll_interval_ms));

    uv_fs_event_t fs_watcher;
    uv_fs_event_init(&loop, &fs_watcher);
    fs_watcher.data = &state;
    const std::string watch_path = root.string();
    const int fs_rc = uv_fs_event_start(&fs_watcher, WatchFsEvent, watch_path.c_str(), 0);
    if (fs_rc != 0) {
        WriteWatchLine(XER::Flux::Terminal::OutputStream::kStderr,
            "[watch] fs-event disabled, fallback to timer polling only");
    }

    while (!stop_requested_.load()) {
        uv_run(&loop, UV_RUN_DEFAULT);
        if (stop_requested_.load()) {
            break;
        }
        if (config_.max_cycles > 0 && state.cycles >= config_.max_cycles) {
            break;
        }
    }

    uv_timer_stop(&timer);
    if (fs_rc == 0) {
        uv_fs_event_stop(&fs_watcher);
        uv_close(reinterpret_cast<uv_handle_t*>(&fs_watcher), nullptr);
    }
    uv_close(reinterpret_cast<uv_handle_t*>(&timer), nullptr);
    uv_run(&loop, UV_RUN_DEFAULT);
    uv_loop_close(&loop);

    state.service.Stop();
    WriteWatchLine(XER::Flux::Terminal::OutputStream::kStdout, "[watch] stopped");
    return state.last_ok ? 0 : 1;
}

void WatchLiveUpdatexScript::RequestStop() {
    stop_requested_.store(true);
}

bool WatchLiveUpdatexScript::StopRequested() {
    return stop_requested_.load();
}
