#include "dataset_window.h"

#include <algorithm>

namespace XER::ML::DataStructures {

std::vector<DatasetBase> DatasetWindow::BuildWindows(const DatasetBase& dataset,
        size_t window_size,
        size_t stride) {
	std::vector<DatasetBase> windows;
	if (dataset.Empty() || window_size == 0U || stride == 0U) {
		return windows;
	}

	for (size_t start = 0; start < dataset.Size(); start += stride) {
		const size_t end = std::min(dataset.Size(), start + window_size);
		if (end <= start) {
			break;
		}

		DatasetBase window(dataset.Name() + "_window");
		for (size_t i = start; i < end; ++i) {
			const DataSample* sample = dataset.GetSample(i);
			if (sample != nullptr) {
				window.AddSample(*sample);
			}
		}

		if (!window.Empty()) {
			windows.push_back(std::move(window));
		}

		if (end == dataset.Size()) {
			break;
		}
	}
	return windows;
}

}  // namespace XER::ML::DataStructures

