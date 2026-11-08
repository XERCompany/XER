#include "midi_event_dispatcher.h"

namespace XER::Audio::MIDI {

void MidiEventDispatcher::AddListener(const Callback& cb) {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    listeners_.push_back(cb);
}

void MidiEventDispatcher::Dispatch(const MidiEvent& event) {
    std::vector<Callback> listeners;
    {
        XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
        listeners = listeners_;
    }
    for (const auto& cb : listeners) {
		cb(event);
	}
}

void MidiEventDispatcher::DispatchBatch(const std::vector<MidiEvent>& events) {
	for (const auto& event : events) {
		Dispatch(event);
	}
}

}  // namespace XER::Audio::MIDI
