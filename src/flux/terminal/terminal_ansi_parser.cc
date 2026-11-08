#include "flux/terminal/terminal_ansi_parser.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace XER::Flux::Terminal {
namespace {

std::size_t SkipCsiSequence(std::string_view text, std::size_t index) {
	std::size_t cursor = index + 1;
	if (static_cast<unsigned char>(text[index]) == 0x1Bu) {
		++cursor;
	}
	while (cursor < text.size()) {
		const unsigned char ch = static_cast<unsigned char>(text[cursor]);
		if (ch >= 0x40u && ch <= 0x7Eu) {
			return cursor + 1;
		}
		++cursor;
	}
	return text.size();
}

std::size_t SkipStringSequence(std::string_view text,
				       std::size_t index,
				       bool allow_bel_terminator) {
	std::size_t cursor = index + 1;
	if (static_cast<unsigned char>(text[index]) == 0x1Bu) {
		++cursor;
	}
	while (cursor < text.size()) {
		const unsigned char ch = static_cast<unsigned char>(text[cursor]);
		if (allow_bel_terminator && ch == 0x07u) {
			return cursor + 1;
		}
		if (ch == 0x9Cu) {
			return cursor + 1;
		}
		if (ch == 0x1Bu && cursor + 1 < text.size() && text[cursor + 1] == '\\') {
			return cursor + 2;
		}
		++cursor;
	}
	return text.size();
}

bool IsStringSequenceInitiator(unsigned char ch) {
	return ch == ']' || ch == 'P' || ch == '_' || ch == '^' || ch == 'X';
}

bool IsSingleEscapeSequence(unsigned char ch) {
	return (ch >= 0x30u && ch <= 0x7Eu) || ch == 0x5Cu;
}

}  // namespace

std::string StripAnsi(std::string_view text) {
	std::string out;
	out.reserve(text.size());
	std::size_t index = 0;
	while (index < text.size()) {
		const unsigned char ch = static_cast<unsigned char>(text[index]);
		if (ch == 0x1Bu) {
			if (index + 1 >= text.size()) {
				break;
			}
			const unsigned char next = static_cast<unsigned char>(text[index + 1]);
			if (next == '[') {
				index = SkipCsiSequence(text, index);
				continue;
			}
			if (next == ']') {
				index = SkipStringSequence(text, index, true);
				continue;
			}
			if (IsStringSequenceInitiator(next)) {
				index = SkipStringSequence(text, index, false);
				continue;
			}
			if (IsSingleEscapeSequence(next)) {
				index += 2;
				continue;
			}
			++index;
			continue;
		}
		if (ch == 0x9Bu) {
			index = SkipCsiSequence(text, index);
			continue;
		}
		if (ch == 0x9Du) {
			index = SkipStringSequence(text, index, true);
			continue;
		}
		if (ch == 0x90u || ch == 0x98u || ch == 0x9Eu || ch == 0x9Fu) {
			index = SkipStringSequence(text, index, false);
			continue;
		}
		if (ch == 0x9Cu) {
			++index;
			continue;
		}
		out.push_back(text[index]);
		++index;
	}
	return out;
}

}  // namespace XER::Flux::Terminal
