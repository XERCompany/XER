#include "flux/terminal/terminal_emulator.h"

#include "flux/terminal/terminal_emulator.h"

#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace XER::flux::terminal {
namespace {

constexpr std::size_t kTabWidth = 8;

void EnsureLine(std::vector<std::string>* lines, std::size_t row) {
	while (lines->size() <= row) {
		lines->emplace_back();
	}
}

void WriteCharacter(std::vector<std::string>* lines, std::size_t row, std::size_t column, char ch) {
	EnsureLine(lines, row);
	std::string& line = (*lines)[row];
	if (line.size() < column) {
		line.append(column - line.size(), ' ');
	}
	if (column < line.size()) {
		line[column] = ch;
	} else {
		line.push_back(ch);
	}
}

bool IsPrintable(unsigned char ch) {
	return ch >= 0x20u && ch != 0x7Fu;
}

bool IsStringSequenceInitiator(unsigned char ch) {
	return ch == ']' || ch == 'P' || ch == '_' || ch == '^' || ch == 'X';
}

bool IsSingleEscapeSequence(unsigned char ch) {
	return (ch >= 0x30u && ch <= 0x7Eu) || ch == 0x5Cu;
}

std::size_t SkipStringSequence(std::string_view text,
				       std::size_t index,
				       bool allow_bel_terminator,
				       bool escaped_form) {
	std::size_t cursor = index + (escaped_form ? 2 : 1);
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

bool ParseCsiSequence(std::string_view text,
			      std::size_t index,
			      char* final_char_out,
			      std::vector<int>* params_out,
			      std::size_t* next_index_out) {
	if (final_char_out == nullptr || params_out == nullptr || next_index_out == nullptr) {
		return false;
	}
	*next_index_out = 0;
	const bool escaped_form = static_cast<unsigned char>(text[index]) == 0x1Bu;
	std::size_t cursor = index + (escaped_form ? 2 : 1);
	std::string params_text;
	while (cursor < text.size()) {
		const unsigned char ch = static_cast<unsigned char>(text[cursor]);
		if (ch >= 0x40u && ch <= 0x7Eu) {
			*final_char_out = static_cast<char>(ch);
			*next_index_out = cursor + 1;
			break;
		}
		if ((ch >= 0x30u && ch <= 0x3Fu) || ch == ';') {
			params_text.push_back(static_cast<char>(ch));
		}
		++cursor;
	}
	if (*next_index_out == 0) {
		return false;
	}
	params_out->clear();
	if (params_text.empty()) {
		return true;
	}
	std::size_t start = 0;
	while (start <= params_text.size()) {
		const std::size_t end = params_text.find(';', start);
		const std::string token = params_text.substr(start, end == std::string::npos ? std::string::npos : end - start);
		params_out->push_back(token.empty() ? 0 : std::atoi(token.c_str()));
		if (end == std::string::npos) {
			break;
		}
		start = end + 1;
	}
	return true;
}

int ParamOr(const std::vector<int>& params, std::size_t index, int default_value) {
	if (index >= params.size() || params[index] == 0) {
		return default_value;
	}
	return params[index];
}

}  // namespace

void TerminalEmulator::Feed(std::string_view text) {
	auto ensure_line = [&](std::size_t row) {
		EnsureLine(&lines_, row);
	};
	auto write_character = [&](char ch) {
		WriteCharacter(&lines_, cursor_row_, cursor_column_, ch);
	};
	auto erase_in_line = [&](int mode) {
		ensure_line(cursor_row_);
		std::string& line = lines_[cursor_row_];
		switch (mode) {
		case 1:
			if (line.size() < cursor_column_ + 1) {
				line.resize(cursor_column_ + 1, ' ');
			}
			for (std::size_t position = 0; position <= cursor_column_ && position < line.size(); ++position) {
				line[position] = ' ';
			}
			break;
		case 2:
			line.clear();
			break;
		case 0:
		default:
			if (cursor_column_ < line.size()) {
				line.erase(cursor_column_);
			}
			break;
		}
	};
	auto erase_in_display = [&](int mode) {
		switch (mode) {
		case 1:
			for (std::size_t row = 0; row < cursor_row_ && row < lines_.size(); ++row) {
				lines_[row].clear();
			}
			erase_in_line(1);
			break;
		case 2:
			lines_.clear();
			cursor_row_ = 0;
			cursor_column_ = 0;
			break;
		case 0:
		default:
			erase_in_line(0);
			if (cursor_row_ + 1 < lines_.size()) {
				lines_.erase(lines_.begin() + static_cast<std::ptrdiff_t>(cursor_row_ + 1), lines_.end());
			}
			break;
		}
	};

	std::size_t index = 0;
	while (index < text.size()) {
		const unsigned char byte = static_cast<unsigned char>(text[index]);
		if (byte == 0x1Bu) {
			if (index + 1 >= text.size()) {
				break;
			}
			const unsigned char next = static_cast<unsigned char>(text[index + 1]);
			if (next == '[') {
				char final_char = '\0';
				std::vector<int> params;
				std::size_t next_index = 0;
				if (ParseCsiSequence(text, index, &final_char, &params, &next_index)) {
					switch (final_char) {
					case 'A': {
						const std::size_t amount = static_cast<std::size_t>(ParamOr(params, 0, 1));
						cursor_row_ = cursor_row_ > amount ? cursor_row_ - amount : 0;
						break;
					}
					case 'B':
						cursor_row_ += static_cast<std::size_t>(ParamOr(params, 0, 1));
						ensure_line(cursor_row_);
						break;
					case 'C':
						cursor_column_ += static_cast<std::size_t>(ParamOr(params, 0, 1));
						break;
					case 'D': {
						const std::size_t amount = static_cast<std::size_t>(ParamOr(params, 0, 1));
						cursor_column_ = cursor_column_ > amount ? cursor_column_ - amount : 0;
						break;
					}
					case 'K':
						erase_in_line(ParamOr(params, 0, 0));
						break;
					case 'J':
						erase_in_display(ParamOr(params, 0, 0));
						break;
					default:
						break;
					}
					index = next_index;
					continue;
				}
			}
			if (next == ']') {
				index = SkipStringSequence(text, index, true, true);
				continue;
			}
			if (IsStringSequenceInitiator(next)) {
				index = SkipStringSequence(text, index, false, true);
				continue;
			}
			if (IsSingleEscapeSequence(next)) {
				index += 2;
				continue;
			}
			++index;
			continue;
		}
		if (byte == 0x9Bu) {
			char final_char = '\0';
			std::vector<int> params;
			std::size_t next_index = 0;
			if (ParseCsiSequence(text, index, &final_char, &params, &next_index)) {
				switch (final_char) {
				case 'A': {
					const std::size_t amount = static_cast<std::size_t>(ParamOr(params, 0, 1));
					cursor_row_ = cursor_row_ > amount ? cursor_row_ - amount : 0;
					break;
				}
				case 'B':
					cursor_row_ += static_cast<std::size_t>(ParamOr(params, 0, 1));
					ensure_line(cursor_row_);
					break;
				case 'C':
					cursor_column_ += static_cast<std::size_t>(ParamOr(params, 0, 1));
					break;
				case 'D': {
					const std::size_t amount = static_cast<std::size_t>(ParamOr(params, 0, 1));
					cursor_column_ = cursor_column_ > amount ? cursor_column_ - amount : 0;
					break;
				}
				case 'K':
					erase_in_line(ParamOr(params, 0, 0));
					break;
				case 'J':
					erase_in_display(ParamOr(params, 0, 0));
					break;
				default:
					break;
				}
				index = next_index;
				continue;
			}
		}
		if (byte == 0x9Du) {
			index = SkipStringSequence(text, index, true, false);
			continue;
		}
		if (byte == 0x90u || byte == 0x98u || byte == 0x9Eu || byte == 0x9Fu) {
			index = SkipStringSequence(text, index, false, false);
			continue;
		}
		if (byte == 0x9Cu) {
			++index;
			continue;
		}

		const char ch = text[index];
		switch (ch) {
		case '\n':
			ensure_line(cursor_row_);
			++cursor_row_;
			cursor_column_ = 0;
			ensure_line(cursor_row_);
			break;
		case '\r':
			ensure_line(cursor_row_);
			cursor_column_ = 0;
			break;
		case '\b':
			if (cursor_column_ > 0) {
				--cursor_column_;
			}
			break;
		case '\t': {
			const std::size_t next_tab_stop = ((cursor_column_ / kTabWidth) + 1) * kTabWidth;
			while (cursor_column_ < next_tab_stop) {
				write_character(' ');
				++cursor_column_;
			}
			break;
		}
		default:
			if (!IsPrintable(static_cast<unsigned char>(ch))) {
				break;
			}
			write_character(ch);
			++cursor_column_;
			break;
		}
		++index;
	}
}

void TerminalEmulator::Reset() {
	lines_.clear();
	cursor_row_ = 0;
	cursor_column_ = 0;
}

std::string TerminalEmulator::PlainText() const {
	if (lines_.empty()) {
		return std::string();
	}
	std::string out;
	for (std::size_t index = 0; index < lines_.size(); ++index) {
		if (index > 0) {
			out.push_back('\n');
		}
		out.append(lines_[index]);
	}
	return out;
}

std::string EmulatePlainText(std::string_view text) {
	TerminalEmulator emulator;
	emulator.Feed(text);
	return emulator.PlainText();
}

}  // namespace XER::flux::terminal
