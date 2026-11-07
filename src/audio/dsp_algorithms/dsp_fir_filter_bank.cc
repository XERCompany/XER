#include "dsp_fir_filter_bank.h"

#include <algorithm>
#include <cstring>
#include <future>
#include <thread>

#if defined(__AVX2__) || defined(__SSE2__)
#include <immintrin.h>
#endif

namespace XER::Engine::Audio::DSP {

namespace {

constexpr size_t kParallelFilterThreshold = 16;
constexpr size_t kMinFiltersPerWorker = 4;

#if defined(__AVX2__)
float HorizontalSum(__m256 values) {
    alignas(32) float lanes[8];
    _mm256_store_ps(lanes, values);
    float sum = 0.0f;
    for (float value : lanes) {
        sum += value;
    }
    return sum;
}
#elif defined(__SSE2__)
float HorizontalSum(__m128 values) {
    alignas(16) float lanes[4];
    _mm_store_ps(lanes, values);
    float sum = 0.0f;
    for (float value : lanes) {
        sum += value;
    }
    return sum;
}
#endif

void PushSample(std::vector<float>& state, float input) {
    if (state.empty()) {
        return;
    }
    if (state.size() > 1) {
        std::memmove(state.data() + 1, state.data(), (state.size() - 1) * sizeof(float));
    }
    state[0] = input;
}

float DotProduct(const std::vector<float>& taps, const std::vector<float>& state) {
    size_t tap_index = 0;
#if defined(__AVX2__)
    __m256 accumulator = _mm256_setzero_ps();
    for (; tap_index + 7 < taps.size(); tap_index += 8) {
        const __m256 tap_values = _mm256_loadu_ps(taps.data() + tap_index);
        const __m256 state_values = _mm256_loadu_ps(state.data() + tap_index);
        accumulator = _mm256_add_ps(accumulator, _mm256_mul_ps(tap_values, state_values));
    }
    float result = HorizontalSum(accumulator);
#elif defined(__SSE2__)
    __m128 accumulator = _mm_setzero_ps();
    for (; tap_index + 3 < taps.size(); tap_index += 4) {
        const __m128 tap_values = _mm_loadu_ps(taps.data() + tap_index);
        const __m128 state_values = _mm_loadu_ps(state.data() + tap_index);
        accumulator = _mm_add_ps(accumulator, _mm_mul_ps(tap_values, state_values));
    }
    float result = HorizontalSum(accumulator);
#else
    float result = 0.0f;
#endif
    for (; tap_index < taps.size(); ++tap_index) {
        result += taps[tap_index] * state[tap_index];
    }
    return result;
}

float ProcessFilter(const std::vector<float>& taps, std::vector<float>& state, float input) {
    PushSample(state, input);
    return DotProduct(taps, state);
}

}  // namespace

std::string FIRFilterBank::GetMemoryStats() const {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    size_t filters_bytes = 0;
    for (const auto& filter : filters_) {
        filters_bytes += filter.size() * sizeof(float);
    }
    size_t states_bytes = 0;
    for (const auto& state : states_) {
        states_bytes += state.size() * sizeof(float);
    }

    std::string stats;
    stats += "Filters: " + std::to_string(filters_bytes) + " bytes\n";
    stats += "States: " + std::to_string(states_bytes) + " bytes\n";
    return stats;
}

FIRFilterBank::FIRFilterBank()
    : mutex_("fir_filter_bank") {}

FIRFilterBank::~FIRFilterBank() = default;

void FIRFilterBank::Clear() {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    filters_.clear();
    states_.clear();
}

bool FIRFilterBank::AddFilter(const std::vector<float>& taps) {
    if (taps.empty()) {
        return false;
    }

    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    filters_.push_back(taps);
    states_.push_back(std::vector<float>(taps.size(), 0.0f));
    return true;
}

size_t FIRFilterBank::GetFilterCount() const {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    return filters_.size();
}

bool FIRFilterBank::ProcessSample(float input, std::vector<float>& outputs) {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    const size_t filter_count = filters_.size();
    outputs.resize(filter_count);
    if (filter_count == 0) {
        return true;
    }

    const auto process_range = [&](size_t start_index, size_t end_index) {
        for (size_t filter_index = start_index; filter_index < end_index; ++filter_index) {
            outputs[filter_index] = ProcessFilter(filters_[filter_index], states_[filter_index], input);
        }
    };

    const size_t hardware_threads = std::max<size_t>(1, std::thread::hardware_concurrency());
    const size_t max_useful_workers = (filter_count + kMinFiltersPerWorker - 1) / kMinFiltersPerWorker;
    const size_t worker_count = std::min(hardware_threads, max_useful_workers);
    if (filter_count < kParallelFilterThreshold || worker_count < 2) {
        process_range(0, filter_count);
        return true;
    }

    const size_t chunk_size = (filter_count + worker_count - 1) / worker_count;
    std::vector<std::thread> workers;
    workers.reserve(worker_count - 1);
    for (size_t worker_index = 1; worker_index < worker_count; ++worker_index) {
        const size_t start_index = worker_index * chunk_size;
        const size_t end_index = std::min(filter_count, start_index + chunk_size);
        if (start_index < end_index) {
            workers.emplace_back(process_range, start_index, end_index);
        }
    }

    process_range(0, std::min(filter_count, chunk_size));
    for (auto& worker : workers) {
        worker.join();
    }
    return true;
}

std::string FIRFilterBank::GetReport() const {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    return "FIRBank: filters=" + std::to_string(filters_.size());
}

std::future<bool> FIRFilterBank::ProcessSampleAsync(float input, std::vector<float>& outputs) {
    return std::async(std::launch::async, [this, input, &outputs]() {
        return this->ProcessSample(input, outputs);
    });
}

}  // namespace XER::Engine::Audio::DSP
