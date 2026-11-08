#include "metric_cpu_gpu_usage.h"

#include <fstream>
#include <numeric>
#include <thread>
#include <chrono>

#if defined(__linux__)
#include <unistd.h>
#include <dlfcn.h>
#endif

namespace XER::ModelsBuilder::TrainingMetrics {

namespace {

#if defined(__linux__)
// Read two snapshots of /proc/stat and compute overall CPU utilisation %.
double QueryCpuPercent() {
    auto readStat = [](long long& idle, long long& total) {
        std::ifstream f("/proc/stat");
        if (!f.good()) { idle = 0; total = 1; return; }
        std::string label;
        long long user, nice, system, idle_v, iowait, irq, softirq, steal;
        f >> label >> user >> nice >> system >> idle_v >> iowait >> irq >> softirq >> steal;
        idle  = idle_v + iowait;
        total = user + nice + system + idle_v + iowait + irq + softirq + steal;
    };
    long long idle0, total0, idle1, total1;
    readStat(idle0, total0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    readStat(idle1, total1);
    const long long dt = total1 - total0;
    if (dt <= 0) return 0.0;
    const long long didle = idle1 - idle0;
    return 100.0 * (1.0 - static_cast<double>(didle) / static_cast<double>(dt));
}

// Query GPU utilisation % for device 0 via NVML.
double QueryGpuPercent() {
    void* lib = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (!lib) return 0.0;

    using nvmlReturn_t  = int;
    using nvmlDevice_t  = void*;
    constexpr nvmlReturn_t kOk = 0;
    struct NvmlUtilization { unsigned int gpu, memory; };

    using PfnInit       = nvmlReturn_t (*)();
    using PfnHandle     = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
    using PfnUtil       = nvmlReturn_t (*)(nvmlDevice_t, NvmlUtilization*);
    using PfnShutdown   = nvmlReturn_t (*)();

    auto pfnInit     = reinterpret_cast<PfnInit>    (dlsym(lib, "nvmlInit_v2"));
    auto pfnHandle   = reinterpret_cast<PfnHandle>  (dlsym(lib, "nvmlDeviceGetHandleByIndex_v2"));
    auto pfnUtil     = reinterpret_cast<PfnUtil>    (dlsym(lib, "nvmlDeviceGetUtilizationRates"));
    auto pfnShutdown = reinterpret_cast<PfnShutdown>(dlsym(lib, "nvmlShutdown"));

    double gpu_pct = 0.0;
    if (pfnInit && pfnHandle && pfnUtil && pfnInit() == kOk) {
        nvmlDevice_t dev = nullptr;
        if (pfnHandle(0, &dev) == kOk && dev) {
            NvmlUtilization u{};
            if (pfnUtil(dev, &u) == kOk) {
                gpu_pct = static_cast<double>(u.gpu);
            }
        }
        if (pfnShutdown) pfnShutdown();
    }
    dlclose(lib);
    return gpu_pct;
}
#endif

}  // namespace

double MeanBy(const std::vector<ResourceUsageSample>& samples,
							double (*selector)(const ResourceUsageSample&)) {
	if (samples.empty()) {
		return 0.0;
	}
	double total = 0.0;
	for (const auto& sample : samples) {
		total += selector(sample);
	}
	return total / static_cast<double>(samples.size());
}

double CpuSelector(const ResourceUsageSample& sample) {
	return sample.cpu_percent;
}

double GpuSelector(const ResourceUsageSample& sample) {
	return sample.gpu_percent;
}

double MemorySelector(const ResourceUsageSample& sample) {
	return sample.memory_mb;
}

void CpuGpuUsageMetric::Reset() {
	samples_.clear();
}

ResourceUsageSample CpuGpuUsageMetric::CaptureSample() {
	ResourceUsageSample sample;
#if defined(__linux__)
        sample.cpu_percent = QueryCpuPercent();
        sample.gpu_percent = QueryGpuPercent();
#else
        sample.cpu_percent = 0.0;
        sample.gpu_percent = 0.0;
#endif
	samples_.push_back(sample);
	return sample;
}

double CpuGpuUsageMetric::MeanCpuPercent() const {
	return MeanBy(samples_, &CpuSelector);
}

double CpuGpuUsageMetric::MeanGpuPercent() const {
	return MeanBy(samples_, &GpuSelector);
}

double CpuGpuUsageMetric::MeanMemoryMb() const {
	return MeanBy(samples_, &MemorySelector);
}

double CpuGpuUsageMetric::QueryProcessMemoryMb() const {
#if defined(__linux__)
	std::ifstream statm_file("/proc/self/statm");
	if (!statm_file.good()) {
		return 0.0;
	}

	long total_pages = 0;
	long resident_pages = 0;
	statm_file >> total_pages >> resident_pages;
	if (resident_pages <= 0) {
		return 0.0;
	}

	const long page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0) {
		return 0.0;
	}
	const double bytes = static_cast<double>(resident_pages) * static_cast<double>(page_size);
	return bytes / (1024.0 * 1024.0);
#else
	return 0.0;
#endif
}

}  // namespace XER::ModelsBuilder::TrainingMetrics

