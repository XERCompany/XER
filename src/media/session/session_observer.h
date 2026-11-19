/**
 * SessionObserver — notification hooks for MediaSession state changes
 * (focus gained/lost, ducking toggles, producer count changes).
 */
#pragma once

#include "media/session/media_session.h"

namespace XER::Media {

class SessionObserver {
public:
    virtual ~SessionObserver() = default;
    virtual void OnSessionStateChanged(MediaSession* session,
                                       SessionState state, bool ducking) = 0;
    virtual void OnSessionBecameEmpty(MediaSession* session) = 0;
};

// MediaSessionImpl variant that forwards every state change to a
// registered SessionObserver in addition to its local callback.
class ObservedMediaSession final : public MediaSession {
public:
    explicit ObservedMediaSession(SessionObserver* observer = nullptr)
        : observer_(observer) {}

    void SetObserver(SessionObserver* o) { observer_ = o; }

    SessionState state() const override { return state_; }
    bool IsDucking() const override { return ducking_; }
    void OnStateChanged(SessionState new_state, bool ducking) override {
        state_ = new_state;
        ducking_ = ducking;
        if (observer_)
            observer_->OnSessionStateChanged(this, new_state, ducking);
    }

    void AddProducer() override { ++producers_; }
    void RemoveProducer() override {
        if (producers_ > 0 && --producers_ == 0 && observer_)
            observer_->OnSessionBecameEmpty(this);
    }
    std::size_t producer_count() const override { return producers_; }

private:
    SessionObserver* observer_;
    SessionState state_ = SessionState::kInactive;
    bool ducking_ = false;
    std::size_t producers_ = 0;
};

}  // namespace XER::Media
