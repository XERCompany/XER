#include "media/player/pipeline_controller.h"

namespace XER::Media {

const char* PipelineStateName(PipelineState state) {
    switch (state) {
        case PipelineState::kIdle: return "idle";
        case PipelineState::kOpening: return "opening";
        case PipelineState::kReady: return "ready";
        case PipelineState::kPlaying: return "playing";
        case PipelineState::kPaused: return "paused";
        case PipelineState::kStopped: return "stopped";
        case PipelineState::kError: return "error";
    }
    return "?";
}

bool PipelineController::Transition(PipelineState to,
                                    std::string* error_out) {
    auto legal = [](PipelineState from, PipelineState to_) {
        switch (from) {
            case PipelineState::kIdle:
                return to_ == PipelineState::kOpening ||
                       to_ == PipelineState::kError;
            case PipelineState::kOpening:
                return to_ == PipelineState::kReady ||
                       to_ == PipelineState::kError ||
                       to_ == PipelineState::kStopped;
            case PipelineState::kReady:
                return to_ == PipelineState::kPlaying ||
                       to_ == PipelineState::kStopped;
            case PipelineState::kPlaying:
                return to_ == PipelineState::kPaused ||
                       to_ == PipelineState::kStopped ||
                       to_ == PipelineState::kError;
            case PipelineState::kPaused:
                return to_ == PipelineState::kPlaying ||
                       to_ == PipelineState::kStopped;
            case PipelineState::kStopped:
                return to_ == PipelineState::kIdle;
            case PipelineState::kError:
                return to_ == PipelineState::kStopped;
        }
        return false;
    };
    if (!legal(state_, to)) {
        if (error_out) {
            *error_out = std::string("illegal transition ") +
                         PipelineStateName(state_) + " -> " +
                         PipelineStateName(to);
        }
        return false;
    }
    const PipelineState from = state_;
    state_ = to;
    if (cb_) cb_(from, to);
    return true;
}

}  // namespace XER::Media
