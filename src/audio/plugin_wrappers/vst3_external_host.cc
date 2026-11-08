#include "vst3_external_host.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/gui/iplugview.h"

namespace XER::Audio::Plugin {
namespace {

using namespace Steinberg;
using namespace Steinberg::Vst;

// Resolves a user-supplied .vst3 reference to the loadable module binary.
// A .vst3 "bundle" is a directory: <Name>.vst3/Contents/x86_64-win/<Name>.vst3.
// A single-file module is a PE binary with a .vst3 extension.
std::wstring ResolveVst3ModulePath(const std::string& plugin_path,
                                   std::string* error_out) {
    const std::wstring wide = XER::Helper::String::Utf8ToWide(plugin_path);
    if (wide.empty()) {
        if (error_out) *error_out = "invalid plugin path encoding";
        return {};
    }

    const DWORD attrs = GetFileAttributesW(wide.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (error_out) *error_out = "plugin file not found: " + plugin_path;
        return {};
    }

    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return wide; // single-file module
    }

    // Bundle directory: prefer Contents/x86_64-win, then scan for *.vst3.
    std::wstring arch_dir = wide;
    if (!XER::Helper::String::EndsWith(arch_dir, L"\\x86_64-win") && !XER::Helper::String::EndsWith(arch_dir, L"/x86_64-win")) {
        arch_dir += L"\\Contents\\x86_64-win";
    }
    const DWORD arch_attrs = GetFileAttributesW(arch_dir.c_str());
    if (arch_attrs != INVALID_FILE_ATTRIBUTES &&
        (arch_attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        WIN32_FIND_DATAW fd {};
        HANDLE find = FindFirstFileW((arch_dir + L"\\*.vst3").c_str(), &fd);
        if (find != INVALID_HANDLE_VALUE) {
            const std::wstring found = arch_dir + L"\\" + fd.cFileName;
            FindClose(find);
            return found;
        }
    }

    // Fallback: any *.vst3 directly inside the bundle root.
    WIN32_FIND_DATAW fd {};
    HANDLE find = FindFirstFileW((wide + L"\\*.vst3").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        const std::wstring found = wide + L"\\" + fd.cFileName;
        FindClose(find);
        return found;
    }

    if (error_out) *error_out = "no .vst3 module binary inside bundle: " + plugin_path;
    return {};
}

// String128 is a fixed UTF-16 buffer; decode the BMP subset to UTF-8.
std::string ParamTitle(const String128& title) {
    std::string out;
    for (const char16* p = title; *p != 0 && out.size() < 512; ++p) {
        const uint32_t cp = static_cast<uint32_t>(*p);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

}  // namespace

struct Vst3ExternalHost::Impl {
    HMODULE dll_handle = nullptr;
    IPtr<IPluginFactory> factory;
    HostApplication host_app;
    IPtr<IComponent> component;
    IPtr<IAudioProcessor> processor;
    IPtr<IEditController> editor;
    IPtr<IPlugView> plug_view;

    double sample_rate = 44100.0;
    uint32_t max_block = 512;
    int32 num_input_buses = 0;
    int32 num_output_buses = 0;

    std::vector<AudioBusBuffers> input_buses;
    std::vector<std::vector<Sample32*>> input_channel_ptrs;
    std::vector<AudioBusBuffers> output_buses;
    std::vector<std::vector<Sample32*>> output_channel_ptrs;
    std::vector<std::vector<std::vector<float>>> output_scratch;

    std::string plugin_name;
};

Vst3ExternalHost::Vst3ExternalHost() : impl_(std::make_unique<Impl>()) {}

Vst3ExternalHost::~Vst3ExternalHost() {
    Unload();
}

bool Vst3ExternalHost::Load(const std::string& plugin_path, double sample_rate,
                            uint32_t max_block_size, std::string* error_out) {
    Unload();
    if (plugin_path.empty() || sample_rate <= 0.0 || max_block_size == 0) {
        if (error_out) *error_out = "invalid VST3 load arguments";
        return false;
    }

    std::string resolve_error;
    const std::wstring dll_path = ResolveVst3ModulePath(plugin_path, &resolve_error);
    if (dll_path.empty()) {
        if (error_out) *error_out = resolve_error;
        return false;
    }

    HMODULE dll = LoadLibraryW(dll_path.c_str());
    if (dll == nullptr) {
        if (error_out) *error_out = "LoadLibrary failed for: " + plugin_path;
        return false;
    }

    using GetPluginFactoryProc = IPluginFactory* (*)();
    const auto get_factory = reinterpret_cast<GetPluginFactoryProc>(
        GetProcAddress(dll, "GetPluginFactory"));
    if (get_factory == nullptr) {
        FreeLibrary(dll);
        if (error_out) *error_out = "module has no GetPluginFactory export";
        return false;
    }

    FUnknown* factory_unknown = get_factory();
    IPtr<IPluginFactory> factory;
    if (factory_unknown != nullptr) {
        IPluginFactory* raw_factory = nullptr;
        factory_unknown->queryInterface(IPluginFactory::iid,
                                        reinterpret_cast<void**>(&raw_factory));
        factory_unknown->release();
        factory = IPtr<IPluginFactory>::adopt(raw_factory);
    }
    if (!factory) {
        FreeLibrary(dll);
        if (error_out) *error_out = "GetPluginFactory returned no IPluginFactory";
        return false;
    }

    // Hand the module handle to Impl early: every failure path below then
    // leaves it for Unload()/the destructor to release.
    impl_->dll_handle = dll;
    impl_->factory = factory; // copy: local `factory` is still needed below

    // Prefer an audio module class; fall back to the first available class.
    PClassInfo class_info {};
    int32 selected_index = -1;
    const int32 class_count = factory->countClasses();
    for (int32 i = 0; i < class_count; ++i) {
        if (factory->getClassInfo(i, &class_info) != kResultOk) {
            continue;
        }
        if (std::strcmp(class_info.category, kVstAudioEffectClass) == 0 ||
            std::strncmp(class_info.category, PlugType::kInstrument,
                         std::strlen(PlugType::kInstrument)) == 0) {
            selected_index = i;
            break;
        }
    }
    if (selected_index < 0 && class_count > 0 &&
        factory->getClassInfo(0, &class_info) == kResultOk) {
        selected_index = 0;
    }
    if (selected_index < 0) {
        if (error_out) *error_out = "module has no classes";
        return false;
    }

    FUnknown* object = nullptr;
    if (factory->createInstance(class_info.cid, IComponent::iid,
                                 reinterpret_cast<void**>(&object)) != kResultOk ||
        object == nullptr) {
        if (error_out) *error_out = "failed to create component instance";
        return false;
    }
    IComponent* raw_component = nullptr;
    object->queryInterface(IComponent::iid, reinterpret_cast<void**>(&raw_component));
    object->release();
    IPtr<IComponent> component = IPtr<IComponent>::adopt(raw_component);
    if (!component) {
        if (error_out) *error_out = "created object is not an IComponent";
        return false;
    }

    IAudioProcessor* raw_processor = nullptr;
    component->queryInterface(IAudioProcessor::iid, reinterpret_cast<void**>(&raw_processor));
    IPtr<IAudioProcessor> processor = IPtr<IAudioProcessor>::adopt(raw_processor);
    if (!processor) {
        if (error_out) *error_out = "component is not an audio processor";
        return false;
    }
    if (!processor->canProcessSampleSize(kSample32)) {
        if (error_out) *error_out = "plugin does not support 32-bit processing";
        return false;
    }

    IEditController* raw_editor = nullptr;
    component->queryInterface(IEditController::iid, reinterpret_cast<void**>(&raw_editor));
    IPtr<IEditController> editor = IPtr<IEditController>::adopt(raw_editor);

    FUnknown* context = static_cast<FUnknown*>(&impl_->host_app);
    if (component->initialize(context) != kResultOk) {
        if (error_out) *error_out = "component initialize failed";
        return false;
    }
    if (editor && editor->initialize(context) != kResultOk) {
        editor = nullptr;
    }

    impl_->sample_rate = sample_rate;
    impl_->max_block = max_block_size;

    ProcessSetup setup {};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = static_cast<int32>(max_block_size);
    setup.sampleRate = sample_rate;
    if (processor->setupProcessing(setup) != kResultOk) {
        component->terminate();
        if (error_out) *error_out = "setupProcessing failed";
        return false;
    }

    // Arrange buses: stereo where the bus supports it, mono otherwise.
    // Bus enumeration lives on IComponent, not IAudioProcessor.
    impl_->num_input_buses = component->getBusCount(kAudio, kInput);
    impl_->num_output_buses = component->getBusCount(kAudio, kOutput);
    std::vector<SpeakerArrangement> in_arr(static_cast<size_t>(impl_->num_input_buses),
                                          SpeakerArr::kStereo);
    std::vector<SpeakerArrangement> out_arr(static_cast<size_t>(impl_->num_output_buses),
                                           SpeakerArr::kStereo);
    BusInfo bus_info {};
    for (int32 i = 0; i < impl_->num_input_buses; ++i) {
        if (component->getBusInfo(kAudio, kInput, i, bus_info) == kResultOk &&
            bus_info.channelCount < 2) {
            in_arr[static_cast<size_t>(i)] = SpeakerArr::kMono;
        }
    }
    for (int32 i = 0; i < impl_->num_output_buses; ++i) {
        if (component->getBusInfo(kAudio, kOutput, i, bus_info) == kResultOk &&
            bus_info.channelCount < 2) {
            out_arr[static_cast<size_t>(i)] = SpeakerArr::kMono;
        }
    }
    processor->setBusArrangements(
        in_arr.empty() ? nullptr : in_arr.data(), impl_->num_input_buses,
        out_arr.empty() ? nullptr : out_arr.data(), impl_->num_output_buses);

    // Preallocate ProcessData plumbing.
    impl_->input_buses.resize(static_cast<size_t>(impl_->num_input_buses));
    impl_->input_channel_ptrs.resize(static_cast<size_t>(impl_->num_input_buses));
    impl_->output_buses.resize(static_cast<size_t>(impl_->num_output_buses));
    impl_->output_channel_ptrs.resize(static_cast<size_t>(impl_->num_output_buses));
    impl_->output_scratch.resize(static_cast<size_t>(impl_->num_output_buses));

    for (int32 b = 0; b < impl_->num_input_buses; ++b) {
        int32 channels = 2;
        if (component->getBusInfo(kAudio, kInput, b, bus_info) == kResultOk) {
            channels = bus_info.channelCount;
        }
        auto& buffers = impl_->input_buses[static_cast<size_t>(b)];
        buffers.numChannels = channels;
        buffers.silenceFlags = 0;
        auto& ptrs = impl_->input_channel_ptrs[static_cast<size_t>(b)];
        ptrs.assign(static_cast<size_t>(channels), nullptr);
        buffers.channelBuffers32 = ptrs.data();
    }
    for (int32 b = 0; b < impl_->num_output_buses; ++b) {
        int32 channels = 2;
        if (component->getBusInfo(kAudio, kOutput, b, bus_info) == kResultOk) {
            channels = bus_info.channelCount;
        }
        channels = std::max<int32>(1, channels);
        auto& buffers = impl_->output_buses[static_cast<size_t>(b)];
        buffers.numChannels = channels;
        buffers.silenceFlags = 0;
        auto& ptrs = impl_->output_channel_ptrs[static_cast<size_t>(b)];
        ptrs.assign(static_cast<size_t>(channels), nullptr);
        buffers.channelBuffers32 = ptrs.data();
        auto& scratch = impl_->output_scratch[static_cast<size_t>(b)];
        scratch.resize(static_cast<size_t>(channels));
        for (int32 ch = 0; ch < channels; ++ch) {
            scratch[static_cast<size_t>(ch)].assign(max_block_size, 0.0f);
            ptrs[static_cast<size_t>(ch)] = scratch[static_cast<size_t>(ch)].data();
        }
    }

    if (processor->setProcessing(true) != kResultOk) {
        component->terminate();
        if (error_out) *error_out = "setProcessing failed";
        return false;
    }

    impl_->component = std::move(component);
    impl_->processor = std::move(processor);
    impl_->editor = std::move(editor);
    impl_->plugin_name = class_info.name;
    return true;
}

void Vst3ExternalHost::Unload() {
    RemoveEditorView();
    if (impl_->processor) {
        impl_->processor->setProcessing(false);
    }
    if (impl_->component) {
        impl_->component->terminate();
    }
    impl_->editor = nullptr;
    impl_->processor = nullptr;
    impl_->component = nullptr;
    impl_->factory = nullptr;
    if (impl_->dll_handle != nullptr) {
        FreeLibrary(impl_->dll_handle);
        impl_->dll_handle = nullptr;
    }
    impl_->num_input_buses = 0;
    impl_->num_output_buses = 0;
}

bool Vst3ExternalHost::IsLoaded() const {
    return impl_->processor != nullptr;
}

bool Vst3ExternalHost::Process(const float* input, float* output, uint32_t frames) {
    if (!IsLoaded() || input == nullptr || output == nullptr || frames == 0 ||
        frames > impl_->max_block) {
        return false;
    }

    // Feed the mono signal into every channel of every input bus — plugins
    // with stereo/sidechain inputs get a dual-mono source.
    for (int32 b = 0; b < impl_->num_input_buses; ++b) {
        auto& ptrs = impl_->input_channel_ptrs[static_cast<size_t>(b)];
        for (auto& ptr : ptrs) {
            ptr = const_cast<Sample32*>(input);
        }
    }

    ProcessData data {};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = static_cast<int32>(frames);
    data.numInputs = impl_->num_input_buses;
    data.numOutputs = impl_->num_output_buses;
    data.inputs = impl_->input_buses.empty() ? nullptr : impl_->input_buses.data();
    data.outputs = impl_->output_buses.empty() ? nullptr : impl_->output_buses.data();

    if (impl_->processor->process(data) != kResultOk) {
        return false;
    }

    // Mono output = channel 0 of the first output bus.
    if (impl_->num_output_buses > 0 && !impl_->output_scratch.empty() &&
        !impl_->output_scratch[0].empty()) {
        const auto& ch0 = impl_->output_scratch[0][0];
        const size_t copy_count = std::min<size_t>(frames, ch0.size());
        std::memcpy(output, ch0.data(), copy_count * sizeof(float));
        for (size_t i = copy_count; i < frames; ++i) {
            output[i] = 0.0f;
        }
    } else {
        std::memset(output, 0, frames * sizeof(float));
    }
    return true;
}

bool Vst3ExternalHost::SetParameter(uint32_t id, float value) {
    if (impl_->editor == nullptr) {
        return false;
    }
    value = std::clamp(value, 0.0f, 1.0f);
    return impl_->editor->setParamNormalized(static_cast<ParamID>(id),
                                             static_cast<ParamValue>(value)) == kResultOk;
}

bool Vst3ExternalHost::GetParameter(uint32_t id, float* value) const {
    if (impl_->editor == nullptr || value == nullptr) {
        return false;
    }
    *value = static_cast<float>(impl_->editor->getParamNormalized(static_cast<ParamID>(id)));
    return true;
}

std::vector<PluginParameterInfo> Vst3ExternalHost::GetParameters() const {
    std::vector<PluginParameterInfo> result;
    if (impl_->editor == nullptr) {
        return result;
    }
    const int32 count = impl_->editor->getParameterCount();
    result.reserve(static_cast<size_t>(count));
    ParameterInfo info {};
    for (int32 i = 0; i < count; ++i) {
        if (impl_->editor->getParameterInfo(i, info) != kResultOk) {
            continue;
        }
        PluginParameterInfo param;
        param.id = static_cast<uint32_t>(info.id);
        param.name = ParamTitle(info.title);
        param.unit = ParamTitle(info.units);
        param.description = ParamTitle(info.shortTitle);
        param.is_stepped = info.stepCount > 1;
        param.is_automatable = (info.flags & ParameterInfo::kCanAutomate) != 0;
        param.default_value = static_cast<float>(info.defaultNormalizedValue);
        param.min_value = 0.0f;
        param.max_value = 1.0f;
        result.push_back(std::move(param));
    }
    return result;
}

std::string Vst3ExternalHost::GetPluginName() const {
    return impl_->plugin_name;
}

bool Vst3ExternalHost::HasEditorView() const {
    // Capability check: the edit controller exists, so AttachEditorView can
    // lazily createView + attach. (plug_view itself is created on attach.)
    return impl_->editor != nullptr;
}

bool Vst3ExternalHost::CreateEditorView(std::string* error_out) {
    RemoveEditorView();
    if (impl_->editor == nullptr) {
        if (error_out) *error_out = "plugin has no edit controller";
        return false;
    }
    IPlugView* view = impl_->editor->createView(ViewType::kEditor);
    if (view == nullptr) {
        if (error_out) *error_out = "plugin provides no editor view";
        return false;
    }
    // createView returns an owned reference — adopt it without addRef.
    impl_->plug_view = IPtr<IPlugView>::adopt(view);
    return true;
}

bool Vst3ExternalHost::AttachEditorView(void* parent_hwnd, int left, int top,
                                        int right, int bottom, std::string* error_out) {
    if (impl_->plug_view == nullptr && !CreateEditorView(error_out)) {
        return false;
    }
    if (impl_->plug_view->attached(parent_hwnd, kPlatformTypeHWND) != kResultOk) {
        if (error_out) *error_out = "plugview attach failed";
        return false;
    }
    SetEditorFrame(left, top, right, bottom);
    return true;
}

void Vst3ExternalHost::SetEditorFrame(int left, int top, int right, int bottom) {
    if (impl_->plug_view == nullptr) {
        return;
    }
    ViewRect rect(static_cast<int32>(left), static_cast<int32>(top),
                  static_cast<int32>(right), static_cast<int32>(bottom));
    impl_->plug_view->onSize(&rect);
}

void Vst3ExternalHost::RemoveEditorView() {
    if (impl_->plug_view != nullptr) {
        impl_->plug_view->removed();
        impl_->plug_view = nullptr;
    }
}

}  // namespace XER::Audio::Plugin
