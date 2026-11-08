#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace XER::Flux::History {

class CommandHistory {
public:
	explicit CommandHistory(std::size_t max_entries = 128);

	void Push(std::string command);
	void Clear();
	void ResetCursor();

	bool empty() const;
	std::size_t size() const;
	std::string Latest() const;
	std::string Previous();
	std::string Next();

	std::vector<std::string> Entries() const;
	std::vector<std::string> MatchingPrefix(std::string_view prefix, std::size_t limit = 0) const;

private:
	std::size_t max_entries_ = 128;
	std::vector<std::string> entries_;
	std::size_t cursor_ = 0;
};

}  // namespace XER::Flux::History
