// xer.UI.* — kit primitives: Kbd, Marker, AspectRatio, ButtonGroup,
// InputGroup, Field, Item, Empty, Collapsible.

#include "../widget.h"
#include "../../convert.h"

#include <memory>
#include <string>
#include <utility>

#include "widgets/kbd.h"
#include "widgets/marker.h"
#include "widgets/aspect_ratio.h"
#include "widgets/button_group.h"
#include "widgets/input_group.h"
#include "widgets/field.h"
#include "widgets/item.h"
#include "widgets/empty.h"
#include "widgets/collapsible.h"
#include "widgets/direction.h"
#include "widgets/form.h"
#include "widgets/attachment.h"
#include "widgets/bubble.h"
#include "widgets/message.h"
#include "widgets/message_scroller.h"
#include "widgets/input_otp.h"
#include "widgets/hover_card.h"

namespace {

namespace UIW = XER::UI::Widgets;
using xer::addon::HasArg;
using xer::addon::ArgUtf8;
using xer::addon::NumberOr;
using xer::addon::BoolOr;
using xer::addon::EnumArg;
using xer::addon::ui::WidgetWrap;
using xer::addon::ui::WidgetFromArg;
using xer::addon::ui::CastWidget;
using xer::addon::ui::BindWidgetCallback;
using xer::addon::ui::EmitFn;
using xer::addon::ui::RegisterWidgetClass;
using xer::addon::CallbackStore;

double NumArg(const Nan::FunctionCallbackInfo<v8::Value>& info, int index,
              double fallback) {
    return HasArg(info, index) ? NumberOr(info[index], fallback) : fallback;
}

bool BoolArg(const Nan::FunctionCallbackInfo<v8::Value>& info, int index,
             bool fallback) {
    return HasArg(info, index) ? BoolOr(info[index], fallback) : fallback;
}

std::string StrArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                   int index) {
    std::string s;
    ArgUtf8(info, index, &s);
    return s;
}

// String get/set pairs — one macro keeps the nine widgets compact.
#define XER_CAT2(a, b) a##b
#define XER_CAT(a, b) XER_CAT2(a, b)
#define KIT_TEXT(CLASS, NAME)                                             \
    NAN_METHOD(XER_CAT(CLASS, XER_CAT(Get, NAME))) {                      \
        auto* w = CastWidget<UIW::CLASS>(info.This());                    \
        info.GetReturnValue().Set(                                        \
            w ? Nan::New(w->XER_CAT(Get, NAME)()).ToLocalChecked()         \
              : Nan::New("").ToLocalChecked());                            \
    }                                                                     \
    NAN_METHOD(XER_CAT(CLASS, XER_CAT(Set, NAME))) {                      \
        auto* w = CastWidget<UIW::CLASS>(info.This());                    \
        std::string v;                                                    \
        if (w && ArgUtf8(info, 0, &v))                                    \
            w->XER_CAT(Set, NAME)(std::move(v));                          \
    }

KIT_TEXT(Kbd, Text)
KIT_TEXT(Marker, Text)

void NewKbd(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Kbd>(StrArg(info, 0)));
}

void NewMarker(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Marker>(StrArg(info, 0)));
}

KIT_TEXT(Field, Label)
KIT_TEXT(Field, Description)
KIT_TEXT(Field, Error)
KIT_TEXT(InputGroup, Prefix)
KIT_TEXT(InputGroup, Suffix)
KIT_TEXT(Item, Title)
KIT_TEXT(Item, Description)
KIT_TEXT(Empty, Title)
KIT_TEXT(Empty, Description)

#undef KIT_TEXT
#undef XER_CAT
#undef XER_CAT2

template <typename W, typename... Args>
void NewSimple(const Nan::FunctionCallbackInfo<v8::Value>& info,
               Args&&... args) {
    WidgetWrap::Adopt(info, std::make_shared<W>(std::forward<Args>(args)...));
}

// --- AspectRatio -------------------------------------------------------------

void NewAspectRatio(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::AspectRatio>(
        info, static_cast<float>(NumArg(info, 0, 16.0 / 9.0)));
}

NAN_METHOD(AspectRatioSetRatio) {
    auto* w = CastWidget<UIW::AspectRatio>(info.This());
    if (w) w->SetRatio(static_cast<float>(NumArg(info, 0, w->GetRatio())));
}

NAN_METHOD(AspectRatioGetRatio) {
    auto* w = CastWidget<UIW::AspectRatio>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetRatio() : 0.0));
}

// --- ButtonGroup -------------------------------------------------------------

using BtnOrientation = UIW::ButtonGroup::Orientation;

BtnOrientation OrientationArg(
    const Nan::FunctionCallbackInfo<v8::Value>& info, int index) {
    return EnumArg(info, index, BtnOrientation::Horizontal,
                   {{"Horizontal", BtnOrientation::Horizontal},
                    {"Vertical", BtnOrientation::Vertical}});
}

void NewButtonGroup(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::ButtonGroup>(info, OrientationArg(info, 0));
}

NAN_METHOD(ButtonGroupSetOrientation) {
    auto* w = CastWidget<UIW::ButtonGroup>(info.This());
    if (w) w->SetOrientation(OrientationArg(info, 0));
}

NAN_METHOD(ButtonGroupGetOrientation) {
    auto* w = CastWidget<UIW::ButtonGroup>(info.This());
    const char* name = "horizontal";
    if (w && w->GetOrientation() == BtnOrientation::Vertical)
        name = "vertical";
    info.GetReturnValue().Set(Nan::New(name).ToLocalChecked());
}

// --- Collapsible ------------------------------------------------------------

void NewCollapsible(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::Collapsible>(info, BoolArg(info, 0, false));
}

NAN_METHOD(CollapsibleSetOpen) {
    auto* w = CastWidget<UIW::Collapsible>(info.This());
    if (w) w->SetOpen(BoolArg(info, 0, w->IsOpen()));
}

NAN_METHOD(CollapsibleIsOpen) {
    auto* w = CastWidget<UIW::Collapsible>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsOpen()));
}

NAN_METHOD(CollapsibleGetProgress) {
    auto* w = CastWidget<UIW::Collapsible>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetProgress() : 0.0));
}

NAN_METHOD(CollapsibleSetOnToggled) {
    auto* w = CastWidget<UIW::Collapsible>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "toggled",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnToggled(
                               EmitFn<void, bool>(store, "toggled"));
                       });
}

// --- Item selected ------------------------------------------------------------

NAN_METHOD(ItemSetSelected) {
    auto* w = CastWidget<UIW::Item>(info.This());
    if (w) w->SetSelected(BoolArg(info, 0, w->IsSelected()));
}

NAN_METHOD(ItemIsSelected) {
    auto* w = CastWidget<UIW::Item>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsSelected()));
}

// --- Direction ----------------------------------------------------------------

using TextDir = XER::UI::Widgets::TextDirection;

TextDir DirArg(const Nan::FunctionCallbackInfo<v8::Value>& info, int index) {
    return EnumArg(info, index, TextDir::Ltr,
                   {{"Inherit", TextDir::Inherit},
                    {"Ltr", TextDir::Ltr},
                    {"Rtl", TextDir::Rtl}});
}

void NewDirection(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::Direction>(info, DirArg(info, 0));
}

NAN_METHOD(DirectionSetDirection) {
    auto* w = CastWidget<UIW::Direction>(info.This());
    if (w) w->SetDirection(DirArg(info, 0));
}

NAN_METHOD(DirectionGetDirection) {
    auto* w = CastWidget<UIW::Direction>(info.This());
    const char* name = "ltr";
    if (w) {
        switch (w->GetDirection()) {
            case TextDir::Inherit: name = "inherit"; break;
            case TextDir::Rtl: name = "rtl"; break;
            default: break;
        }
    }
    info.GetReturnValue().Set(Nan::New(name).ToLocalChecked());
}

// --- Form ---------------------------------------------------------------------

void NewForm(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::Form>(info, static_cast<int>(NumArg(info, 0, 1)),
                         static_cast<float>(NumArg(info, 1, -1.0)));
}

NAN_METHOD(FormSetColumns) {
    auto* w = CastWidget<UIW::Form>(info.This());
    if (w) w->SetColumns(static_cast<int>(NumArg(info, 0, w->GetColumns())));
}

NAN_METHOD(FormGetColumns) {
    auto* w = CastWidget<UIW::Form>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetColumns() : 1));
}

NAN_METHOD(FormSetGap) {
    auto* w = CastWidget<UIW::Form>(info.This());
    if (w) w->SetGap(static_cast<float>(NumArg(info, 0, w->GetGap())));
}

NAN_METHOD(FormGetGap) {
    auto* w = CastWidget<UIW::Form>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetGap() : -1.0));
}

// --- Attachment ---------------------------------------------------------------

void NewAttachment(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Attachment>(
                                 StrArg(info, 0), StrArg(info, 1)));
}

NAN_METHOD(AttachmentSetFilename) {
    auto* w = CastWidget<UIW::Attachment>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetFilename(std::move(v));
}

NAN_METHOD(AttachmentGetFilename) {
    auto* w = CastWidget<UIW::Attachment>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetFilename()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(AttachmentSetMeta) {
    auto* w = CastWidget<UIW::Attachment>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetMeta(std::move(v));
}

NAN_METHOD(AttachmentGetMeta) {
    auto* w = CastWidget<UIW::Attachment>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetMeta()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(AttachmentSetOnRemove) {
    auto* w = CastWidget<UIW::Attachment>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "remove",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnRemove(EmitFn<void>(store, "remove"));
                       });
}

// --- Bubble / Message -----------------------------------------------------------

using BubbleAlign = UIW::Bubble::Align;

BubbleAlign AlignArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                     int index) {
    return EnumArg(info, index, BubbleAlign::Other,
                   {{"Other", BubbleAlign::Other}, {"Own", BubbleAlign::Own}});
}

void NewBubble(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Bubble>(
                                 StrArg(info, 0), AlignArg(info, 1)));
}

NAN_METHOD(BubbleSetText) {
    auto* w = CastWidget<UIW::Bubble>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetText(std::move(v));
}

NAN_METHOD(BubbleGetText) {
    auto* w = CastWidget<UIW::Bubble>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetText()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(BubbleSetAlign) {
    auto* w = CastWidget<UIW::Bubble>(info.This());
    if (w) w->SetAlign(AlignArg(info, 0));
}

NAN_METHOD(BubbleGetAlign) {
    auto* w = CastWidget<UIW::Bubble>(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->GetAlign() == BubbleAlign::Own ? "own" : "other")
            .ToLocalChecked());
}

void NewMessage(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Message>(
                                 StrArg(info, 0), StrArg(info, 1),
                                 StrArg(info, 2), AlignArg(info, 3)));
}

NAN_METHOD(MessageSetSender) {
    auto* w = CastWidget<UIW::Message>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetSender(std::move(v));
}

NAN_METHOD(MessageGetSender) {
    auto* w = CastWidget<UIW::Message>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetSender()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(MessageSetText) {
    auto* w = CastWidget<UIW::Message>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetText(std::move(v));
}

NAN_METHOD(MessageGetText) {
    auto* w = CastWidget<UIW::Message>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetText()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(MessageSetTime) {
    auto* w = CastWidget<UIW::Message>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetTime(std::move(v));
}

NAN_METHOD(MessageGetTime) {
    auto* w = CastWidget<UIW::Message>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetTime()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(MessageSetAlign) {
    auto* w = CastWidget<UIW::Message>(info.This());
    if (w) w->SetAlign(AlignArg(info, 0));
}

NAN_METHOD(MessageGetAlign) {
    auto* w = CastWidget<UIW::Message>(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->GetAlign() == BubbleAlign::Own ? "own" : "other")
            .ToLocalChecked());
}

// --- MessageScroller ------------------------------------------------------------

void NewMessageScroller(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::MessageScroller>());
}

NAN_METHOD(MessageScrollerAddMessage) {
    auto* w = CastWidget<UIW::MessageScroller>(info.This());
    if (!w) return;
    auto child = WidgetFromArg(info, 0);
    if (child) w->AddMessage(std::move(child));
}

NAN_METHOD(MessageScrollerClear) {
    auto* w = CastWidget<UIW::MessageScroller>(info.This());
    if (w) w->Clear();
}

NAN_METHOD(MessageScrollerScrollToBottom) {
    auto* w = CastWidget<UIW::MessageScroller>(info.This());
    if (w) w->ScrollToBottom();
}

NAN_METHOD(MessageScrollerGetScrollY) {
    auto* w = CastWidget<UIW::MessageScroller>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetScrollY() : 0.0));
}

// --- InputOTP -------------------------------------------------------------------

void NewInputOTP(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::InputOTP>(
        info, static_cast<int>(NumArg(info, 0, 6)));
}

NAN_METHOD(InputOTPSetLength) {
    auto* w = CastWidget<UIW::InputOTP>(info.This());
    if (w) w->SetLength(static_cast<int>(NumArg(info, 0, w->GetLength())));
}

NAN_METHOD(InputOTPGetLength) {
    auto* w = CastWidget<UIW::InputOTP>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetLength() : 6));
}

NAN_METHOD(InputOTPSetCode) {
    auto* w = CastWidget<UIW::InputOTP>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetCode(std::move(v));
}

NAN_METHOD(InputOTPGetCode) {
    auto* w = CastWidget<UIW::InputOTP>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetCode()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(InputOTPSetOnComplete) {
    auto* w = CastWidget<UIW::InputOTP>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "complete",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnComplete(
                               EmitFn<void, std::string>(store, "complete"));
                       });
}

// --- HoverCard -------------------------------------------------------------------

void NewHoverCard(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::HoverCard>());
}

NAN_METHOD(HoverCardSetTrigger) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    if (!w) return;
    auto trigger = WidgetFromArg(info, 0);
    if (trigger) w->SetTrigger(std::move(trigger));
}

NAN_METHOD(HoverCardSetTitle) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetTitle(std::move(v));
}

NAN_METHOD(HoverCardGetTitle) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetTitle()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(HoverCardSetDescription) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetDescription(std::move(v));
}

NAN_METHOD(HoverCardGetDescription) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetDescription()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(HoverCardSetContent) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    if (!w) return;
    auto content = WidgetFromArg(info, 0);
    if (content) w->SetContent(std::move(content));
}

NAN_METHOD(HoverCardSetOpenDelay) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    if (w) w->SetOpenDelay(static_cast<float>(NumArg(info, 0, 0.5)));
}

NAN_METHOD(HoverCardSetActionLabel) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetActionLabel(std::move(v));
}

NAN_METHOD(HoverCardSetOnAction) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "action",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnAction(EmitFn<void>(store, "action"));
                       });
}

} // namespace

namespace xer::addon::ui {

void RegisterKit(v8::Local<v8::Object> xer) {
    RegisterWidgetClass(xer, "Kbd", NewKbd,
                        {{"SetText", KbdSetText}, {"GetText", KbdGetText}});
    RegisterWidgetClass(xer, "Marker", NewMarker,
                        {{"SetText", MarkerSetText},
                         {"GetText", MarkerGetText}});
    RegisterWidgetClass(
        xer, "AspectRatio", NewAspectRatio,
        {{"SetRatio", AspectRatioSetRatio},
         {"GetRatio", AspectRatioGetRatio}});
    RegisterWidgetClass(
        xer, "ButtonGroup", NewButtonGroup,
        {{"SetOrientation", ButtonGroupSetOrientation},
         {"GetOrientation", ButtonGroupGetOrientation}});
    RegisterWidgetClass(
        xer, "InputGroup", NewSimple<UIW::InputGroup>,
        {{"SetPrefix", InputGroupSetPrefix},
         {"GetPrefix", InputGroupGetPrefix},
         {"SetSuffix", InputGroupSetSuffix},
         {"GetSuffix", InputGroupGetSuffix}});
    RegisterWidgetClass(
        xer, "Field", NewSimple<UIW::Field>,
        {{"SetLabel", FieldSetLabel},
         {"GetLabel", FieldGetLabel},
         {"SetDescription", FieldSetDescription},
         {"GetDescription", FieldGetDescription},
         {"SetError", FieldSetError},
         {"GetError", FieldGetError}});
    RegisterWidgetClass(
        xer, "Item", NewSimple<UIW::Item>,
        {{"SetTitle", ItemSetTitle},
         {"GetTitle", ItemGetTitle},
         {"SetDescription", ItemSetDescription},
         {"GetDescription", ItemGetDescription},
         {"SetSelected", ItemSetSelected},
         {"IsSelected", ItemIsSelected}});
    RegisterWidgetClass(
        xer, "Empty", NewSimple<UIW::Empty>,
        {{"SetTitle", EmptySetTitle},
         {"GetTitle", EmptyGetTitle},
         {"SetDescription", EmptySetDescription},
         {"GetDescription", EmptyGetDescription}});
    RegisterWidgetClass(
        xer, "Collapsible", NewCollapsible,
        {{"SetOpen", CollapsibleSetOpen},
         {"IsOpen", CollapsibleIsOpen},
         {"GetProgress", CollapsibleGetProgress},
         {"SetOnToggled", CollapsibleSetOnToggled}});
    RegisterWidgetClass(
        xer, "Direction", NewDirection,
        {{"SetDirection", DirectionSetDirection},
         {"GetDirection", DirectionGetDirection}});
    RegisterWidgetClass(
        xer, "Form", NewForm,
        {{"SetColumns", FormSetColumns},
         {"GetColumns", FormGetColumns},
         {"SetGap", FormSetGap},
         {"GetGap", FormGetGap}});
    RegisterWidgetClass(
        xer, "Attachment", NewAttachment,
        {{"SetFilename", AttachmentSetFilename},
         {"GetFilename", AttachmentGetFilename},
         {"SetMeta", AttachmentSetMeta},
         {"GetMeta", AttachmentGetMeta},
         {"SetOnRemove", AttachmentSetOnRemove}});
    RegisterWidgetClass(
        xer, "Bubble", NewBubble,
        {{"SetText", BubbleSetText},
         {"GetText", BubbleGetText},
         {"SetAlign", BubbleSetAlign},
         {"GetAlign", BubbleGetAlign}});
    RegisterWidgetClass(
        xer, "Message", NewMessage,
        {{"SetSender", MessageSetSender},
         {"GetSender", MessageGetSender},
         {"SetText", MessageSetText},
         {"GetText", MessageGetText},
         {"SetTime", MessageSetTime},
         {"GetTime", MessageGetTime},
         {"SetAlign", MessageSetAlign},
         {"GetAlign", MessageGetAlign}});
    RegisterWidgetClass(
        xer, "MessageScroller", NewMessageScroller,
        {{"AddMessage", MessageScrollerAddMessage},
         {"Clear", MessageScrollerClear},
         {"ScrollToBottom", MessageScrollerScrollToBottom},
         {"GetScrollY", MessageScrollerGetScrollY}});
    RegisterWidgetClass(
        xer, "InputOTP", NewInputOTP,
        {{"SetLength", InputOTPSetLength},
         {"GetLength", InputOTPGetLength},
         {"SetCode", InputOTPSetCode},
         {"GetCode", InputOTPGetCode},
         {"SetOnComplete", InputOTPSetOnComplete}});
    RegisterWidgetClass(
        xer, "HoverCard", NewHoverCard,
        {{"SetTrigger", HoverCardSetTrigger},
         {"SetTitle", HoverCardSetTitle},
         {"GetTitle", HoverCardGetTitle},
         {"SetDescription", HoverCardSetDescription},
         {"GetDescription", HoverCardGetDescription},
         {"SetContent", HoverCardSetContent},
         {"SetOpenDelay", HoverCardSetOpenDelay},
         {"SetActionLabel", HoverCardSetActionLabel},
         {"SetOnAction", HoverCardSetOnAction}});
}

} // namespace xer::addon::ui
