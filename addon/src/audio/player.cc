// xer.audio.Player — WASAPI audio player class binding.
// Load() accepts ONLY xer.audio.Buffer instances.

#include "player.h"

#include <string>
#include <utility>
#include <vector>

#include "buffer.h"

// Defensive: a later include path could re-define the Win32 macro.
#undef GetCurrentTime

namespace xer::addon::audio {

namespace {

using XER::Content::Audio::AudioPlayer;
using XER::Content::Audio::PlaybackState;

bool ArgNum(const Nan::FunctionCallbackInfo<v8::Value>& info, int index,
            double fallback, double* out) {
    if (index < info.Length() && info[index]->IsNumber()) {
        *out = info[index].As<v8::Number>()->Value();
        return true;
    }
    *out = fallback;
    return false;
}

const char* StateName(PlaybackState s) {
    switch (s) {
        case PlaybackState::kPlaying: return "playing";
        case PlaybackState::kPaused: return "paused";
        case PlaybackState::kStopped: return "stopped";
        case PlaybackState::kError: return "error";
        default: return "idle";
    }
}

PlayerWrap* Unwrap(v8::Local<v8::Value> v) {
    if (v.IsEmpty() || !v->IsObject()) return nullptr;
    v8::Local<v8::Object> obj = v.As<v8::Object>();
    if (obj->InternalFieldCount() < 1) return nullptr;
    return Nan::ObjectWrap::Unwrap<PlayerWrap>(obj);
}

} // namespace

PlayerWrap::PlayerWrap(std::shared_ptr<AudioPlayer> player)
    : player_(std::move(player)),
      callbacks_(std::make_shared<CallbackStore>()) {}

PlayerWrap::~PlayerWrap() {
    // Same teardown hazard as WidgetWrap: ~AudioPlayer touches WASAPI
    // threads/singletons and CallbackStore::Clear() calls
    // Nan::Persistent::Reset() — both unsafe once the isolate is disposing.
    // Leak both; bounded (one per live Player at exit).
    static auto* leaked_players =
        new std::vector<std::shared_ptr<AudioPlayer>>();
    static auto* leaked_stores =
        new std::vector<std::shared_ptr<CallbackStore>>();
    leaked_players->push_back(std::move(player_));
    leaked_stores->push_back(std::move(callbacks_));
}

PlayerWrap* UnwrapPlayer(v8::Local<v8::Value> v) { return Unwrap(v); }

// --- ctor --------------------------------------------------------------------

void PlayerWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new PlayerWrap(std::make_shared<AudioPlayer>());
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

// --- lifecycle ---------------------------------------------------------------

void PlayerWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    if (!w || !w->Get()) return;
    double rate = 44100.0, ch = 2.0;
    ArgNum(info, 0, 44100.0, &rate);
    ArgNum(info, 1, 2.0, &ch);
    info.GetReturnValue().Set(Nan::New(
        w->Get()->Initialize(static_cast<int>(rate),
                             static_cast<int>(ch))));
}

void PlayerWrap::IsInitialized(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->Get() && w->Get()->IsInitialized()));
}

// Load(buffer) — the ONLY input: a xer.audio.Buffer instance.
void PlayerWrap::Load(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    if (!w || !w->Get() || info.Length() < 1) {
        info.GetReturnValue().Set(Nan::New(false));
        return;
    }
    auto* bw = UnwrapBuffer(info[0]);
    if (!bw || !bw->Get()) {
        Nan::ThrowError("Player.Load expects a xer.audio.Buffer");
        return;
    }
    // Playback owns its own copy of the decoded samples.
    XER::Audio::Core::AudioSourceBuffer copy = *bw->Get();
    info.GetReturnValue().Set(
        Nan::New(w->Get()->LoadFromBuffer(std::move(copy))));
}

void PlayerWrap::IsLoaded(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->Get() && w->Get()->IsLoaded()));
}

// --- transport ---------------------------------------------------------------

void PlayerWrap::Play(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->Get() && w->Get()->Play()));
}

void PlayerWrap::Pause(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->Get() && w->Get()->Pause()));
}

void PlayerWrap::Stop(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->Get() && w->Get()->Stop()));
}

void PlayerWrap::Seek(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    double t = 0.0;
    ArgNum(info, 0, 0.0, &t);
    info.GetReturnValue().Set(
        Nan::New(w && w->Get() && w->Get()->Seek(t)));
}

void PlayerWrap::GetState(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    PlaybackState s =
        w && w->Get() ? w->Get()->GetState() : PlaybackState::kStopped;
    info.GetReturnValue().Set(
        Nan::New(StateName(s)).ToLocalChecked());
}

void PlayerWrap::GetCurrentTime(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    info.GetReturnValue().Set(Nan::New(
        w && w->Get() ? w->Get()->GetCurrentTime() : 0.0));
}

void PlayerWrap::GetDuration(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    info.GetReturnValue().Set(Nan::New(
        w && w->Get() ? w->Get()->GetDuration() : 0.0));
}

// --- volume / format ---------------------------------------------------------

void PlayerWrap::GetVolume(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    info.GetReturnValue().Set(Nan::New(
        w && w->Get() ? static_cast<double>(w->Get()->GetVolume())
                      : 1.0));
}

void PlayerWrap::SetVolume(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    double v = 1.0;
    ArgNum(info, 0, 1.0, &v);
    if (w && w->Get()) w->Get()->SetVolume(static_cast<float>(v));
}

void PlayerWrap::GetSampleRate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->Get() ? w->Get()->GetSampleRate() : 0));
}

void PlayerWrap::GetChannelCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->Get() ? w->Get()->GetChannelCount() : 0));
}

void PlayerWrap::SetFadeInOut(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    if (!w || !w->Get()) return;
    double fi = 0.0, fo = 0.0;
    ArgNum(info, 0, 0.0, &fi);
    ArgNum(info, 1, 0.0, &fo);
    w->Get()->SetFadeInOut(fi, fo);
}

// --- callbacks ---------------------------------------------------------------

void PlayerWrap::SetOnPosition(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    if (!w || !w->Get()) return;
    auto store = w->Callbacks();
    if (store && info.Length() > 0 && info[0]->IsFunction()) {
        store->Set("position", info[0].As<v8::Function>());
        w->Get()->SetPositionCallback(
            [store](double cur, double dur) {
                store->Emit("position", cur, dur);
            });
    } else if (store) {
        store->Remove("position");
        w->Get()->SetPositionCallback(nullptr);
    }
}

void PlayerWrap::SetOnState(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapPlayer(info.This());
    if (!w || !w->Get()) return;
    auto store = w->Callbacks();
    if (store && info.Length() > 0 && info[0]->IsFunction()) {
        store->Set("state", info[0].As<v8::Function>());
        w->Get()->SetStateCallback(
            [store](PlaybackState s) {
                store->Emit("state", StateName(s));
            });
    } else if (store) {
        store->Remove("state");
        w->Get()->SetStateCallback(nullptr);
    }
}

// --- registration ------------------------------------------------------------

void InitAudioPlayer(v8::Local<v8::Object> audio_ns) {
    v8::Local<v8::FunctionTemplate> tpl =
        Nan::New<v8::FunctionTemplate>(PlayerWrap::New);
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    tpl->SetClassName(Nan::New("Player").ToLocalChecked());

    Nan::SetMethod(tpl->PrototypeTemplate(), "Initialize",
                   PlayerWrap::Initialize);
    Nan::SetMethod(tpl->PrototypeTemplate(), "IsInitialized",
                   PlayerWrap::IsInitialized);
    Nan::SetMethod(tpl->PrototypeTemplate(), "Load", PlayerWrap::Load);
    Nan::SetMethod(tpl->PrototypeTemplate(), "IsLoaded",
                   PlayerWrap::IsLoaded);
    Nan::SetMethod(tpl->PrototypeTemplate(), "Play", PlayerWrap::Play);
    Nan::SetMethod(tpl->PrototypeTemplate(), "Pause", PlayerWrap::Pause);
    Nan::SetMethod(tpl->PrototypeTemplate(), "Stop", PlayerWrap::Stop);
    Nan::SetMethod(tpl->PrototypeTemplate(), "Seek", PlayerWrap::Seek);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetState",
                   PlayerWrap::GetState);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetCurrentTime",
                   PlayerWrap::GetCurrentTime);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetDuration",
                   PlayerWrap::GetDuration);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetVolume",
                   PlayerWrap::GetVolume);
    Nan::SetMethod(tpl->PrototypeTemplate(), "SetVolume",
                   PlayerWrap::SetVolume);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetSampleRate",
                   PlayerWrap::GetSampleRate);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetChannelCount",
                   PlayerWrap::GetChannelCount);
    Nan::SetMethod(tpl->PrototypeTemplate(), "SetFadeInOut",
                   PlayerWrap::SetFadeInOut);
    Nan::SetMethod(tpl->PrototypeTemplate(), "SetOnPosition",
                   PlayerWrap::SetOnPosition);
    Nan::SetMethod(tpl->PrototypeTemplate(), "SetOnState",
                   PlayerWrap::SetOnState);

    Nan::Set(audio_ns, Nan::New("Player").ToLocalChecked(),
             Nan::GetFunction(tpl).ToLocalChecked());
}

} // namespace xer::addon::audio
