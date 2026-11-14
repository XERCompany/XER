// xer.audio.core.Logger — AudioLogger binding (process-wide singleton).

#include "logger.h"

#include "audio/audio_core/audio_logger.h"

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioLogger;

LoggerWrap::LoggerWrap() = default;
LoggerWrap::~LoggerWrap() = default;

void LoggerWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new LoggerWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void LoggerWrap::LogEngineStart(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogEngineStart(
        IntArg(info, 0, 44100), IntArg(info, 1, 512));
}

void LoggerWrap::LogEngineStop(
    const Nan::FunctionCallbackInfo<v8::Value>&) {
    AudioLogger::Instance().LogEngineStop();
}

void LoggerWrap::LogEngineError(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogEngineError(StrArg(info, 0));
}

void LoggerWrap::LogEffectLoad(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogEffectLoad(StrArg(info, 0));
}

void LoggerWrap::LogEffectProcess(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogEffectProcess(
        StrArg(info, 0), NumArg(info, 1, 0.0));
}

void LoggerWrap::LogEffectError(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogEffectError(StrArg(info, 0), StrArg(info, 1));
}

void LoggerWrap::LogAnalysisStart(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogAnalysisStart(StrArg(info, 0));
}

void LoggerWrap::LogAnalysisComplete(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogAnalysisComplete(
        StrArg(info, 0), NumArg(info, 1, 0.0));
}

void LoggerWrap::LogAnalysisError(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogAnalysisError(StrArg(info, 0), StrArg(info, 1));
}

void LoggerWrap::LogFileLoad(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogFileLoad(
        StrArg(info, 0), static_cast<size_t>(IntArg(info, 1, 0)));
}

void LoggerWrap::LogFileSave(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogFileSave(
        StrArg(info, 0), static_cast<size_t>(IntArg(info, 1, 0)));
}

void LoggerWrap::LogFileError(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogFileError(StrArg(info, 0), StrArg(info, 1));
}

void LoggerWrap::LogBufferAllocation(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogBufferAllocation(
        static_cast<size_t>(IntArg(info, 0, 0)));
}

void LoggerWrap::LogBufferDeallocation(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogBufferDeallocation(
        static_cast<size_t>(IntArg(info, 0, 0)));
}

void LoggerWrap::LogFFTOperation(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogFFTOperation(
        static_cast<size_t>(IntArg(info, 0, 0)), NumArg(info, 1, 0.0));
}

void LoggerWrap::LogFilterOperation(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    AudioLogger::Instance().LogFilterOperation(
        StrArg(info, 0), NumArg(info, 1, 0.0));
}


void InitCoreLogger(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Logger", LoggerWrap::New,
        {{"LogEngineStart", LoggerWrap::LogEngineStart},
         {"LogEngineStop", LoggerWrap::LogEngineStop},
         {"LogEngineError", LoggerWrap::LogEngineError},
         {"LogEffectLoad", LoggerWrap::LogEffectLoad},
         {"LogEffectProcess", LoggerWrap::LogEffectProcess},
         {"LogEffectError", LoggerWrap::LogEffectError},
         {"LogAnalysisStart", LoggerWrap::LogAnalysisStart},
         {"LogAnalysisComplete", LoggerWrap::LogAnalysisComplete},
         {"LogAnalysisError", LoggerWrap::LogAnalysisError},
         {"LogFileLoad", LoggerWrap::LogFileLoad},
         {"LogFileSave", LoggerWrap::LogFileSave},
         {"LogFileError", LoggerWrap::LogFileError},
         {"LogBufferAllocation", LoggerWrap::LogBufferAllocation},
         {"LogBufferDeallocation", LoggerWrap::LogBufferDeallocation},
         {"LogFFTOperation", LoggerWrap::LogFFTOperation},
         {"LogFilterOperation", LoggerWrap::LogFilterOperation}});
}

} // namespace xer::addon::audio::core
