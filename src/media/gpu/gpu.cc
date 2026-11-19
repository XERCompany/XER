#include "media/gpu/gpu_media.h"

namespace XER::Media {

GpuVideoAcceleratorFactory& GpuVideoAcceleratorFactory::Instance() {
    static GpuVideoAcceleratorFactory instance;
    return instance;
}

VideoDecodeAccelerator* GpuVideoAcceleratorFactory::AcceleratorFor(
    CodecId codec) {
    for (auto& a : accelerators_)
        if (a->supports(codec)) return a.get();
    return nullptr;
}

void GpuVideoAcceleratorFactory::RegisterAccelerator(
    std::unique_ptr<VideoDecodeAccelerator> accel) {
    accelerators_.push_back(std::move(accel));
}

std::vector<CodecId> GpuVideoAcceleratorFactory::SupportedCodecs() const {
    std::vector<CodecId> out;
    for (CodecId c : {CodecId::kAV1, CodecId::kH264, CodecId::kH265,
                      CodecId::kVP8, CodecId::kVP9})
        if (const_cast<GpuVideoAcceleratorFactory*>(this)->AcceleratorFor(c))
            out.push_back(c);
    return out;
}

}  // namespace XER::Media
