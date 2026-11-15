// xer.UI.* — XML UI markup loader (XER::UI::Xml, libxml2-backed).
//
//   const res = UI.parseXml(`<Card><Label text="Hi"/></Card>`);
//   if (res.ok) win.SetContent(res.widget);
//   console.log(res.warnings, res.error);
//
//   const res2 = UI.parseXmlFile("ui/main.xml");
//   UI.xmlSupportedTags();  // comma-separated tag list for tooling

#include "widget.h"
#include "../util.h"

#include <string>
#include <vector>

#include "xml/xml_ui.h"

namespace {

namespace XERXml = XER::UI::Xml;
using xer::addon::ui::WidgetWrap;

v8::Local<v8::Object> ResultToJS(const XERXml::LoadResult& r) {
    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    Nan::Set(obj, Nan::New("ok").ToLocalChecked(),
             Nan::New(r.ok()));
    Nan::Set(obj, Nan::New("error").ToLocalChecked(),
             Nan::New(r.error).ToLocalChecked());

    v8::Local<v8::Array> warnings = Nan::New<v8::Array>(
        static_cast<uint32_t>(r.warnings.size()));
    for (std::size_t i = 0; i < r.warnings.size(); ++i) {
        Nan::Set(warnings, static_cast<uint32_t>(i),
                 Nan::New(r.warnings[i]).ToLocalChecked());
    }
    Nan::Set(obj, Nan::New("warnings").ToLocalChecked(), warnings);

    if (r.root) {
        Nan::Set(obj, Nan::New("widget").ToLocalChecked(),
                 WidgetWrap::ToJS(r.root));
    } else {
        Nan::Set(obj, Nan::New("widget").ToLocalChecked(),
                 Nan::Undefined());
    }
    return obj;
}

NAN_METHOD(ParseXml) {
    std::string markup;
    if (!xer::addon::ArgUtf8(info, 0, &markup)) {
        Nan::ThrowTypeError("parseXml(markup) expects a string");
        return;
    }
    info.GetReturnValue().Set(ResultToJS(XERXml::Load(markup)));
}

NAN_METHOD(ParseXmlFile) {
    std::string path;
    if (!xer::addon::ArgUtf8(info, 0, &path)) {
        Nan::ThrowTypeError("parseXmlFile(path) expects a string");
        return;
    }
    info.GetReturnValue().Set(ResultToJS(XERXml::LoadFile(path)));
}

NAN_METHOD(XmlSupportedTags) {
    info.GetReturnValue().Set(
        Nan::New(XERXml::SupportedTags()).ToLocalChecked());
}

}  // namespace

namespace xer::addon::ui {

void RegisterXml(v8::Local<v8::Object> xer) {
    Nan::Set(xer, Nan::New("parseXml").ToLocalChecked(),
             Nan::GetFunction(
                 Nan::New<v8::FunctionTemplate>(ParseXml))
                 .ToLocalChecked());
    Nan::Set(xer, Nan::New("parseXmlFile").ToLocalChecked(),
             Nan::GetFunction(
                 Nan::New<v8::FunctionTemplate>(ParseXmlFile))
                 .ToLocalChecked());
    Nan::Set(xer, Nan::New("xmlSupportedTags").ToLocalChecked(),
             Nan::GetFunction(
                 Nan::New<v8::FunctionTemplate>(XmlSupportedTags))
                 .ToLocalChecked());
}

}  // namespace xer::addon::ui
