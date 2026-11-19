/**
 * XER::Media::Session + AudioFocusManager (design doc: audio focus).
 *
 * A MediaSession collects all audio-producing objects of one logical
 * "tab". AudioFocusManager is a global stack-based arbiter:
 *  - persistent sessions suspend every other session when they take focus;
 *  - transient sessions duck the topmost active persistent session.
 * States: ACTIVE (has focus, may play), SUSPENDED (paused, resumable),
 * INACTIVE (no focus, no producers). DUCKING is an orthogonal flag.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace XER::Media {

enum class AudioFocusType : uint8_t { kPersistent, kTransient };

enum class SessionState : uint8_t { kActive, kSuspended, kInactive };

class MediaSession {
public:
    enum Flag : uint8_t { kDucking = 1 << 0 };

    virtual ~MediaSession() = default;

    virtual SessionState state() const = 0;
    virtual bool IsDucking() const = 0;
    // Called by AudioFocusManager when state changes.
    virtual void OnStateChanged(SessionState new_state, bool ducking) = 0;

    // Producer registration — session becomes INACTIVE when empty.
    virtual void AddProducer() = 0;
    virtual void RemoveProducer() = 0;
    virtual std::size_t producer_count() const = 0;
};

// Stack-based global arbiter (see doc for the exact algorithm).
class AudioFocusManager {
public:
    static AudioFocusManager& Instance();

    // Registers a session's focus request. Moves |session| to the top of the
    // stack, grants it ACTIVE, and applies suspend/duck per focus type.
    void RequestAudioFocus(MediaSession* session, AudioFocusType type);
    // Removes the session; if it was on top, un-ducks a persistent entry.
    void AbandonAudioFocus(MediaSession* session);

    std::size_t stack_size() const { return stack_.size(); }

private:
    struct Entry {
        MediaSession* session;
        AudioFocusType type;
    };
    std::vector<Entry> stack_;  // back() == top
};

// Convenience in-process session for native audio producers.
class MediaSessionImpl final : public MediaSession {
public:
    using StateCallback = std::function<void(SessionState, bool /*ducking*/)>;

    explicit MediaSessionImpl(StateCallback cb = {}) : cb_(std::move(cb)) {}

    SessionState state() const override { return state_; }
    bool IsDucking() const override { return ducking_; }
    void OnStateChanged(SessionState new_state, bool ducking) override {
        state_ = new_state;
        ducking_ = ducking;
        if (cb_) cb_(new_state, ducking);
    }

    void AddProducer() override { ++producers_; }
    void RemoveProducer() override {
        if (producers_ > 0) --producers_;
    }
    std::size_t producer_count() const override { return producers_; }

private:
    StateCallback cb_;
    SessionState state_ = SessionState::kInactive;
    bool ducking_ = false;
    std::size_t producers_ = 0;
};

}  // namespace XER::Media
