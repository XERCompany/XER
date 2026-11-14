#pragma once

// xer.audio.core.Logger — wraps XER::Audio::Core::AudioLogger (singleton).

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class LoggerWrap : public Nan::ObjectWrap {
public:
    explicit LoggerWrap();
    ~LoggerWrap() override;

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);

    static void LogEngineStart(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogEngineStop(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogEngineError(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogEffectLoad(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogEffectProcess(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogEffectError(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogAnalysisStart(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogAnalysisComplete(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogAnalysisError(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogFileLoad(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogFileSave(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogFileError(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogBufferAllocation(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogBufferDeallocation(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogFFTOperation(const Nan::FunctionCallbackInfo<v8::Value>&);
    static void LogFilterOperation(const Nan::FunctionCallbackInfo<v8::Value>&);
};

void InitCoreLogger(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
