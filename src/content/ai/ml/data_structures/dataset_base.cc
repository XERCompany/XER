#include "dataset_base.h"

namespace XER::ML::DataStructures {

DatasetBase::DatasetBase(std::string name)
		: name_(std::move(name)) {
}

void DatasetBase::AddSample(DataSample sample) {
	samples_.push_back(std::move(sample));
}

const DataSample* DatasetBase::GetSample(size_t index) const {
	if (index >= samples_.size()) {
		return nullptr;
	}
	return &samples_[index];
}

}  // namespace XER::ML::DataStructures

