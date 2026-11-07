#include "midi_sysex_handler.h"

namespace XER::Engine::Audio::MIDI {

bool MidiSysexHandler::IsValidSysex(const std::vector<uint8_t>& msg) const {
    return msg.size() >= 3 && msg.front() == 0xF0 && msg.back() == 0xF7;
}

bool MidiSysexHandler::Parse(const std::vector<uint8_t>& msg, MidiSysexMessage* out) const {
    if (out == nullptr || !IsValidSysex(msg)) {
        return false;
    }

    *out = MidiSysexMessage{};
    if (msg[1] == 0x7E || msg[1] == 0x7F) {
        if (msg.size() < 6) {
            return false;
        }
        out->universal_non_realtime = msg[1] == 0x7E;
        out->universal_realtime = msg[1] == 0x7F;
        out->manufacturer_id = msg[1];
        out->device_id = msg[2];
        out->sub_id1 = msg[3];
        out->sub_id2 = msg[4];
        out->payload.assign(msg.begin() + 5, msg.end() - 1);
        return true;
    }

    if (msg[1] == 0x00) {
        if (msg.size() < 6) {
            return false;
        }
        out->manufacturer_id = (static_cast<uint32_t>(msg[1]) << 16)
            | (static_cast<uint32_t>(msg[2]) << 8)
            | static_cast<uint32_t>(msg[3]);
        out->payload.assign(msg.begin() + 4, msg.end() - 1);
        return true;
    }

    out->manufacturer_id = msg[1];
    out->payload.assign(msg.begin() + 2, msg.end() - 1);
    return true;
}

std::vector<uint8_t> MidiSysexHandler::BuildIdentityRequest(uint8_t device_id) const {
    return {0xF0, 0x7E, device_id, 0x06, 0x01, 0xF7};
}

std::vector<uint8_t> MidiSysexHandler::BuildIdentityReply(
    uint8_t device_id,
    uint32_t manufacturer_id,
    uint16_t family_code,
    uint16_t model_number,
    uint32_t version) const {
    std::vector<uint8_t> reply = {0xF0, 0x7E, device_id, 0x06, 0x02};
    if (manufacturer_id > 0xFFu) {
        reply.push_back(static_cast<uint8_t>((manufacturer_id >> 16) & 0x7Fu));
        reply.push_back(static_cast<uint8_t>((manufacturer_id >> 8) & 0x7Fu));
        reply.push_back(static_cast<uint8_t>(manufacturer_id & 0x7Fu));
    } else {
        reply.push_back(static_cast<uint8_t>(manufacturer_id & 0x7Fu));
    }
    reply.push_back(static_cast<uint8_t>(family_code & 0x7Fu));
    reply.push_back(static_cast<uint8_t>((family_code >> 8) & 0x7Fu));
    reply.push_back(static_cast<uint8_t>(model_number & 0x7Fu));
    reply.push_back(static_cast<uint8_t>((model_number >> 8) & 0x7Fu));
    reply.push_back(static_cast<uint8_t>(version & 0x7Fu));
    reply.push_back(static_cast<uint8_t>((version >> 8) & 0x7Fu));
    reply.push_back(static_cast<uint8_t>((version >> 16) & 0x7Fu));
    reply.push_back(static_cast<uint8_t>((version >> 24) & 0x7Fu));
    reply.push_back(0xF7);
    return reply;
}

}  // namespace XER::Engine::Audio::MIDI
