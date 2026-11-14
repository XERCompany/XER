// xer.notify.* — native desktop toast notifications (Steam-style overlay).
//
//   xer.notify.show(title, message[, {type, duration, icon}])
//   xer.notify.info/success/warning/error(title, message[, duration|opts])
//   xer.notify.clear()

#include "../util.h"

#include "overlays/toast.h"

namespace xer::addon {
namespace {

using XER::UI::Overlays::ToastManager;
using XER::UI::Overlays::ToastOptions;
using XER::UI::Overlays::ToastType;

ToastType TypeFromJS(v8::Local<v8::Value> v, ToastType fallback) {
    if (v->IsString()) {
        Nan::Utf8String s(v);
        const std::string str(*s, static_cast<std::size_t>(s.length()));
        if (str == "success") return ToastType::Success;
        if (str == "warning") return ToastType::Warning;
        if (str == "error") return ToastType::Error;
        if (str == "info") return ToastType::Info;
    }
    if (v->IsNumber()) {
        const int n = Nan::To<int>(v).FromMaybe(0);
        if (n >= 0 && n <= 3) return static_cast<ToastType>(n);
    }
    return fallback;
}

ToastOptions ParseOptions(v8::Local<v8::Value> arg, ToastType type) {
    ToastOptions opt;
    opt.type = type;
    if (!arg->IsObject()) return opt;
    v8::Local<v8::Object> obj = arg.As<v8::Object>();

    v8::Local<v8::Value> title;
    if (Nan::Get(obj, Nan::New("title").ToLocalChecked()).ToLocal(&title) &&
        title->IsString()) {
        Nan::Utf8String s(title);
        opt.title = std::string(*s, static_cast<std::size_t>(s.length()));
    }
    v8::Local<v8::Value> message;
    if (Nan::Get(obj, Nan::New("message").ToLocalChecked())
            .ToLocal(&message) &&
        message->IsString()) {
        Nan::Utf8String s(message);
        opt.message =
            std::string(*s, static_cast<std::size_t>(s.length()));
    }
    v8::Local<v8::Value> t;
    if (Nan::Get(obj, Nan::New("type").ToLocalChecked()).ToLocal(&t)) {
        opt.type = TypeFromJS(t, opt.type);
    }
    v8::Local<v8::Value> d;
    if (Nan::Get(obj, Nan::New("duration").ToLocalChecked()).ToLocal(&d) &&
        d->IsNumber()) {
        opt.duration_ms = Nan::To<int>(d).FromMaybe(opt.duration_ms);
    }
    v8::Local<v8::Value> icon;
    if (Nan::Get(obj, Nan::New("icon").ToLocalChecked()).ToLocal(&icon) &&
        icon->IsString()) {
        Nan::Utf8String s(icon);
        opt.icon_path =
            std::string(*s, static_cast<std::size_t>(s.length()));
    }
    return opt;
}

// Shared implementation for show/info/success/warning/error.
//   (title, message[, durationMs|optionsObject])
void NotifyTyped(const Nan::FunctionCallbackInfo<v8::Value>& info,
                 ToastType type) {
    std::string title, message;
    if (!ArgUtf8(info, 0, &title) || !ArgUtf8(info, 1, &message)) {
        ThrowUsage("notify: expected (title, message[, options])");
        return;
    }

    ToastOptions opt;
    opt.title = title;
    opt.message = message;
    opt.type = type;

    if (HasArg(info, 2)) {
        v8::Local<v8::Value> arg = info[2];
        if (arg->IsObject() && !arg->IsNumber()) {
            ToastOptions parsed = ParseOptions(arg, type);
            parsed.title = title;
            parsed.message = message;
            opt = parsed;
        } else if (arg->IsNumber()) {
            opt.duration_ms = Nan::To<int>(arg).FromMaybe(opt.duration_ms);
        }
    }

    ToastManager::Instance().Show(std::move(opt));
}

NAN_METHOD(NotifyShow) {
    std::string title, message;
    if (!ArgUtf8(info, 0, &title) || !ArgUtf8(info, 1, &message)) {
        ThrowUsage("notify.show: expected (title, message[, options])");
        return;
    }
    ToastOptions opt = ParseOptions(HasArg(info, 2) ? info[2]
                                                 : Nan::Undefined(),
                                    ToastType::Info);
    opt.title = title;
    opt.message = message;
    ToastManager::Instance().Show(std::move(opt));
}

NAN_METHOD(NotifyInfo)    { NotifyTyped(info, ToastType::Info); }
NAN_METHOD(NotifySuccess) { NotifyTyped(info, ToastType::Success); }
NAN_METHOD(NotifyWarning) { NotifyTyped(info, ToastType::Warning); }
NAN_METHOD(NotifyError)   { NotifyTyped(info, ToastType::Error); }

NAN_METHOD(NotifyClear) {
    ToastManager::Instance().Clear();
}

} // namespace

void InitNotify(v8::Local<v8::Object> exports) {
    Nan::SetMethod(exports, "show", NotifyShow);
    Nan::SetMethod(exports, "info", NotifyInfo);
    Nan::SetMethod(exports, "success", NotifySuccess);
    Nan::SetMethod(exports, "warning", NotifyWarning);
    Nan::SetMethod(exports, "error", NotifyError);
    Nan::SetMethod(exports, "clear", NotifyClear);
}

} // namespace xer::addon
