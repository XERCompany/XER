#include "file_diff.h"
#include "string_utils.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace XER::Engine::Helper {

namespace {

}  // namespace

FileDiff::FileDiff(std::string before, std::string after)
	: diff_{} {
	diff_.before = std::move(before);
	diff_.after = std::move(after);
	diff_.equal = (diff_.before == diff_.after);

	const auto left = String::SplitLines(diff_.before);
	const auto right = String::SplitLines(diff_.after);

	const std::size_t n = left.size();
	const std::size_t m = right.size();
	std::vector<std::vector<std::size_t>> lcs(n + 1, std::vector<std::size_t>(m + 1, 0));

	for (std::size_t i = n; i > 0; --i) {
		for (std::size_t j = m; j > 0; --j) {
			if (left[i - 1] == right[j - 1]) {
				lcs[i - 1][j - 1] = lcs[i][j] + 1;
			} else {
				lcs[i - 1][j - 1] = std::max(lcs[i][j - 1], lcs[i - 1][j]);
			}
		}
	}

	std::size_t i = 0;
	std::size_t j = 0;
	while (i < n && j < m) {
		if (left[i] == right[j]) {
			++i;
			++j;
			continue;
		}

		if (lcs[i + 1][j] >= lcs[i][j + 1]) {
			diff_.removed_lines.push_back(left[i]);
			diff_.operations.push_back({FileDiffOperationType::Removed, left[i]});
			++i;
		} else {
			diff_.added_lines.push_back(right[j]);
			diff_.operations.push_back({FileDiffOperationType::Added, right[j]});
			++j;
		}
	}

	while (i < n) {
		diff_.removed_lines.push_back(left[i]);
		diff_.operations.push_back({FileDiffOperationType::Removed, left[i]});
		++i;
	}

	while (j < m) {
		diff_.added_lines.push_back(right[j]);
		diff_.operations.push_back({FileDiffOperationType::Added, right[j]});
		++j;
	}

	diff_.added_count = diff_.added_lines.size();
	diff_.removed_count = diff_.removed_lines.size();
}

const FileDiffReturn& FileDiff::returnDiffFile() const {
	return diff_;
}

}