// xer.XER.Application — UI event loop (XER_UI_Core::Application singleton;
// every `new XER.Application()` wraps the same instance).

#include "widget.h"

#include <memory>

#include "core/application.h"

namespace {

namespace UICore = XER::UI::Core;

class AppWrap : public Nan::ObjectWrap {
public:
    AppWrap() : app_(&UICore::Application::Instance()) {}

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    UICore::Application* Get() const { return app_; }

private:
    UICore::Application* app_;
};

void AppWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new AppWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

AppWrap* UnwrapApp(v8::Local<v8::Value> v) {
    if (v.IsEmpty() || !v->IsObject()) return nullptr;
    return Nan::ObjectWrap::Unwrap<AppWrap>(v.As<v8::Object>());
}

NAN_METHOD(AppInitialize) {
    auto* self = UnwrapApp(info.This());
    info.GetReturnValue().Set(Nan::New(self && self->Get()->Initialize()));
}

NAN_METHOD(AppRun) {
    auto* self = UnwrapApp(info.This());
    info.GetReturnValue().Set(Nan::New(self ? self->Get()->Run() : 1));
}

NAN_METHOD(AppShutdown) {
    auto* self = UnwrapApp(info.This());
    if (self) self->Get()->Shutdown();
}

NAN_METHOD(AppIsInitialized) {
    auto* self = UnwrapApp(info.This());
    info.GetReturnValue().Set(Nan::New(self && self->Get()->IsInitialized()));
}

NAN_METHOD(AppIsRunning) {
    auto* self = UnwrapApp(info.This());
    info.GetReturnValue().Set(Nan::New(self && self->Get()->IsRunning()));
}

NAN_METHOD(AppSetAppName) {
    auto* self = UnwrapApp(info.This());
    std::string name;
    if (self && xer::addon::ArgUtf8(info, 0, &name))
        self->Get()->SetAppName(name);
}

NAN_METHOD(AppGetAppName) {
    auto* self = UnwrapApp(info.This());
    info.GetReturnValue().Set(
        self ? Nan::New(self->Get()->GetAppName()).ToLocalChecked()
             : Nan::New("").ToLocalChecked());
}

}  // namespace

namespace xer::addon::ui {

void RegisterApplication(v8::Local<v8::Object> xer) {
    v8::Local<v8::FunctionTemplate> app_tpl =
        Nan::New<v8::FunctionTemplate>(AppWrap::New);
    app_tpl->InstanceTemplate()->SetInternalFieldCount(1);
    app_tpl->SetClassName(Nan::New("Application").ToLocalChecked());
    Nan::SetMethod(app_tpl->PrototypeTemplate(), "Initialize", AppInitialize);
    Nan::SetMethod(app_tpl->PrototypeTemplate(), "Run", AppRun);
    Nan::SetMethod(app_tpl->PrototypeTemplate(), "Shutdown", AppShutdown);
    Nan::SetMethod(app_tpl->PrototypeTemplate(), "IsInitialized",
                   AppIsInitialized);
    Nan::SetMethod(app_tpl->PrototypeTemplate(), "IsRunning", AppIsRunning);
    Nan::SetMethod(app_tpl->PrototypeTemplate(), "SetAppName", AppSetAppName);
    Nan::SetMethod(app_tpl->PrototypeTemplate(), "GetAppName", AppGetAppName);
    Nan::Set(xer, Nan::New("Application").ToLocalChecked(),
             Nan::GetFunction(app_tpl).ToLocalChecked());
}

}  // namespace xer::addon::ui
