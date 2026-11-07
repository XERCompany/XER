#include "midi_parser.h"

#include <cstddef>

namespace XER::Engine::Audio::MIDI {

namespace {

bool IsStatusByte(uint8_t byte) {
    return (byte & 0x80u) != 0;
}

bool IsRealtimeStatus(uint8_t status) {
    return status >= 0xF8u;
}

size_t DataLengthForStatus(uint8_t status) {
    if (status < 0x80u) {
        return 0;
    }
    if (status < 0xF0u) {
        switch (status & 0xF0u) {
        case 0xC0u:
        case 0xD0u:
            return 1;
        default:
            return 2;
        }
    }

    switch (status) {
    case 0xF1u:
    case 0xF3u:
        return 1;
    case 0xF2u:
        return 2;
    case 0xF6u:
    case 0xF8u:
    case 0xFAu:
    case 0xFBu:
    case 0xFCu:
    case 0xFEu:
    case 0xFFu:
        return 0;
    default:
        return static_cast<size_t>(-1);
    }
}

MidiMessageType EventTypeFromStatus(uint8_t status) {
    if (status < 0x80u) {
        return MidiMessageType::kUnknown;
    }
    if (status < 0xF0u) {
        switch (status & 0xF0u) {
        case 0x80u: return MidiMessageType::kNoteOff;
        case 0x90u: return MidiMessageType::kNoteOn;
        case 0xA0u: return MidiMessageType::kPolyphonicAftertouch;
        case 0xB0u: return MidiMessageType::kControlChange;
        case 0xC0u: return MidiMessageType::kProgramChange;
        case 0xD0u: return MidiMessageType::kChannelAftertouch;
        case 0xE0u: return MidiMessageType::kPitchBend;
        default: return MidiMessageType::kUnknown;
        }
    }

    switch (status) {
    case 0xF0u: return MidiMessageType::kSysEx;
    case 0xF1u: return MidiMessageType::kTimeCodeQuarterFrame;
    case 0xF2u: return MidiMessageType::kSongPositionPointer;
    case 0xF3u: return MidiMessageType::kSongSelect;
    case 0xF6u: return MidiMessageType::kTuneRequest;
    case 0xF8u: return MidiMessageType::kTimingClock;
    case 0xFAu: return MidiMessageType::kStart;
    case 0xFBu: return MidiMessageType::kContinue;
    case 0xFCu: return MidiMessageType::kStop;
    case 0xFEu: return MidiMessageType::kActiveSensing;
    case 0xFFu: return MidiMessageType::kSystemReset;
    default:
        return IsRealtimeStatus(status) ? MidiMessageType::kSystemRealtime : MidiMessageType::kSystemCommon;
    }
}

MidiEvent BuildEvent(
    MidiMessageType type,
    uint8_t status,
    uint8_t data1,
    uint8_t data2,
    uint32_t tick,
    bool running_status,
    std::vector<uint8_t> message) {
    MidiEvent event;
    event.type = type;
    event.status = status;
    event.channel = status < 0xF0u ? static_cast<uint8_t>(status & 0x0Fu) : 0u;
    event.data1 = data1;
    event.data2 = data2;
    event.tick = tick;
    event.running_status = running_status;
    event.message = std::move(message);
    return event;
}

}  // namespace

bool MidiParser::ParseStream(const uint8_t* data, size_t bytes, std::vector<MidiEvent>& out) const {
    if (data == nullptr || bytes == 0) {
        return false;
    }

    out.clear();
    uint8_t running_status = 0;
    uint32_t tick = 0;
    size_t cursor = 0;
    while (cursor < bytes) {
        uint8_t byte = data[cursor];
        if (IsRealtimeStatus(byte)) {
            out.push_back(BuildEvent(EventTypeFromStatus(byte), byte, 0, 0, tick++, false, {byte}));
            ++cursor;
            continue;
        }

        uint8_t status = 0;
        bool used_running_status = false;
        if (IsStatusByte(byte)) {
            status = byte;
            ++cursor;
            if (status < 0xF0u) {
                running_status = status;
            } else if (status != 0xF7u) {
                running_status = 0;
            }
        } else {
            if (running_status == 0) {
                out.clear();
                return false;
            }
            status = running_status;
            used_running_status = true;
        }

        if (status == 0xF0u) {
            std::vector<uint8_t> message;
            message.push_back(status);
            while (cursor < bytes) {
                const uint8_t sysex_byte = data[cursor++];
                message.push_back(sysex_byte);
                if (sysex_byte == 0xF7u) {
                    break;
                }
            }
            if (message.back() != 0xF7u) {
                out.clear();
                return false;
            }
            const uint8_t data1 = message.size() > 1 ? message[1] : 0;
            const uint8_t data2 = message.size() > 2 ? message[2] : 0;
            out.push_back(BuildEvent(MidiMessageType::kSysEx, status, data1, data2, tick++, false, std::move(message)));
            continue;
        }

        const size_t data_length = DataLengthForStatus(status);
        if (data_length == static_cast<size_t>(-1) || cursor + data_length > bytes) {
            out.clear();
            return false;
        }

        std::vector<uint8_t> message;
        message.reserve(data_length + 1);
        message.push_back(status);
        uint8_t data1 = 0;
        uint8_t data2 = 0;
        if (data_length >= 1) {
            data1 = data[cursor++];
            if (IsStatusByte(data1)) {
                out.clear();
                return false;
            }
            message.push_back(data1);
        }
        if (data_length >= 2) {
            data2 = data[cursor++];
            if (IsStatusByte(data2)) {
                out.clear();
                return false;
            }
            message.push_back(data2);
        }

        out.push_back(BuildEvent(EventTypeFromStatus(status), status, data1, data2, tick++, used_running_status, std::move(message)));
    }

    return !out.empty();
}

std::string MidiParser::DescribeEventType(MidiMessageType type) {
    switch (type) {
    case MidiMessageType::kNoteOff: return "note_off";
    case MidiMessageType::kNoteOn: return "note_on";
    case MidiMessageType::kPolyphonicAftertouch: return "poly_aftertouch";
    case MidiMessageType::kControlChange: return "control_change";
    case MidiMessageType::kProgramChange: return "program_change";
    case MidiMessageType::kChannelAftertouch: return "channel_aftertouch";
    case MidiMessageType::kPitchBend: return "pitch_bend";
    case MidiMessageType::kSongPositionPointer: return "song_position_pointer";
    case MidiMessageType::kSongSelect: return "song_select";
    case MidiMessageType::kTimeCodeQuarterFrame: return "time_code_quarter_frame";
    case MidiMessageType::kTuneRequest: return "tune_request";
    case MidiMessageType::kStart: return "start";
    case MidiMessageType::kContinue: return "continue";
    case MidiMessageType::kStop: return "stop";
    case MidiMessageType::kTimingClock: return "timing_clock";
    case MidiMessageType::kActiveSensing: return "active_sensing";
    case MidiMessageType::kSystemReset: return "system_reset";
    case MidiMessageType::kSysEx: return "sysex";
    case MidiMessageType::kSystemCommon: return "system_common";
    case MidiMessageType::kSystemRealtime: return "system_realtime";
    case MidiMessageType::kUnknown:
    default:
        return "unknown";
    }
}

}  // namespace XER::Engine::Audio::MIDI
