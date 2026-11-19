/**
 * PipelineController — the player state machine. Transitions:
 *   Idle -> Opening -> Ready -> Playing <-> Paused
 *   any  -> Stopped -> Idle
 *   any  -> Error (terminal)
 * MediaPlayerImpl drives it; embedders observe transitions via callback.
 */
#pragma once

#include <functional>
#include <string>

namespace XER::Media {

enum class PipelineState {
    kIdle,
    kOpening,
    kReady,
    kPlaying,
    kPaused,
    kStopped,
    kError,
};

const char* PipelineStateName(PipelineState state);

class PipelineController {
public:
    using TransitionCallback =
        std::function<void(PipelineState from, PipelineState to)>;

    void SetTransitionCallback(TransitionCallback cb) { cb_ = std::move(cb); }

    PipelineState state() const { return state_; }
    bool Is(PipelineState s) const { return state_ == s; }
    // True when the transition is legal from the current state; on success
    // the state changes and the observer callback fires.
    bool Transition(PipelineState to, std::string* error_out = nullptr);

    // Convenience wrappers used by the player.
    bool BeginOpen(std::string* e) { return Transition(PipelineState::kOpening, e); }
    bool Opened(std::string* e) { return Transition(PipelineState::kReady, e); }
    bool Play(std::string* e) { return Transition(PipelineState::kPlaying, e); }
    bool Pause(std::string* e) { return Transition(PipelineState::kPaused, e); }
    bool Stop(std::string* e) { return Transition(PipelineState::kStopped, e); }
    bool Fail(std::string* e) { return Transition(PipelineState::kError, e); }

private:
    PipelineState state_ = PipelineState::kIdle;
    TransitionCallback cb_;
};

}  // namespace XER::Media
