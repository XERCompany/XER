// xer.audio.* — audio probe/convert bindings (XER_AudioCore).
// Mirrors tools/audioprobe + tools/audioconvert.

#include "xer_bind.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "audio/audio_core/audio_source_loader.h"
#include "audio/file_io_codecs/codec_ffmpeg_encode_helper.h"
#include "audio/file_io_codecs/codec_mp3_lame.h"
#include "audio/file_io_codecs/codec_wav_pcm.h"
#include "helper/path_utils.h"
#include "helper/string_utils.h"

namespace {

namespace Path = XER::Helper::Path;
namespace AudioCore = XER::Audio::Core;
namespace CodecIO = XER::Audio::CodecIO;
namespace Str = XER::Helper::String;

constexpr const char* kSupportedOut =
    "wav, mp3, flac, ogg, opus, m4a, aac";

// --- shared argument helpers -------------------------------------------------

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

// --- DSP helpers (same math as audioprobe) -----------------------------------

double CalculatePeak(const std::vector<float>& samples) {
    double peak = 0.0;
    for (float s : samples) {
        const double a = std::abs(static_cast<double>(s));
        if (a > peak) peak = a;
    }
    return peak;
}

double CalculateRMS(const std::vector<float>& samples) {
    if (samples.empty()) return 0.0;
    double sum = 0.0;
    for (float s : samples) {
        const double v = static_cast<double>(s);
        sum += v * v;
    }
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

double ToDb(double linear) { return 20.0 * std::log10(linear + 1e-10); }

// --- encoder (same dispatch as audioconvert) ---------------------------------

bool EncodeForPath(const std::vector<float>& samples, int sample_rate,
                   int channels, const std::string& path,
                   std::vector<uint8_t>* bytes, std::string* error) {
    const size_t dot = path.find_last_of('.');
    const std::string ext = dot == std::string::npos
        ? std::string()
        : Str::ToLowerAscii(path.substr(dot));
    const size_t frames = samples.size() / static_cast<size_t>(channels);
    const float* data = samples.data();

    if (ext == ".wav") {
        if (CodecIO::WavPcmCodec().Encode16(data, frames, *bytes, sample_rate,
                                            channels)) {
            return true;
        }
        *error = "wav encode failed";
        return false;
    }
    if (ext == ".mp3") {
        if (CodecIO::Mp3LameCodec().EncodeInterleaved(data, frames, sample_rate,
                                                      channels, *bytes)) {
            return true;
        }
        *error = "mp3 encode failed";
        return false;
    }

    static const std::map<std::string, std::string> kFfmpegCodecs = {
        {".flac", "flac"}, {".ogg", "libvorbis"}, {".opus", "libopus"},
        {".m4a", "aac"},   {".aac", "aac"}
    };
    const auto codec = kFfmpegCodecs.find(ext);
    if (codec == kFfmpegCodecs.end()) {
        *error = "unsupported output format '" + ext + "' (" + kSupportedOut +
                 ")";
        return false;
    }
    return CodecIO::detail::EncodeInterleavedAudioBufferWithFfmpeg(
        data, frames, sample_rate, channels, ext, codec->second, bytes, error);
}

// Decodes `path` into `buffer`; on failure throws and returns false.
bool LoadOrThrow(const Nan::FunctionCallbackInfo<v8::Value>& info,
                 const std::string& raw_path, int rate, int channels,
                 AudioCore::AudioSourceBuffer* buffer) {
    const std::string upath = Path::NormalizeUtf8Path(raw_path);
    AudioCore::AudioSourceLoadOptions options;
    options.input_path = Path::FromUtf8(upath);
    options.target_sample_rate = rate > 0 ? rate : 44100;
    options.target_channels = channels > 0 ? channels : 0;
    options.resample_quality = AudioCore::ResampleQuality::HIGH;

    std::string error;
    if (!AudioCore::AudioSourceLoader::Load(options, buffer, &error)) {
        Nan::ThrowError(("Failed to load audio '" + raw_path + "': " + error)
                            .c_str());
        return false;
    }
    return true;
}

// --- bindings ----------------------------------------------------------------

NAN_METHOD(AudioInfo) {
    std::string path;
    if (!xer::addon::ArgUtf8(info, 0, &path)) {
        xer::addon::ThrowUsage("audio.info(path)");
        return;
    }

    AudioCore::AudioSourceBuffer buffer;
    if (!LoadOrThrow(info, path, 0, 0, &buffer)) {
        return;
    }

    const double duration = buffer.sample_rate > 0
        ? static_cast<double>(buffer.frame_count) / buffer.sample_rate
        : 0.0;
    const double peak = CalculatePeak(buffer.samples);
    const double rms = CalculateRMS(buffer.samples);

    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    Nan::Set(obj, Nan::New("format").ToLocalChecked(),
             Nan::New(buffer.source_format).ToLocalChecked());
    Nan::Set(obj, Nan::New("codec").ToLocalChecked(),
             Nan::New(buffer.codec_name).ToLocalChecked());
    Nan::Set(obj, Nan::New("backend").ToLocalChecked(),
             Nan::New(buffer.decode_backend).ToLocalChecked());
    Nan::Set(obj, Nan::New("sampleRate").ToLocalChecked(),
             Nan::New(static_cast<double>(buffer.sample_rate)));
    Nan::Set(obj, Nan::New("originalSampleRate").ToLocalChecked(),
             Nan::New(static_cast<double>(buffer.original_sample_rate)));
    Nan::Set(obj, Nan::New("channels").ToLocalChecked(),
             Nan::New(static_cast<double>(buffer.channels)));
    Nan::Set(obj, Nan::New("originalChannels").ToLocalChecked(),
             Nan::New(static_cast<double>(buffer.original_channels)));
    Nan::Set(obj, Nan::New("frames").ToLocalChecked(),
             Nan::New(static_cast<double>(buffer.frame_count)));
    Nan::Set(obj, Nan::New("originalFrames").ToLocalChecked(),
             Nan::New(static_cast<double>(buffer.original_frame_count)));
    Nan::Set(obj, Nan::New("duration").ToLocalChecked(), Nan::New(duration));
    Nan::Set(obj, Nan::New("peak").ToLocalChecked(), Nan::New(peak));
    Nan::Set(obj, Nan::New("peakDb").ToLocalChecked(), Nan::New(ToDb(peak)));
    Nan::Set(obj, Nan::New("rms").ToLocalChecked(), Nan::New(rms));
    Nan::Set(obj, Nan::New("rmsDb").ToLocalChecked(), Nan::New(ToDb(rms)));
    info.GetReturnValue().Set(obj);
}

NAN_METHOD(AudioConvert) {
    // audio.convert(inputPath, outputPath, {sampleRate?, channels?})
    std::string in_path, out_path;
    if (!xer::addon::ArgUtf8(info, 0, &in_path) ||
        !xer::addon::ArgUtf8(info, 1, &out_path)) {
        xer::addon::ThrowUsage("audio.convert(input, output, opts?)");
        return;
    }

    int target_rate = 0;
    int target_channels = 0;
    if (info.Length() > 2 && info[2]->IsObject()) {
        v8::Local<v8::Object> opts = info[2].As<v8::Object>();
        target_rate = static_cast<int>(GetNumberProp(opts, "sampleRate", 0));
        target_channels = static_cast<int>(GetNumberProp(opts, "channels", 0));
    }

    AudioCore::AudioSourceBuffer buffer;
    if (!LoadOrThrow(info, in_path, target_rate, target_channels, &buffer)) {
        return;
    }

    // Keep the original sample rate when the caller did not request one
    // (same reload trick as audioconvert).
    if (target_rate == 0 && buffer.original_sample_rate > 0 &&
        buffer.original_sample_rate != buffer.sample_rate) {
        AudioCore::AudioSourceLoadOptions options;
        options.input_path =
            Path::FromUtf8(Path::NormalizeUtf8Path(in_path));
        options.target_sample_rate = buffer.original_sample_rate;
        options.target_channels = target_channels > 0 ? target_channels : 0;
        options.resample_quality = AudioCore::ResampleQuality::HIGH;
        std::string error;
        buffer = {};
        if (!AudioCore::AudioSourceLoader::Load(options, &buffer, &error)) {
            Nan::ThrowError(
                ("Failed to reload audio at original rate: " + error).c_str());
            return;
        }
    }

    std::vector<uint8_t> encoded;
    std::string error;
    if (!EncodeForPath(buffer.samples, buffer.sample_rate, buffer.channels,
                       out_path, &encoded, &error)) {
        Nan::ThrowError(("audio.convert encode failed: " + error).c_str());
        return;
    }

    const std::string upath_out = Path::NormalizeUtf8Path(out_path);
    std::ofstream out(Path::FromUtf8(upath_out), std::ios::binary);
    if (!out) {
        Nan::ThrowError(("audio.convert: cannot open output '" + out_path +
                         "'")
                            .c_str());
        return;
    }
    out.write(reinterpret_cast<const char*>(encoded.data()),
              static_cast<std::streamsize>(encoded.size()));
    out.close();
    if (!out.good()) {
        Nan::ThrowError(("audio.convert: write failed for '" + out_path + "'")
                            .c_str());
        return;
    }

    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    Nan::Set(obj, Nan::New("sampleRate").ToLocalChecked(),
             Nan::New(static_cast<double>(buffer.sample_rate)));
    Nan::Set(obj, Nan::New("channels").ToLocalChecked(),
             Nan::New(static_cast<double>(buffer.channels)));
    Nan::Set(obj, Nan::New("frames").ToLocalChecked(),
             Nan::New(static_cast<double>(buffer.frame_count)));
    Nan::Set(obj, Nan::New("duration").ToLocalChecked(),
             Nan::New(buffer.sample_rate > 0
                          ? static_cast<double>(buffer.frame_count) /
                                buffer.sample_rate
                          : 0.0));
    Nan::Set(obj, Nan::New("bytes").ToLocalChecked(),
             Nan::New(static_cast<double>(encoded.size())));
    info.GetReturnValue().Set(obj);
}

}  // namespace

namespace xer::addon {

void InitAudio(v8::Local<v8::Object> exports) {
    Nan::SetMethod(exports, "info", AudioInfo);
    Nan::SetMethod(exports, "convert", AudioConvert);
}

}  // namespace xer::addon
