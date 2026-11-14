// xer.image.* — image decode/info/save bindings (XER_Image).

#include "xer_bind.h"

#include <cstring>
#include <map>
#include <string>

#include <node_buffer.h>

#include "image/io/image_loader.h"
#include "image/io/image_saver.h"
#include "image/core/image_buffer.h"
#include "image/core/image_descriptor.h"
#include "image/core/pixel_format.h"
#include "image/core/color_space.h"
#include "helper/path_utils.h"

namespace {

namespace Path = XER::Helper::Path;

// Same normalization the preview tool applies: any common packed format ->
// tightly packed RGBA8. Returns false for formats it does not handle.
bool NormalizeToRGBA8(const image::ImageBuffer& src, image::ImageBuffer& dst) {
    const int w = src.Width();
    const int h = src.Height();
    if (w <= 0 || h <= 0) return false;

    dst.Allocate(w, h, image::PixelFormat::RGBA8, image::ColorSpace::sRGB);
    const std::size_t px = static_cast<std::size_t>(w) * h;
    const uint8_t* s = src.Data();
    uint8_t* d = dst.Data();
    if (s == nullptr || d == nullptr) return false;

    switch (src.Format()) {
        case image::PixelFormat::RGBA8:
            std::memcpy(d, s, px * 4);
            return true;
        case image::PixelFormat::BGRA8:
            for (std::size_t i = 0; i < px; ++i) {
                d[i * 4 + 0] = s[i * 4 + 2];
                d[i * 4 + 1] = s[i * 4 + 1];
                d[i * 4 + 2] = s[i * 4 + 0];
                d[i * 4 + 3] = s[i * 4 + 3];
            }
            return true;
        case image::PixelFormat::RGB8:
            for (std::size_t i = 0; i < px; ++i) {
                d[i * 4 + 0] = s[i * 3 + 0];
                d[i * 4 + 1] = s[i * 3 + 1];
                d[i * 4 + 2] = s[i * 3 + 2];
                d[i * 4 + 3] = 255;
            }
            return true;
        case image::PixelFormat::BGR8:
            for (std::size_t i = 0; i < px; ++i) {
                d[i * 4 + 0] = s[i * 3 + 2];
                d[i * 4 + 1] = s[i * 3 + 1];
                d[i * 4 + 2] = s[i * 3 + 0];
                d[i * 4 + 3] = 255;
            }
            return true;
        case image::PixelFormat::Gray8:
            for (std::size_t i = 0; i < px; ++i) {
                const uint8_t g = s[i];
                d[i * 4 + 0] = g;
                d[i * 4 + 1] = g;
                d[i * 4 + 2] = g;
                d[i * 4 + 3] = 255;
            }
            return true;
        default:
            return false;
    }
}

std::string GetStringProp(v8::Local<v8::Object> obj, const char* key) {
    v8::Local<v8::Value> v =
        Nan::Get(obj, Nan::New(key).ToLocalChecked()).ToLocalChecked();
    if (!v->IsString()) return {};
    Nan::Utf8String s(v);
    return std::string(*s, static_cast<std::size_t>(s.length()));
}

double GetNumberProp(v8::Local<v8::Object> obj, const char* key,
                     double fallback) {
    v8::Local<v8::Value> v =
        Nan::Get(obj, Nan::New(key).ToLocalChecked()).ToLocalChecked();
    return v->IsNumber() ? v.As<v8::Number>()->Value() : fallback;
}

bool GetBoolProp(v8::Local<v8::Object> obj, const char* key, bool fallback) {
    v8::Local<v8::Value> v =
        Nan::Get(obj, Nan::New(key).ToLocalChecked()).ToLocalChecked();
    return v->IsBoolean() ? v.As<v8::Boolean>()->Value() : fallback;
}

image::PixelFormat ParsePixelFormat(const std::string& name) {
    static const std::map<std::string, image::PixelFormat> kFormats = {
        {"rgba8", image::PixelFormat::RGBA8},
        {"bgra8", image::PixelFormat::BGRA8},
        {"rgb8", image::PixelFormat::RGB8},
        {"bgr8", image::PixelFormat::BGR8},
        {"gray8", image::PixelFormat::Gray8},
        {"gray16", image::PixelFormat::Gray16},
        {"rgba16", image::PixelFormat::RGBA16},
        {"rgba32f", image::PixelFormat::RGBA32F},
        {"yuv420p", image::PixelFormat::YUV420P},
    };
    const auto it = kFormats.find(name);
    return it != kFormats.end() ? it->second : image::PixelFormat::Unknown;
}

// Loads an image; on failure throws and returns nullptr.
std::shared_ptr<image::ImageBuffer> LoadOrThrow(
    const Nan::FunctionCallbackInfo<v8::Value>& info, int arg_index,
    image::ImageDescriptor* desc) {
    std::string path;
    if (!xer::addon::ArgUtf8(info, arg_index, &path)) {
        xer::addon::ThrowUsage("image.* requires a path string");
        return nullptr;
    }
    const std::string upath = Path::NormalizeUtf8Path(path);
    image::ImageLoader loader;
    auto buf = loader.Load(upath, *desc);
    if (!buf || !buf->IsValid()) {
        Nan::ThrowError(("Failed to load image: " + path).c_str());
        return nullptr;
    }
    return buf;
}

NAN_METHOD(ImageInfo) {
    image::ImageDescriptor desc;
    auto buf = LoadOrThrow(info, 0, &desc);
    if (!buf) return;

    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    Nan::Set(obj, Nan::New("width").ToLocalChecked(),
             Nan::New(static_cast<double>(buf->Width())));
    Nan::Set(obj, Nan::New("height").ToLocalChecked(),
             Nan::New(static_cast<double>(buf->Height())));
    Nan::Set(obj, Nan::New("format").ToLocalChecked(),
             Nan::New(image::PixelFormatName(buf->Format())).ToLocalChecked());
    Nan::Set(obj, Nan::New("pixelFormat").ToLocalChecked(),
             Nan::New(image::PixelFormatName(desc.format)).ToLocalChecked());
    Nan::Set(obj, Nan::New("formatName").ToLocalChecked(),
             Nan::New(desc.format_name).ToLocalChecked());
    Nan::Set(obj, Nan::New("colorSpace").ToLocalChecked(),
             Nan::New(image::ColorSpaceName(buf->GetColorSpace())).ToLocalChecked());
    Nan::Set(obj, Nan::New("channels").ToLocalChecked(),
             Nan::New(static_cast<double>(desc.channels)));
    Nan::Set(obj, Nan::New("depth").ToLocalChecked(),
             Nan::New(static_cast<double>(desc.depth)));
    Nan::Set(obj, Nan::New("hasAlpha").ToLocalChecked(),
             Nan::New(desc.has_alpha));
    Nan::Set(obj, Nan::New("isHdr").ToLocalChecked(), Nan::New(desc.is_hdr));
    Nan::Set(obj, Nan::New("pageCount").ToLocalChecked(),
             Nan::New(static_cast<double>(desc.page_count)));
    Nan::Set(obj, Nan::New("dpiX").ToLocalChecked(), Nan::New(desc.dpi_x));
    Nan::Set(obj, Nan::New("dpiY").ToLocalChecked(), Nan::New(desc.dpi_y));
    info.GetReturnValue().Set(obj);
}

NAN_METHOD(ImageDecode) {
    image::ImageDescriptor desc;
    auto buf = LoadOrThrow(info, 0, &desc);
    if (!buf) return;

    const bool want_rgba =
        info.Length() > 1 && info[1]->IsObject() &&
        GetBoolProp(info[1].As<v8::Object>(), "rgba", false);

    image::ImageBuffer out;
    const image::ImageBuffer* result = buf.get();
    if (want_rgba) {
        if (!NormalizeToRGBA8(*buf, out)) {
            Nan::ThrowError("Cannot normalize pixel format to RGBA8");
            return;
        }
        result = &out;
    }

    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    Nan::Set(obj, Nan::New("width").ToLocalChecked(),
             Nan::New(static_cast<double>(result->Width())));
    Nan::Set(obj, Nan::New("height").ToLocalChecked(),
             Nan::New(static_cast<double>(result->Height())));
    Nan::Set(obj, Nan::New("format").ToLocalChecked(),
             Nan::New(image::PixelFormatName(result->Format())).ToLocalChecked());
    Nan::Set(obj, Nan::New("formatName").ToLocalChecked(),
             Nan::New(desc.format_name).ToLocalChecked());
    Nan::Set(obj, Nan::New("bytesPerPixel").ToLocalChecked(),
             Nan::New(static_cast<double>(image::BytesPerPixel(result->Format()))));
    Nan::Set(obj, Nan::New("data").ToLocalChecked(),
             Nan::CopyBuffer(reinterpret_cast<const char*>(result->Data()),
                             static_cast<uint32_t>(result->DataSize()))
                 .ToLocalChecked());
    info.GetReturnValue().Set(obj);
}

NAN_METHOD(ImageSave) {
    // image.save({data,width,height,format?}, path, {format?,quality?})
    if (info.Length() < 2 || !info[0]->IsObject() || !info[1]->IsString()) {
        xer::addon::ThrowUsage(
            "image.save({data,width,height,format?}, path, opts?)");
        return;
    }
    v8::Local<v8::Object> px = info[0].As<v8::Object>();

    std::string path;
    if (!xer::addon::ArgUtf8(info, 1, &path)) {
        xer::addon::ThrowUsage("image.save path must be a string");
        return;
    }
    const std::string upath = Path::NormalizeUtf8Path(path);

    v8::Local<v8::Value> data_v =
        Nan::Get(px, Nan::New("data").ToLocalChecked()).ToLocalChecked();
    // Buffer is a Uint8Array subclass, so one path covers both.
    if (!data_v->IsUint8Array()) {
        Nan::ThrowError("image.save: 'data' must be a Buffer/Uint8Array");
        return;
    }
    const int width = static_cast<int>(GetNumberProp(px, "width", 0));
    const int height = static_cast<int>(GetNumberProp(px, "height", 0));
    if (width <= 0 || height <= 0) {
        Nan::ThrowError("image.save: invalid width/height");
        return;
    }

    image::PixelFormat fmt = image::PixelFormat::RGBA8;
    const std::string fmt_name = GetStringProp(px, "format");
    if (!fmt_name.empty()) {
        fmt = ParsePixelFormat(fmt_name);
        if (fmt == image::PixelFormat::Unknown) {
            Nan::ThrowError(("image.save: unknown pixel format '" + fmt_name +
                             "'").c_str());
            return;
        }
    }

    image::ImageBuffer img;
    img.Allocate(width, height, fmt, image::ColorSpace::sRGB);
    if (!img.IsValid()) {
        Nan::ThrowError("image.save: failed to allocate buffer");
        return;
    }

    v8::Local<v8::Uint8Array> u8 =
        v8::Local<v8::Uint8Array>::Cast(data_v);
    const size_t src_len = u8->ByteLength();
    // ->Data() collides with the v8::Data base-class name; go through the
    // ArrayBuffer backing store instead.
    std::shared_ptr<v8::BackingStore> bs = u8->Buffer()->GetBackingStore();
    const uint8_t* src = bs == nullptr
        ? nullptr
        : reinterpret_cast<const uint8_t*>(bs->Data()) + u8->ByteOffset();
    const size_t copy_len = src_len < img.DataSize() ? src_len : img.DataSize();
    if (copy_len > 0 && src != nullptr)
        std::memcpy(img.Data(), src, copy_len);

    // opts: {format?: 'png'|'jpg'|'jpeg'|'raw', quality?: number}
    int quality = 92;
    std::string out_format;
    if (info.Length() > 2 && info[2]->IsObject()) {
        v8::Local<v8::Object> opts = info[2].As<v8::Object>();
        out_format = GetStringProp(opts, "format");
        quality = static_cast<int>(GetNumberProp(opts, "quality", 92));
    }

    image::ImageSaver saver;
    bool ok = false;
    if (out_format == "png") {
        ok = saver.SavePng(img, upath);
    } else if (out_format == "jpg" || out_format == "jpeg") {
        ok = saver.SaveJpeg(img, upath, quality);
    } else if (out_format == "raw") {
        ok = saver.SaveRaw(img, upath);
    } else if (!out_format.empty()) {
        Nan::ThrowError(("image.save: unknown output format '" + out_format +
                         "'").c_str());
        return;
    } else {
        ok = saver.Save(img, upath, quality);
    }
    if (!ok) {
        Nan::ThrowError(("image.save failed: " + path).c_str());
        return;
    }
    info.GetReturnValue().Set(Nan::New(true));
}

}  // namespace

namespace xer::addon {

void InitImage(v8::Local<v8::Object> exports) {
    Nan::SetMethod(exports, "info", ImageInfo);
    Nan::SetMethod(exports, "decode", ImageDecode);
    Nan::SetMethod(exports, "save", ImageSave);
}

}  // namespace xer::addon
