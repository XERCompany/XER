#include "audio_interleave_processor.h"

#include <algorithm>
#include <cstring>
#include <future>
#include <thread>

namespace Engine::Audio::Core {

namespace {

unsigned CpuWorkerCount(size_t work_items) {
    if (work_items == 0u) return 1u;
    const unsigned hardware = std::thread::hardware_concurrency();
    return static_cast<unsigned>(std::min<size_t>(work_items, std::max(1u, hardware)));
}

template <typename Callback>
void RunParallelFrames(size_t frame_count, Callback&& callback) {
    constexpr size_t kMinimumFramesPerWorker = 65536u;
    const size_t worker_count = std::min<size_t>(
        CpuWorkerCount(frame_count),
        std::max<size_t>(1u, frame_count / kMinimumFramesPerWorker));
    if (worker_count <= 1u) {
        callback(0u, frame_count);
        return;
    }

    std::vector<std::future<void>> futures;
    futures.reserve(worker_count - 1u);
    for (size_t worker = 1u; worker < worker_count; ++worker) {
        const size_t begin = frame_count * worker / worker_count;
        const size_t end = frame_count * (worker + 1u) / worker_count;
        futures.push_back(std::async(std::launch::async, callback, begin, end));
    }
    callback(0u, frame_count / worker_count);
    for (auto& future : futures) {
        future.get();
    }
}

}  // namespace

AudioInterleaveProcessor::AudioInterleaveProcessor() {}

AudioInterleaveProcessor::~AudioInterleaveProcessor() {}

bool AudioInterleaveProcessor::Interleave(
    const float* const* planar_data,
    int channel_count,
    size_t frame_count,
    float* interleaved_output) {
    
    if (!planar_data || !interleaved_output || channel_count <= 0 || frame_count == 0) {
        return false;
    }

    RunParallelFrames(frame_count, [&](size_t begin, size_t end) {
        for (size_t frame = begin; frame < end; ++frame) {
            for (int ch = 0; ch < channel_count; ++ch) {
                interleaved_output[frame * static_cast<size_t>(channel_count) + static_cast<size_t>(ch)] = planar_data[ch][frame];
            }
        }
    });
    
    return true;
}

bool AudioInterleaveProcessor::Deinterleave(
    const float* interleaved_data,
    int channel_count,
    size_t frame_count,
    float* const* planar_output) {
    
    if (!interleaved_data || !planar_output || channel_count <= 0 || frame_count == 0) {
        return false;
    }

    RunParallelFrames(frame_count, [&](size_t begin, size_t end) {
        for (size_t frame = begin; frame < end; ++frame) {
            for (int ch = 0; ch < channel_count; ++ch) {
                planar_output[ch][frame] = interleaved_data[frame * static_cast<size_t>(channel_count) + static_cast<size_t>(ch)];
            }
        }
    });
    
    return true;
}

bool AudioInterleaveProcessor::InterleavePlanar(
    std::vector<std::vector<float>>& channels,
    size_t frame_count) {
    
    if (channels.empty() || frame_count == 0) {
        return false;
    }

    int channel_count = static_cast<int>(channels.size());
    size_t total_samples = channel_count * frame_count;
    
    // Create interleaved buffer
    std::vector<float> temp(total_samples);
    
    // Pointers for Interleave call
    std::vector<const float*> ptrs(channel_count);
    for (int i = 0; i < channel_count; ++i) {
        ptrs[i] = channels[i].data();
    }
    
    if (!Interleave(ptrs.data(), channel_count, frame_count, temp.data())) {
        return false;
    }
    
    // Replace all channels with single interleaved vector
    channels.clear();
    channels.push_back(std::move(temp));
    
    return true;
}

bool AudioInterleaveProcessor::DeinterleavePlanar(
    const float* interleaved,
    int channel_count,
    size_t frame_count,
    std::vector<std::vector<float>>& output_channels) {
    
    if (!interleaved || channel_count <= 0 || frame_count == 0) {
        return false;
    }

    output_channels.clear();
    output_channels.resize(channel_count);
    
    for (int ch = 0; ch < channel_count; ++ch) {
        output_channels[ch].resize(frame_count);
    }

    // Create pointers for Deinterleave call
    std::vector<float*> ptrs(channel_count);
    for (int i = 0; i < channel_count; ++i) {
        ptrs[i] = output_channels[i].data();
    }

    return Deinterleave(interleaved, channel_count, frame_count, ptrs.data());
}

bool AudioInterleaveProcessor::DuplicateChannel(
    const float* source,
    float* dest,
    size_t frame_count) {
    
    if (!source || !dest) return false;
    
    std::memcpy(dest, source, frame_count * sizeof(float));
    return true;
}

bool AudioInterleaveProcessor::MixChannels(
    const float* const* inputs,
    int input_channels,
    float* output,
    size_t frame_count) {
    
    if (!inputs || !output || input_channels <= 0 || frame_count == 0) {
        return false;
    }

    const float scale = 1.0f / static_cast<float>(input_channels);
    RunParallelFrames(frame_count, [&](size_t begin, size_t end) {
        for (size_t frame = begin; frame < end; ++frame) {
            float sum = 0.0f;
            for (int ch = 0; ch < input_channels; ++ch) {
                sum += inputs[ch][frame];
            }
            output[frame] = sum * scale;
        }
    });
    
    return true;
}

}  // namespace Engine::Audio::Core
