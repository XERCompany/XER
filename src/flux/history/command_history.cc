#include "flux/history/command_history.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace XER::Flux::History {
CommandHistory::CommandHistory(std::size_t max_entries)
	: max_entries_(max_entries == 0 ? 1 : max_entries),
	  cursor_(0) {}

void CommandHistory::Push(std::string command) {
	command = XER::Helper::String::TrimAsciiWhitespaceCopy(command);
	if (command.empty()) {
		ResetCursor();
		return;
	}
	if (!entries_.empty() && entries_.back() == command) {
		ResetCursor();
		return;
	}
	entries_.push_back(std::move(command));
	if (entries_.size() > max_entries_) {
		entries_.erase(entries_.begin());
	}
	ResetCursor();
}

void CommandHistory::Clear() {
	entries_.clear();
	ResetCursor();
}

void CommandHistory::ResetCursor() {
	cursor_ = entries_.size();
}

bool CommandHistory::empty() const {
	return entries_.empty();
}

std::size_t CommandHistory::size() const {
	return entries_.size();
}

std::string CommandHistory::Latest() const {
	return entries_.empty() ? std::string() : entries_.back();
}

std::string CommandHistory::Previous() {
	if (entries_.empty()) {
		return std::string();
	}
	if (cursor_ == 0) {
		return entries_.front();
	}
	--cursor_;
	return entries_[cursor_];
}

std::string CommandHistory::Next() {
	if (entries_.empty()) {
		return std::string();
	}
	if (cursor_ >= entries_.size()) {
		return std::string();
	}
	++cursor_;
	return cursor_ < entries_.size() ? entries_[cursor_] : std::string();
}

std::vector<std::string> CommandHistory::Entries() const {
	return entries_;
}

std::vector<std::string> CommandHistory::MatchingPrefix(std::string_view prefix, std::size_t limit) const {
	std::vector<std::string> matches;
	for (auto iterator = entries_.rbegin(); iterator != entries_.rend(); ++iterator) {
		if (XER::Helper::String::StartsWith(*iterator, prefix)) {
			matches.push_back(*iterator);
			if (limit > 0 && matches.size() >= limit) {
				break;
			}
		}
	}
	return matches;
}

}  // namespace XER::Flux::History
