#pragma once

#include "dataset_base.h"

namespace XER::ML::DataStructures {

class DatasetIterator {
 public:
	explicit DatasetIterator(const DatasetBase& dataset);

	const DataSample* Next();
	void Reset();
	bool HasNext() const;

 private:
	const DatasetBase& dataset_;
	size_t cursor_ = 0U;
};

}  // namespace XER::ML::DataStructures

