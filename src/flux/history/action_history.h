#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace XER::flux::history {

struct ActionRecord {
	std::size_t sequence = 0;
	std::string category;
	std::string description;
};

class ActionHistory {
public:
	explicit ActionHistory(std::size_t max_entries = 128);

	void Push(std::string category, std::string description);
	void Clear();

	bool empty() const;
	std::size_t size() const;
	const ActionRecord* Latest() const;

	std::vector<ActionRecord> Entries() const;
	std::vector<ActionRecord> Recent(std::size_t limit) const;

private:
	std::size_t max_entries_ = 128;
	std::size_t next_sequence_ = 1;
	std::deque<ActionRecord> entries_;
};

}  // namespace XER::flux::history
