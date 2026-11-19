#include "media/session/media_session.h"

#include <algorithm>

namespace XER::Media {

AudioFocusManager& AudioFocusManager::Instance() {
    static AudioFocusManager instance;
    return instance;
}

void AudioFocusManager::RequestAudioFocus(MediaSession* session,
                                          AudioFocusType type) {
    // Already on the stack? Remove first, then re-push on top.
    stack_.erase(std::remove_if(stack_.begin(), stack_.end(),
                                [session](const Entry& e) {
                                    return e.session == session;
                                }),
                 stack_.end());
    stack_.push_back({session, type});
    session->OnStateChanged(SessionState::kActive, false);

    if (type == AudioFocusType::kPersistent) {
        // Suspend every other session on the stack.
        for (auto& e : stack_) {
            if (e.session != session)
                e.session->OnStateChanged(SessionState::kSuspended, false);
        }
    } else {
        // Transient: duck the next-top entry only if it is persistent.
        if (stack_.size() >= 2) {
            Entry& below = stack_[stack_.size() - 2];
            if (below.type == AudioFocusType::kPersistent)
                below.session->OnStateChanged(SessionState::kActive, true);
            // Transient below us: nothing to do — any persistent session is
            // already ducking.
        }
    }
}

void AudioFocusManager::AbandonAudioFocus(MediaSession* session) {
    const bool was_top =
        !stack_.empty() && stack_.back().session == session;
    stack_.erase(std::remove_if(stack_.begin(), stack_.end(),
                                [session](const Entry& e) {
                                    return e.session == session;
                                }),
                 stack_.end());
    session->OnStateChanged(SessionState::kInactive, false);

    if (!was_top || stack_.empty()) return;

    // Resume the next session on top of the stack.
    Entry& top = stack_.back();
    if (top.type == AudioFocusType::kPersistent) {
        // Stop ducking it.
        top.session->OnStateChanged(SessionState::kActive, false);
    }
    // Next top transient: do nothing.
}

}  // namespace XER::Media
