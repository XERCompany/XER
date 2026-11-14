// xer.node — Node.js NAN bindings for the core XER API.
//
//   xer.info()                  runtime/platform info
//   xer.image.{info,decode,save}
//   xer.audio.{info,convert}

#include "xer_bind.h"

#include <node_version.h>

#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"

namespace {

#if defined(_WIN32)
constexpr const char* kPlatform = "win32";
#elif defined(__APPLE__)
constexpr const char* kPlatform = "darwin";
#else
constexpr const char* kPlatform = "linux";
#endif

#if defined(_M_X64) || defined(__x86_64__)
constexpr const char* kArch = "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
constexpr const char* kArch = "arm64";
#elif defined(_M_IX86) || defined(__i386__)
constexpr const char* kArch = "ia32";
#else
constexpr const char* kArch = "unknown";
#endif

NAN_METHOD(XerInfo) {
    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    Nan::Set(obj, Nan::New("node").ToLocalChecked(),
             Nan::New(NODE_VERSION_STRING));
    Nan::Set(obj, Nan::New("platform").ToLocalChecked(), Nan::New(kPlatform));
    Nan::Set(obj, Nan::New("arch").ToLocalChecked(), Nan::New(kArch));
    Nan::Set(obj, Nan::New("xer").ToLocalChecked(), Nan::New("xer3-addon"));
    info.GetReturnValue().Set(obj);
}

}  // namespace

NAN_MODULE_INIT(Init) {
    // Keep FFmpeg quiet inside the host process (same policy as audioplayer).
    XER::Bridge::FFmpeg::SetLogLevel(XER::Bridge::FFmpeg::LogLevel::kError);

    Nan::SetMethod(target, "info", XerInfo);

    v8::Local<v8::Object> image = Nan::New<v8::Object>();
    xer::addon::InitImage(image);
    Nan::Set(target, Nan::New("image").ToLocalChecked(), image);

    v8::Local<v8::Object> audio = Nan::New<v8::Object>();
    xer::addon::InitAudio(audio);
    Nan::Set(target, Nan::New("audio").ToLocalChecked(), audio);
}

NODE_MODULE(xeraddon, Init)
