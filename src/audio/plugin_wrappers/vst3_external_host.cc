#include "vst3_external_host.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "vst3_plugin_scanner.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
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

// moduleinfo CID is a TUID written as 32 hex chars (2 per byte).
bool DecodeCid(const std::string& hex, TUID out /*char[16]*/) {
    if (hex.size() != 32) {
        return false;
    }
    const auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (int i = 0; i < 16; ++i) {
        const int hi = nib(hex[i * 2]);
        const int lo = nib(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out[i] = static_cast<char>((hi << 4) | lo);
    }
    return true;
}

std::vector<char> ReadWholeFile(const std::wstring& path) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        return {};
    }
    LARGE_INTEGER size {};
    std::vector<char> data;
    if (GetFileSizeEx(f, &size) != 0 && size.QuadPart > 0 &&
        size.QuadPart < (64 << 20)) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ReadFile(f, data.data(), static_cast<DWORD>(data.size()), &read,
                 nullptr);
        data.resize(read);
    }
    CloseHandle(f);
    return data;
}

// Minimal read-only IBStream over an in-memory preset file — the SDK's
// MemoryStream lives in sdk_common, which this build does not link.
class PresetStream final : public IBStream {
public:
    explicit PresetStream(std::vector<char> data)
        : data_(std::move(data)) {}

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, IBStream::iid, IBStream)
        QUERY_INTERFACE(iid, obj, FUnknown::iid, FUnknown)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs_; }
    uint32 PLUGIN_API release() override {
        const uint32 n = --refs_;
        if (n == 0) {
            delete this;
        }
        return n;
    }

    tresult PLUGIN_API read(void* buffer, int32 numBytes,
                            int32* numBytesRead) override {
        if (buffer == nullptr || numBytes < 0) {
            return kInvalidArgument;
        }
        const int64 avail = static_cast<int64>(data_.size()) - cursor_;
        const int32 n = static_cast<int32>(
            std::min<int64>(numBytes, avail));
        if (n > 0) {
            std::memcpy(buffer, data_.data() + cursor_, n);
            cursor_ += n;
        }
        if (numBytesRead != nullptr) {
            *numBytesRead = n;
        }
        return kResultOk;
    }
    tresult PLUGIN_API write(void*, int32, int32*) override {
        return kNotImplemented;
    }
    tresult PLUGIN_API seek(int64 pos, int32 mode,
                            int64* result) override {
        int64 base = 0;
        if (mode == kIBSeekCur) base = cursor_;
        if (mode == kIBSeekEnd) base = static_cast<int64>(data_.size());
        const int64 next = base + pos;
        if (next < 0) {
            return kInvalidArgument;
        }
        cursor_ = std::min<int64>(next, static_cast<int64>(data_.size()));
        if (result != nullptr) {
            *result = cursor_;
        }
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64* pos) override {
        if (pos == nullptr) {
            return kInvalidArgument;
        }
        *pos = cursor_;
        return kResultOk;
    }

private:
    ~PresetStream() = default;
    std::vector<char> data_;
    int64 cursor_ = 0;
    std::atomic<uint32> refs_{1};
};

// Applies one snapshot preset from a bundle's Contents\Resources dir to
// component + controller. Best-effort: a preset the plugin rejects is
// ignored. Prefers paths listed in moduleinfo, then scans the
// Snapshots dir for *.vstpreset.
void ApplyBundleSnapshot(const std::wstring& resources_dir,
                         const std::vector<std::string>& snapshot_paths,
                         IComponent* component, IEditController* editor) {
    if (component == nullptr) {
        return;
    }
    std::vector<std::wstring> files;
    for (const auto& rel : snapshot_paths) {
        files.push_back(resources_dir + L"\\" +
                        XER::Helper::String::Utf8ToWide(rel));
    }
    WIN32_FIND_DATAW fd {};
    HANDLE find = FindFirstFileW(
        (resources_dir + L"\\Snapshots\\*.vstpreset").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            files.push_back(resources_dir + L"\\Snapshots\\" +
                            fd.cFileName);
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    for (const auto& file : files) {
        const std::vector<char> data = ReadWholeFile(file);
        if (data.empty()) {
            continue;
        }
        // The stream stays alive until both readers release it.
        IPtr<IBStream> stream = IPtr<IBStream>::adopt(
            new PresetStream(std::vector<char>(data)));
        if (component->setState(stream) != kResultOk) {
            continue;
        }
        if (editor != nullptr) {
            stream->seek(0, IBStream::kIBSeekSet, nullptr);
            editor->setComponentState(stream);
        }
        return;
    }
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
    std::wstring resources_dir;   // bundle Contents\Resources (when read)

    // Always-valid ProcessData companions: empty parameter queues, empty
    // event lists and a minimal transport context — sloppy plugins read
    // these without null checks (a nullptr deref here was a real crash).
    Steinberg::Vst::ParameterChanges in_params;
    Steinberg::Vst::ParameterChanges out_params;
    Steinberg::Vst::EventList in_events;
    Steinberg::Vst::EventList out_events;
    ProcessContext process_context {};
    int64 processed_samples = 0;

#ifdef _WIN32
    std::vector<HWND> plugin_hwnds;       // child windows the plugin created
    struct Frame { int l, t, r, b; } last_frame {0, 0, 0, 0};
#endif
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

    // Bundles under the system VST3 dir carry Contents\Resources\ —
    // read moduleinfo.json for exact class selection and keep the
    // Resources dir for snapshots.
    const std::wstring bundle_root = Vst3BundleRootForModule(dll_path);
    Vst3ModuleInfo module_info;
    const bool have_module_info =
        IsCommonFilesVst3Path(bundle_root) &&
        ReadVst3ModuleInfo(bundle_root, &module_info);
    if (have_module_info) {
        impl_->resources_dir = bundle_root + L"\\Contents\\Resources";
    }

    // Prefer an audio module class; fall back to the first available class.
    PClassInfo class_info {};
    int32 selected_index = -1;
    const int32 class_count = factory->countClasses();
    // moduleinfo pins the class by CID — more reliable than matching the
    // category string when a bundle ships several classes.
    if (have_module_info) {
        TUID wanted {};
        bool have_cid = false;
        for (const auto& cls : module_info.classes) {
            if (cls.category == "Audio Module Class" &&
                DecodeCid(cls.cid, wanted)) {
                have_cid = true;
                break;
            }
        }
        if (have_cid) {
            for (int32 i = 0; i < class_count; ++i) {
                if (factory->getClassInfo(i, &class_info) == kResultOk &&
                    std::memcmp(class_info.cid, wanted, sizeof(TUID)) == 0) {
                    selected_index = i;
                    break;
                }
            }
        }
    }
    for (int32 i = 0; selected_index < 0 && i < class_count; ++i) {
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

    IEditController* raw_editor = nullptr;
    component->queryInterface(IEditController::iid, reinterpret_cast<void**>(&raw_editor));
    if (raw_editor == nullptr) {
        // Many plugins ship the edit controller as a separate class:
        // ask the component for its controller CID and create it via
        // the factory, linking it back to the component.
        TUID ctrl_cid {};
        if (component->getControllerClassId(ctrl_cid) == kResultOk) {
            FUnknown* ctrl = nullptr;
            if (factory->createInstance(ctrl_cid, IEditController::iid,
                                        reinterpret_cast<void**>(&ctrl)) == kResultOk &&
                ctrl != nullptr) {
                ctrl->queryInterface(IEditController::iid,
                                     reinterpret_cast<void**>(&raw_editor));
                ctrl->release();
                if (raw_editor != nullptr) {
                    IConnectionPoint* comp_cp = nullptr;
                    component->queryInterface(IConnectionPoint::iid,
                                              reinterpret_cast<void**>(&comp_cp));
                    IPtr<IConnectionPoint> comp_point = IPtr<IConnectionPoint>::adopt(comp_cp);
                    IConnectionPoint* ctrl_cp = nullptr;
                    raw_editor->queryInterface(IConnectionPoint::iid,
                                               reinterpret_cast<void**>(&ctrl_cp));
                    IPtr<IConnectionPoint> ctrl_point = IPtr<IConnectionPoint>::adopt(ctrl_cp);
                    if (comp_point && ctrl_point) {
                        comp_point->connect(ctrl_point);
                        ctrl_point->connect(comp_point);
                    }
                }
            }
        }
    }
    IPtr<IEditController> editor = IPtr<IEditController>::adopt(raw_editor);

    FUnknown* context = static_cast<FUnknown*>(&impl_->host_app);
    if (component->initialize(context) != kResultOk) {
        if (error_out) *error_out = "component initialize failed";
        return false;
    }
    if (editor && editor->initialize(context) != kResultOk) {
        editor = nullptr;
    }

    // canProcessSampleSize answers kResultTrue/kResultFalse; plugins that
    // do not override it return kNotImplemented — only an explicit
    // kResultFalse after initialize() counts as "unsupported", and
    // setupProcessing below remains the final authority. This host's
    // Process() path is 32-bit only, so a 64-bit-only plugin is declined.
    if (processor->canProcessSampleSize(kSample32) == kResultFalse) {
        component->terminate();
        if (error_out) {
            *error_out = processor->canProcessSampleSize(kSample64) == kResultTrue
                ? "plugin requires 64-bit processing, which this host does not support"
                : "plugin supports neither 32- nor 64-bit processing";
        }
        return false;
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
    std::fprintf(stderr,
                 "[vst3] %s: input_buses=%d output_buses=%d\n",
                 class_info.name, impl_->num_input_buses,
                 impl_->num_output_buses);
    for (int32 i = 0; i < impl_->num_input_buses + impl_->num_output_buses;
         ++i) {
        const BusDirection dir =
            i < impl_->num_input_buses ? kInput : kOutput;
        const int32 bi =
            i < impl_->num_input_buses ? i : i - impl_->num_input_buses;
        if (component->getBusInfo(kAudio, dir, bi, bus_info) == kResultOk) {
            std::fprintf(stderr,
                         "[vst3]   %s bus %d: '%s' channels=%d flags=0x%x\n",
                         dir == kInput ? "in " : "out", bi,
                         ParamTitle(bus_info.name).c_str(),
                         bus_info.channelCount, bus_info.flags);
        }
    }
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
    const tresult arr_res = processor->setBusArrangements(
        in_arr.empty() ? nullptr : in_arr.data(), impl_->num_input_buses,
        out_arr.empty() ? nullptr : out_arr.data(), impl_->num_output_buses);
    std::fprintf(stderr, "[vst3] setBusArrangements -> 0x%x\n",
                 static_cast<unsigned>(arr_res));

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

    // setActive allocates the plugin's DSP buffers; calling process()
    // without it reads null internal state — observed crash.
    if (component->setActive(true) != kResultOk) {
        component->terminate();
        if (error_out) *error_out = "component setActive failed";
        return false;
    }
    if (processor->setProcessing(true) != kResultOk) {
        component->setActive(false);
        component->terminate();
        if (error_out) *error_out = "setProcessing failed";
        return false;
    }

    impl_->component = std::move(component);
    impl_->processor = std::move(processor);
    impl_->editor = std::move(editor);
    if (impl_->editor != nullptr) {
        const int32 pc = impl_->editor->getParameterCount();
        std::fprintf(stderr, "[vst3] params=%d\n", pc);
        ParameterInfo pi {};
        for (int32 i = 0; i < pc; ++i) {
            if (impl_->editor->getParameterInfo(i, pi) == kResultOk) {
                std::fprintf(stderr, "[vst3]   param %d: id=%u '%s' def=%.3f\n",
                    i, pi.id, ParamTitle(pi.title).c_str(),
                    pi.defaultNormalizedValue);
            }
        }
    }
    impl_->plugin_name = !module_info.name.empty() ? module_info.name
                                                  : class_info.name;

    // Bundle Resources: apply the first snapshot preset (if any) so the
    // plugin comes up in its documented default state.
    if (have_module_info) {
        std::vector<std::string> snapshots;
        for (const auto& cls : module_info.classes) {
            if (cls.category == "Audio Module Class") {
                snapshots = cls.snapshots;
                break;
            }
        }
        ApplyBundleSnapshot(impl_->resources_dir, snapshots,
                            impl_->component.get(), impl_->editor.get());
    }
    return true;
}

void Vst3ExternalHost::Unload() {
    RemoveEditorView();
    if (impl_->processor) {
        impl_->processor->setProcessing(false);
    }
    if (impl_->component) {
        impl_->component->setActive(false);
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
    impl_->resources_dir.clear();
}

std::string Vst3ExternalHost::GetResourcesPath() const {
    return XER::Helper::String::WideToUtf8(impl_->resources_dir);
}

bool Vst3ExternalHost::IsLoaded() const {
    return impl_->processor != nullptr;
}

bool Vst3ExternalHost::Process(const float* input, float* output, uint32_t frames) {
    const float* ins[] = {input};
    float* outs[] = {output};
    return ProcessChannels(ins, outs, 1, frames);
}

bool Vst3ExternalHost::ProcessChannels(const float* const* inputs,
                                       float* const* outputs,
                                       int32_t channels, uint32_t frames) {
    if (!IsLoaded() || inputs == nullptr || outputs == nullptr ||
        channels <= 0 || frames == 0 || frames > impl_->max_block) {
        return false;
    }
    for (int32 c = 0; c < channels; ++c) {
        if (inputs[c] == nullptr || outputs[c] == nullptr) {
            return false;
        }
    }

    // Map caller channels onto every input bus: channel c of the caller
    // drives channel c of the bus; when the bus is wider than the source,
    // the last source channel is repeated (dual-mono for mono input).
    for (int32 b = 0; b < impl_->num_input_buses; ++b) {
        auto& ptrs = impl_->input_channel_ptrs[static_cast<size_t>(b)];
        for (size_t c = 0; c < ptrs.size(); ++c) {
            const int32 src = std::min<int32>(static_cast<int32>(c), channels - 1);
            ptrs[c] = const_cast<Sample32*>(inputs[src]);
        }
    }

    auto& ctx = impl_->process_context;
    ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid |
                ProcessContext::kTimeSigValid | ProcessContext::kContTimeValid |
                ProcessContext::kProjectTimeMusicValid;
    ctx.sampleRate = impl_->sample_rate;
    ctx.projectTimeSamples = impl_->processed_samples;
    ctx.continousTimeSamples = impl_->processed_samples;
    ctx.tempo = 120.0;
    ctx.timeSigNumerator = 4;
    ctx.timeSigDenominator = 4;
    ctx.projectTimeMusic =
        impl_->processed_samples / (impl_->sample_rate * 0.5);

    ProcessData data {};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = static_cast<int32>(frames);
    data.numInputs = impl_->num_input_buses;
    data.numOutputs = impl_->num_output_buses;
    data.inputs = impl_->input_buses.empty() ? nullptr : impl_->input_buses.data();
    data.outputs = impl_->output_buses.empty() ? nullptr : impl_->output_buses.data();
    data.inputParameterChanges = &impl_->in_params;
    data.outputParameterChanges = &impl_->out_params;
    data.inputEvents = &impl_->in_events;
    data.outputEvents = &impl_->out_events;
    data.processContext = &ctx;

    const tresult pr = impl_->processor->process(data);
    impl_->processed_samples += frames;
    static std::atomic<int> dbg{0};
    if (dbg.fetch_add(1) < 5 && !impl_->output_scratch.empty() &&
        !impl_->output_scratch[0].empty()) {
        float pk = 0.0f;
        for (float v : impl_->output_scratch[0][0]) {
            pk = std::max(pk, std::fabs(v));
        }
        std::fprintf(stderr,
                     "[vst3] process -> 0x%x, out peak %.4f\n",
                     static_cast<unsigned>(pr), pk);
    }
    if (pr != kResultOk) {
        return false;
    }

    // Copy the first output bus back per channel; zero-fill any caller
    // channel the bus does not provide.
    if (impl_->num_output_buses > 0 && !impl_->output_scratch.empty()) {
        const auto& bus0 = impl_->output_scratch[0];
        for (int32 c = 0; c < channels; ++c) {
            if (static_cast<size_t>(c) < bus0.size() &&
                !bus0[static_cast<size_t>(c)].empty()) {
                const auto& src = bus0[static_cast<size_t>(c)];
                const size_t copy_count = std::min<size_t>(frames, src.size());
                std::memcpy(outputs[c], src.data(), copy_count * sizeof(float));
                for (size_t i = copy_count; i < frames; ++i) {
                    outputs[c][i] = 0.0f;
                }
            } else {
                std::memset(outputs[c], 0, frames * sizeof(float));
            }
        }
    } else {
        for (int32 c = 0; c < channels; ++c) {
            std::memset(outputs[c], 0, frames * sizeof(float));
        }
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

#ifdef _WIN32
namespace {
struct EnumCtx {
    std::vector<HWND>* list;
};
BOOL CALLBACK CollectChildren(HWND hwnd, LPARAM lp) {
    reinterpret_cast<EnumCtx*>(lp)->list->push_back(hwnd);
    return TRUE;
}
std::vector<HWND> ChildWindowsOf(HWND parent) {
    std::vector<HWND> out;
    EnumCtx ctx{&out};
    EnumChildWindows(parent, CollectChildren, reinterpret_cast<LPARAM>(&ctx));
    return out;
}
}  // namespace
#endif

bool Vst3ExternalHost::AttachEditorView(void* parent_hwnd, int left, int top,
                                        int right, int bottom, std::string* error_out) {
    if (impl_->plug_view == nullptr && !CreateEditorView(error_out)) {
        return false;
    }
#ifdef _WIN32
    const HWND parent = static_cast<HWND>(parent_hwnd);
    const auto before = ChildWindowsOf(parent);
#endif
    if (impl_->plug_view->attached(parent_hwnd, kPlatformTypeHWND) != kResultOk) {
        if (error_out) *error_out = "plugview attach failed";
        return false;
    }
#ifdef _WIN32
    // Many plugins size/anchor their own child window instead of honoring
    // onSize — find the HWNDs they created and pin them to the frame rect.
    const auto after = ChildWindowsOf(parent);
    impl_->plugin_hwnds.clear();
    for (HWND w : after) {
        if (std::find(before.begin(), before.end(), w) == before.end()) {
            impl_->plugin_hwnds.push_back(w);
        }
    }
#endif
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
#ifdef _WIN32
    impl_->last_frame = {left, top, right, bottom};
    for (HWND w : impl_->plugin_hwnds) {
        if (IsWindow(w)) {
            SetWindowPos(w, nullptr, left, top, right - left, bottom - top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
#endif
}

void Vst3ExternalHost::RemoveEditorView() {
    if (impl_->plug_view != nullptr) {
        impl_->plug_view->removed();
        impl_->plug_view = nullptr;
    }
}

}  // namespace XER::Audio::Plugin
