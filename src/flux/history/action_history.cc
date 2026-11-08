#include "flux/history/action_history.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace XER::Flux::History {

ActionHistory::ActionHistory(std::size_t max_entries)
	: max_entries_(max_entries == 0 ? 1 : max_entries) {}

void ActionHistory::Push(std::string category, std::string description) {
	entries_.push_back(ActionRecord{next_sequence_++, std::move(category), std::move(description)});
	while (entries_.size() > max_entries_) {
		entries_.pop_front();
	}
}

void ActionHistory::Clear() {
	entries_.clear();
}

bool ActionHistory::empty() const {
	return entries_.empty();
}

std::size_t ActionHistory::size() const {
	return entries_.size();
}

const ActionRecord* ActionHistory::Latest() const {
	return entries_.empty() ? nullptr : &entries_.back();
}

std::vector<ActionRecord> ActionHistory::Entries() const {
	return std::vector<ActionRecord>(entries_.begin(), entries_.end());
}

std::vector<ActionRecord> ActionHistory::Recent(std::size_t limit) const {
	if (limit == 0 || limit >= entries_.size()) {
		return Entries();
	}
	return std::vector<ActionRecord>(entries_.end() - static_cast<std::ptrdiff_t>(limit), entries_.end());
}

}  // namespace XER::Flux::History
