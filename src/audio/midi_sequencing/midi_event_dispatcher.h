#pragma once

#include <functional>
#include <vector>

#include "async_io/sync_primitives/mutex_wrapper.h"
#include "midi_parser.h"

namespace XER::Engine::Audio::MIDI {

class MidiEventDispatcher {
public:
    using Callback = std::function<void(const MidiEvent&)>;

    void AddListener(const Callback& cb);
    void Dispatch(const MidiEvent& event);
    void DispatchBatch(const std::vector<MidiEvent>& events);

private:
    XER::AsyncIO::IO::Sync::MutexWrapper mutex_{"midi_dispatcher"};
    std::vector<Callback> listeners_;
};

}  // namespace XER::Engine::Audio::MIDI
