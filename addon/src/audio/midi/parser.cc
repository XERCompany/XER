// xer.audio.midi.Parser — MidiParser binding.

#include "parser.h"

#include <vector>
#include <string>

namespace xer::addon::audio::midi {

using XER::Audio::MIDI::MidiParser;

ParserWrap::ParserWrap() = default;
ParserWrap::~ParserWrap() = default;

ParserWrap* UnwrapParser(v8::Local<v8::Value> v) {
    return UnwrapT<ParserWrap>(v);
}

void ParserWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ParserWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ParserWrap::Parse(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParser(info.This());
    if (!w) return;
    std::vector<uint8_t> in;
    if (!U8FromJS(info[0], &in)) {
        Nan::ThrowError("Parser.Parse expects Uint8Array");
        return;
    }
    std::vector<MidiEvent> out;
    if (!w->Get().ParseStream(in.data(), in.size(), out)) return;
    info.GetReturnValue().Set(MidiEventsToJS(out));
}

void ParserWrap::DescribeType(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParser(info.This());
    info.GetReturnValue().Set(Nan::New(XER::Audio::MIDI::MidiParser::DescribeEventType(static_cast<XER::Audio::MIDI::MidiMessageType>(IntArg(info, 0)))).ToLocalChecked());
}


void InitMidiParser(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Parser", ParserWrap::New,
        {{"Parse", ParserWrap::Parse},
         {"DescribeType", ParserWrap::DescribeType}});
}

} // namespace xer::addon::audio::midi
