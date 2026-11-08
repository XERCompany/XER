#include "ml_profiler.h"

namespace XER::ML::Utils {

MlProfiler::ScopedProfile::ScopedProfile(MlProfiler& profiler, std::string_view key)
		: profiler_(profiler), key_(key), start_(Clock::now()) {
	profiler_.Begin(key_);
}

MlProfiler::ScopedProfile::~ScopedProfile() {
	profiler_.End(key_);
}

void MlProfiler::Begin(std::string_view key) {
	starts_[std::string(key)] = Clock::now();
}

void MlProfiler::End(std::string_view key) {
	const std::string resolved(key);
	const auto it = starts_.find(resolved);
	if (it == starts_.end()) {
		return;
	}

	const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
			Clock::now() - it->second);
	last_millis_[resolved] = elapsed.count();
	starts_.erase(it);
}

double MlProfiler::GetLastMillis(std::string_view key) const {
	const auto it = last_millis_.find(std::string(key));
	if (it == last_millis_.end()) {
		return 0.0;
	}
	return it->second;
}

std::unordered_map<std::string, double> MlProfiler::SnapshotMillis() const {
	return last_millis_;
}

}  // namespace XER::ML::Utils

