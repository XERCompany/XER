#include "dataset_iterator.h"

namespace XER::ML::DataStructures {

DatasetIterator::DatasetIterator(const DatasetBase& dataset)
		: dataset_(dataset) {
}

const DataSample* DatasetIterator::Next() {
	if (!HasNext()) {
		return nullptr;
	}
	return dataset_.GetSample(cursor_++);
}

void DatasetIterator::Reset() {
	cursor_ = 0U;
}

bool DatasetIterator::HasNext() const {
	return cursor_ < dataset_.Size();
}

}  // namespace XER::ML::DataStructures

