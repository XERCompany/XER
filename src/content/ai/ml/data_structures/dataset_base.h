#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace XER::ML::DataStructures {

struct DataSample {
	std::vector<float> features;
	float label = 0.0f;
};

class DatasetBase {
 public:
	DatasetBase() = default;
	explicit DatasetBase(std::string name);

	void AddSample(DataSample sample);
	const DataSample* GetSample(size_t index) const;
	size_t Size() const { return samples_.size(); }
	bool Empty() const { return samples_.empty(); }

	const std::string& Name() const { return name_; }
	void SetName(std::string name) { name_ = std::move(name); }

	const std::vector<DataSample>& Samples() const { return samples_; }

 private:
	std::string name_{"dataset"};
	std::vector<DataSample> samples_;
};

}  // namespace XER::ML::DataStructures

