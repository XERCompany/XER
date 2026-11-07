#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace XER::flux::terminal {

enum class InputEventKind {
	kUnknown,
	kText,
	kSubmit,
	kInterrupt,
	kBackspace,
	kArrowUp,
	kArrowDown,
	kArrowLeft,
	kArrowRight,
	kTab,
	kMouseDown,
	kMouseUp,
	kMouseMove,
	kEscape,
};

struct InputEvent {
	InputEventKind kind = InputEventKind::kUnknown;
	std::string text;
	std::string raw;
	std::uint8_t button = 0;
	std::uint16_t x = 0;
	std::uint16_t y = 0;
};

InputEvent ParseInputSequence(std::string_view sequence);
std::vector<InputEvent> ParseInputBuffer(std::string_view buffer);
bool IsCommandSubmission(const InputEvent& event);

}  // namespace XER::flux::terminal
