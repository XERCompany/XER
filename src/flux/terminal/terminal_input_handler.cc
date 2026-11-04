#include "flux/terminal/terminal_input_handler.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace flux::terminal {
namespace {

bool IsPrintableByte(unsigned char byte) {
	return byte >= 0x20u && byte != 0x7Fu;
}

std::size_t EscapeSequenceLength(std::string_view buffer, std::size_t index) {
	if (index >= buffer.size() || buffer[index] != '\x1b') {
		return 1;
	}
	if (index + 1 >= buffer.size()) {
		return 1;
	}
	if (buffer[index + 1] == '[') {
		for (std::size_t cursor = index + 2; cursor < buffer.size(); ++cursor) {
			const unsigned char byte = static_cast<unsigned char>(buffer[cursor]);
			if (byte >= 0x40u && byte <= 0x7Eu) {
				return cursor - index + 1;
			}
		}
		return buffer.size() - index;
	}
	if (buffer[index + 1] == ']') {
		for (std::size_t cursor = index + 2; cursor < buffer.size(); ++cursor) {
			if (buffer[cursor] == '\a') {
				return cursor - index + 1;
			}
			if (buffer[cursor] == '\x1b' && cursor + 1 < buffer.size() &&
				buffer[cursor + 1] == '\\') {
				return cursor - index + 2;
			}
		}
		return buffer.size() - index;
	}
	if (buffer[index + 1] == 'P' || buffer[index + 1] == '^' ||
		buffer[index + 1] == '_' || buffer[index + 1] == 'X') {
		for (std::size_t cursor = index + 2; cursor + 1 < buffer.size(); ++cursor) {
			if (buffer[cursor] == '\x1b' && buffer[cursor + 1] == '\\') {
				return cursor - index + 2;
			}
		}
		return buffer.size() - index;
	}
	return 2;
}

bool ParseUnsigned(std::string_view text, std::uint32_t maximum, std::uint32_t& value) {
	if (text.empty()) {
		return false;
	}
	value = 0;
	for (const char ch : text) {
		if (ch < '0' || ch > '9') {
			return false;
		}
		const std::uint32_t digit = static_cast<std::uint32_t>(ch - '0');
		if (value > (maximum - digit) / 10u) {
			return false;
		}
		value = value * 10u + digit;
	}
	return true;
}

bool ParseSgrMouse(std::string_view sequence, InputEvent& event) {
	if (sequence.size() < 7 || sequence.substr(0, 3) != "\x1b[<" ||
		(sequence.back() != 'M' && sequence.back() != 'm')) {
		return false;
	}

	const std::string_view parameters = sequence.substr(3, sequence.size() - 4);
	const std::size_t first_separator = parameters.find(';');
	if (first_separator == std::string_view::npos) {
		return false;
	}
	const std::size_t second_separator = parameters.find(';', first_separator + 1);
	if (second_separator == std::string_view::npos ||
		parameters.find(';', second_separator + 1) != std::string_view::npos) {
		return false;
	}

	std::uint32_t code = 0;
	std::uint32_t x = 0;
	std::uint32_t y = 0;
	if (!ParseUnsigned(parameters.substr(0, first_separator), 255u, code) ||
		!ParseUnsigned(
			parameters.substr(first_separator + 1, second_separator - first_separator - 1),
			std::numeric_limits<std::uint16_t>::max(),
			x
		) ||
		!ParseUnsigned(
			parameters.substr(second_separator + 1),
			std::numeric_limits<std::uint16_t>::max(),
			y
		) ||
		x == 0 || y == 0) {
		return false;
	}
	if (code > 63u) {
		return false;
	}

	const std::uint8_t button = static_cast<std::uint8_t>(code & 3u);
	const bool motion = (code & 32u) != 0;
	if (button == 3u && !motion && sequence.back() == 'M') {
		event.kind = InputEventKind::kMouseUp;
	} else if (sequence.back() == 'm') {
		event.kind = InputEventKind::kMouseUp;
	} else if (motion) {
		event.kind = InputEventKind::kMouseMove;
	} else if (button < 3u) {
		event.kind = InputEventKind::kMouseDown;
	} else {
		return false;
	}

	event.button = button;
	event.x = static_cast<std::uint16_t>(x);
	event.y = static_cast<std::uint16_t>(y);
	return true;
}

}  // namespace

InputEvent ParseInputSequence(std::string_view sequence) {
	InputEvent event;
	event.raw.assign(sequence.data(), sequence.size());
	if (sequence.empty()) {
		return event;
	}
	if (sequence == "\n" || sequence == "\r") {
		event.kind = InputEventKind::kSubmit;
		return event;
	}
	if (sequence == "\x03") {
		event.kind = InputEventKind::kInterrupt;
		return event;
	}
	if (sequence == "\b" || sequence == "\x7f") {
		event.kind = InputEventKind::kBackspace;
		return event;
	}
	if (sequence == "\x1b") {
		event.kind = InputEventKind::kEscape;
		return event;
	}
	if (sequence == "\t") {
		event.kind = InputEventKind::kTab;
		return event;
	}
	if (sequence == "\x1b[A") {
		event.kind = InputEventKind::kArrowUp;
		return event;
	}
	if (sequence == "\x1b[B") {
		event.kind = InputEventKind::kArrowDown;
		return event;
	}
	if (sequence == "\x1b[C") {
		event.kind = InputEventKind::kArrowRight;
		return event;
	}
	if (sequence == "\x1b[D") {
		event.kind = InputEventKind::kArrowLeft;
		return event;
	}
	if (sequence.size() >= 3 && sequence.substr(0, 3) == "\x1b[<") {
		if (!ParseSgrMouse(sequence, event)) {
			event.kind = InputEventKind::kUnknown;
		}
		return event;
	}
	if (sequence.size() >= 2 && sequence.front() == '\x1b') {
		event.kind = InputEventKind::kUnknown;
		return event;
	}
	event.kind = InputEventKind::kText;
	event.text.assign(sequence.data(), sequence.size());
	return event;
}

std::vector<InputEvent> ParseInputBuffer(std::string_view buffer) {
	std::vector<InputEvent> events;
	std::string pending_text;
	auto flush_text = [&]() {
		if (pending_text.empty()) {
			return;
		}
		InputEvent event;
		event.kind = InputEventKind::kText;
		event.text = pending_text;
		event.raw = pending_text;
		events.push_back(std::move(event));
		pending_text.clear();
	};

	for (std::size_t index = 0; index < buffer.size();) {
		const unsigned char byte = static_cast<unsigned char>(buffer[index]);
		if (buffer[index] == '\x1b') {
			flush_text();
			const std::size_t length = EscapeSequenceLength(buffer, index);
			events.push_back(ParseInputSequence(buffer.substr(index, length)));
			index += length;
			continue;
		}
		if (buffer[index] == '\n' || buffer[index] == '\r' || buffer[index] == '\b' || buffer[index] == '\x7f' || buffer[index] == '\x03' || buffer[index] == '\t') {
			flush_text();
			events.push_back(ParseInputSequence(buffer.substr(index, 1)));
			++index;
			continue;
		}
		if (IsPrintableByte(byte)) {
			pending_text.push_back(buffer[index]);
		}
		++index;
	}
	flush_text();
	return events;
}

bool IsCommandSubmission(const InputEvent& event) {
	return event.kind == InputEventKind::kSubmit;
}

}  // namespace flux::terminal
