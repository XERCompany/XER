#pragma once

#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>

namespace XER::ML::Utils {

class MlProfiler {
 public:
	using Clock = std::chrono::steady_clock;

	class ScopedProfile {
	 public:
		ScopedProfile(MlProfiler& profiler, std::string_view key);
		~ScopedProfile();

	 private:
		MlProfiler& profiler_;
		std::string key_;
		Clock::time_point start_;
	};

	void Begin(std::string_view key);
	void End(std::string_view key);

	double GetLastMillis(std::string_view key) const;
	std::unordered_map<std::string, double> SnapshotMillis() const;

 private:
	std::unordered_map<std::string, Clock::time_point> starts_;
	std::unordered_map<std::string, double> last_millis_;
};

}  // namespace XER::ML::Utils

