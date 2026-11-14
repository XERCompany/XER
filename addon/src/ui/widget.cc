// xer.XER.Widget / xer.XER.Label — widget base class and Label widget.

#include "widget.h"

#include <string>

#include "widgets/widget.h"
#include "widgets/card.h"
#include "widgets/label.h"
#include "layout/geometry.h"

namespace {

namespace UIW = XER::UI::Widgets;
namespace UIL = XER::UI::Layout;

using xer::addon::ui::UnwrapWidget;
using xer::addon::ui::WidgetFromArg;
using xer::addon::ui::WidgetWrap;

// ---------------------------------------------------------------------------
// Widget methods (shared by every widget subclass via prototype chain)
// ---------------------------------------------------------------------------

NAN_METHOD(WidgetAddChild) {
    auto child = WidgetFromArg(info, 0);
    auto* self = UnwrapWidget(info.This());
    if (!self || !child) {
        Nan::ThrowError("widget.addChild(child) expects a XER widget");
        return;
    }
    self->Get()->AddChild(child);
}

NAN_METHOD(WidgetRemoveChild) {
    auto child = WidgetFromArg(info, 0);
    auto* self = UnwrapWidget(info.This());
    if (!self || !child) {
        Nan::ThrowError("widget.removeChild(child) expects a XER widget");
        return;
    }
    self->Get()->RemoveChild(child);
}

void SetRectFromArgs(const Nan::FunctionCallbackInfo<v8::Value>& info,
                     int x_idx, int y_idx, int w_idx, int h_idx,
                     bool keep_pos, bool keep_size) {
    auto* self = UnwrapWidget(info.This());
    if (!self) {
        Nan::ThrowError("widget bounds call on non-widget object");
        return;
    }
    UIL::Rect r = self->Get()->GetBounds();
    if (!keep_pos) {
        r.x = info[x_idx]->IsNumber() ? info[x_idx].As<v8::Number>()->Value() : r.x;
        r.y = info[y_idx]->IsNumber() ? info[y_idx].As<v8::Number>()->Value() : r.y;
    }
    if (!keep_size) {
        r.width = info[w_idx]->IsNumber()
                      ? info[w_idx].As<v8::Number>()->Value()
                      : r.width;
        r.height = info[h_idx]->IsNumber()
                       ? info[h_idx].As<v8::Number>()->Value()
                       : r.height;
    }
    self->Get()->SetBounds(r);
}

NAN_METHOD(WidgetSetPosition) { SetRectFromArgs(info, 0, 1, 2, 3, false, true); }

NAN_METHOD(WidgetSetSize) { SetRectFromArgs(info, 0, 1, 0, 1, true, false); }

NAN_METHOD(WidgetSetBounds) { SetRectFromArgs(info, 0, 1, 2, 3, false, false); }

NAN_METHOD(WidgetGetBounds) {
    auto* self = UnwrapWidget(info.This());
    if (!self) {
        Nan::ThrowError("widget.GetBounds on non-widget object");
        return;
    }
    const UIL::Rect r = self->Get()->GetBounds();
    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    Nan::Set(obj, Nan::New("x").ToLocalChecked(), Nan::New(r.x));
    Nan::Set(obj, Nan::New("y").ToLocalChecked(), Nan::New(r.y));
    Nan::Set(obj, Nan::New("width").ToLocalChecked(), Nan::New(r.width));
    Nan::Set(obj, Nan::New("height").ToLocalChecked(), Nan::New(r.height));
    info.GetReturnValue().Set(obj);
}

NAN_METHOD(WidgetSetVisible) {
    auto* self = UnwrapWidget(info.This());
    if (!self) return;
    self->Get()->SetVisible(
        info.Length() > 0 && info[0]->IsBoolean()
            ? info[0].As<v8::Boolean>()->Value()
            : true);
}

NAN_METHOD(WidgetInvalidate) {
    auto* self = UnwrapWidget(info.This());
    if (self) self->Get()->Invalidate();
}

NAN_METHOD(WidgetIsVisible) {
    auto* self = UnwrapWidget(info.This());
    info.GetReturnValue().Set(
        Nan::New(self && self->Get()->IsVisible()));
}

NAN_METHOD(WidgetSetEnabled) {
    auto* self = UnwrapWidget(info.This());
    if (!self) return;
    self->Get()->SetEnabled(
        info.Length() > 0 && info[0]->IsBoolean()
            ? info[0].As<v8::Boolean>()->Value()
            : true);
}

NAN_METHOD(WidgetSetTooltip) {
    auto* self = UnwrapWidget(info.This());
    std::string text;
    if (!self || !xer::addon::ArgUtf8(info, 0, &text)) return;
    self->Get()->SetTooltip(text);
}

NAN_METHOD(WidgetGetTooltip) {
    auto* self = UnwrapWidget(info.This());
    if (!self) {
        info.GetReturnValue().Set(Nan::New("").ToLocalChecked());
        return;
    }
    info.GetReturnValue().Set(
        Nan::New(self->Get()->GetTooltip()).ToLocalChecked());
}

NAN_METHOD(WidgetShowOverlay) {
    auto overlay = WidgetFromArg(info, 0);
    auto* self = UnwrapWidget(info.This());
    if (!self || !overlay) {
        Nan::ThrowError("widget.ShowOverlay(overlay) expects a XER widget");
        return;
    }
    self->Get()->ShowOverlay(overlay);
}

NAN_METHOD(WidgetHideOverlay) {
    auto* self = UnwrapWidget(info.This());
    if (self) self->Get()->HideOverlay();
}

// ---------------------------------------------------------------------------
// Label text accessors (reachable only on Label instances)
// ---------------------------------------------------------------------------

NAN_METHOD(LabelSetText) {
    std::string text;
    if (!xer::addon::ArgUtf8(info, 0, &text)) {
        Nan::ThrowError("label.SetText(text) expects a string");
        return;
    }
    auto* self = UnwrapWidget(info.This());
    if (!self) return;
    if (auto* lbl = dynamic_cast<UIW::Label*>(self->Get().get())) {
        lbl->SetText(text);
    }
}

NAN_METHOD(LabelGetText) {
    auto* self = UnwrapWidget(info.This());
    if (!self) {
        info.GetReturnValue().Set(Nan::New("").ToLocalChecked());
        return;
    }
    if (auto* lbl = dynamic_cast<UIW::Label*>(self->Get().get())) {
        info.GetReturnValue().Set(Nan::New(lbl->GetText()).ToLocalChecked());
        return;
    }
    info.GetReturnValue().Set(Nan::New("").ToLocalChecked());
}

void AddProtoMethods(v8::Local<v8::FunctionTemplate> tpl) {
    Nan::SetMethod(tpl->PrototypeTemplate(), "addChild", WidgetAddChild);
    Nan::SetMethod(tpl->PrototypeTemplate(), "removeChild", WidgetRemoveChild);
    Nan::SetMethod(tpl->PrototypeTemplate(), "SetPosition", WidgetSetPosition);
    Nan::SetMethod(tpl->PrototypeTemplate(), "SetSize", WidgetSetSize);
    Nan::SetMethod(tpl->PrototypeTemplate(), "SetBounds", WidgetSetBounds);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetBounds", WidgetGetBounds);
    Nan::SetMethod(tpl->PrototypeTemplate(), "SetVisible", WidgetSetVisible);
    Nan::SetMethod(tpl->PrototypeTemplate(), "IsVisible", WidgetIsVisible);
    Nan::SetMethod(tpl->PrototypeTemplate(), "SetEnabled", WidgetSetEnabled);
    Nan::SetMethod(tpl->PrototypeTemplate(), "SetTooltip", WidgetSetTooltip);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetTooltip", WidgetGetTooltip);
    Nan::SetMethod(tpl->PrototypeTemplate(), "ShowOverlay", WidgetShowOverlay);
    Nan::SetMethod(tpl->PrototypeTemplate(), "HideOverlay", WidgetHideOverlay);
    Nan::SetMethod(tpl->PrototypeTemplate(), "Invalidate", WidgetInvalidate);
}

}  // namespace

namespace xer::addon::ui {

// ---------------------------------------------------------------------------
// WidgetWrap
// ---------------------------------------------------------------------------

WidgetWrap::WidgetWrap(std::shared_ptr<XER::UI::Widgets::Widget> w,
                       std::shared_ptr<CallbackStore> callbacks)
    : widget_(std::move(w)),
      callbacks_(callbacks ? std::move(callbacks)
                           : std::make_shared<CallbackStore>()) {}

WidgetWrap::~WidgetWrap() { callbacks_->Clear(); }

std::shared_ptr<XER::UI::Widgets::Widget> WidgetWrap::Get() const {
    return widget_;
}

void WidgetWrap::Adopt(const Nan::FunctionCallbackInfo<v8::Value>& info,
                       std::shared_ptr<XER::UI::Widgets::Widget> w,
                       std::shared_ptr<CallbackStore> callbacks) {
    auto* wrap = new WidgetWrap(std::move(w), std::move(callbacks));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

v8::Local<v8::Object> WidgetWrap::ToJS(
    std::shared_ptr<XER::UI::Widgets::Widget> w) {
    v8::Local<v8::Context> ctx = Nan::GetCurrentContext();
    v8::Local<v8::Object> obj = WidgetBaseTemplate()
                                   ->InstanceTemplate()
                                   ->NewInstance(ctx)
                                   .ToLocalChecked();
    auto* wrap = new WidgetWrap(std::move(w));
    wrap->Wrap(obj);
    return obj;
}

void WidgetWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    Adopt(info, std::make_shared<XER::UI::Widgets::Card>());
}

void WidgetWrap::NewLabel(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    std::string text;
    xer::addon::ArgUtf8(info, 0, &text);
    const bool muted = xer::addon::HasArg(info, 1) &&
                       info[1]->IsBoolean() &&
                       info[1].As<v8::Boolean>()->Value();
    Adopt(info, std::make_shared<XER::UI::Widgets::Label>(text, muted));
}

WidgetWrap* UnwrapWidget(v8::Local<v8::Value> v) {
    if (v.IsEmpty() || !v->IsObject()) return nullptr;
    return Nan::ObjectWrap::Unwrap<WidgetWrap>(v.As<v8::Object>());
}

namespace {
v8::Global<v8::FunctionTemplate>& WidgetBaseTemplateStore() {
    static v8::Global<v8::FunctionTemplate> tpl;
    return tpl;
}
}  // namespace

v8::Local<v8::FunctionTemplate> WidgetBaseTemplate() {
    return v8::Local<v8::FunctionTemplate>::New(
        v8::Isolate::GetCurrent(), WidgetBaseTemplateStore());
}

std::shared_ptr<XER::UI::Widgets::Widget> WidgetFromValue(
    v8::Local<v8::Value> v) {
    WidgetWrap* wrap = UnwrapWidget(v);
    return wrap ? wrap->Get() : nullptr;
}

std::shared_ptr<XER::UI::Widgets::Widget> WidgetFromArg(
    const Nan::FunctionCallbackInfo<v8::Value>& info, int index) {
    return WidgetFromValue(
        index < info.Length() ? info[index] : v8::Local<v8::Value>());
}

void RegisterWidgets(v8::Local<v8::Object> xer) {
    v8::Local<v8::FunctionTemplate> widget_tpl =
        Nan::New<v8::FunctionTemplate>(WidgetWrap::New);
    widget_tpl->InstanceTemplate()->SetInternalFieldCount(1);
    widget_tpl->SetClassName(Nan::New("Widget").ToLocalChecked());
    AddProtoMethods(widget_tpl);
    WidgetBaseTemplateStore().Reset(v8::Isolate::GetCurrent(), widget_tpl);

    v8::Local<v8::FunctionTemplate> label_tpl =
        Nan::New<v8::FunctionTemplate>(WidgetWrap::NewLabel);
    label_tpl->InstanceTemplate()->SetInternalFieldCount(1);
    label_tpl->SetClassName(Nan::New("Label").ToLocalChecked());
    label_tpl->Inherit(widget_tpl);
    AddProtoMethods(label_tpl);
    Nan::SetMethod(label_tpl->PrototypeTemplate(), "SetText", LabelSetText);
    Nan::SetMethod(label_tpl->PrototypeTemplate(), "GetText", LabelGetText);

    Nan::Set(xer, Nan::New("Widget").ToLocalChecked(),
             Nan::GetFunction(widget_tpl).ToLocalChecked());
    Nan::Set(xer, Nan::New("Label").ToLocalChecked(),
             Nan::GetFunction(label_tpl).ToLocalChecked());
}

}  // namespace xer::addon::ui
