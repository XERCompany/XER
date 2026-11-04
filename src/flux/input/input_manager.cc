#include "flux/input/input_manager.h"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "flux/terminal/terminal_input_handler.h"

namespace flux::input {
namespace {

bool IsBlank(std::string_view text) {
	for (char ch : text) {
		if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
			return false;
		}
	}
	return true;
}

}  // namespace

InputManager::InputManager(std::size_t history_limit)
	: history_(history_limit) {}

flux::history::CommandHistory& InputManager::history() {
	return history_;
}

const flux::history::CommandHistory& InputManager::history() const {
	return history_;
}

KeyBindingManager& InputManager::bindings() {
	return bindings_;
}

const KeyBindingManager& InputManager::bindings() const {
	return bindings_;
}

const std::string& InputManager::current_buffer() const {
	return current_buffer_;
}

void InputManager::ClearCurrentBuffer() {
	current_buffer_.clear();
	history_.ResetCursor();
}

std::vector<InputDispatch> InputManager::Feed(std::string_view input) {
	std::vector<InputDispatch> dispatches;
	const std::vector<flux::terminal::InputEvent> events = flux::terminal::ParseInputBuffer(input);
	for (const flux::terminal::InputEvent& event : events) {
		switch (event.kind) {
		case flux::terminal::InputEventKind::kText:
			current_buffer_ += event.text;
			break;
		case flux::terminal::InputEventKind::kTab:
			current_buffer_.push_back('\t');
			break;
		case flux::terminal::InputEventKind::kBackspace:
			if (!current_buffer_.empty()) {
				current_buffer_.pop_back();
			}
			break;
		case flux::terminal::InputEventKind::kSubmit:
			if (!IsBlank(current_buffer_)) {
				InputDispatch dispatch;
				dispatch.kind = InputDispatchKind::kCommand;
				dispatch.raw = event.raw;
				dispatch.buffer = current_buffer_;
				dispatch.parsed_command = ParseCommandLine(current_buffer_);
				history_.Push(current_buffer_);
				dispatches.push_back(std::move(dispatch));
			}
			ClearCurrentBuffer();
			break;
		case flux::terminal::InputEventKind::kInterrupt: {
			InputDispatch dispatch;
			dispatch.kind = InputDispatchKind::kInterrupt;
			dispatch.raw = event.raw;
			dispatch.buffer = current_buffer_;
			dispatches.push_back(std::move(dispatch));
			break;
		}
		case flux::terminal::InputEventKind::kArrowUp: {
			if (!history_.empty()) {
				current_buffer_ = history_.Previous();
				InputDispatch dispatch;
				dispatch.kind = InputDispatchKind::kHistoryRecall;
				dispatch.raw = event.raw;
				dispatch.buffer = current_buffer_;
				dispatches.push_back(std::move(dispatch));
			}
			break;
		}
		case flux::terminal::InputEventKind::kArrowDown: {
			if (!history_.empty()) {
				current_buffer_ = history_.Next();
				InputDispatch dispatch;
				dispatch.kind = InputDispatchKind::kHistoryRecall;
				dispatch.raw = event.raw;
				dispatch.buffer = current_buffer_;
				dispatches.push_back(std::move(dispatch));
			}
			break;
		}
		default:
			if (bindings_.HasBinding(event.raw)) {
				InputDispatch dispatch;
				dispatch.kind = InputDispatchKind::kBinding;
				dispatch.raw = event.raw;
				dispatch.buffer = current_buffer_;
				dispatch.binding_command = bindings_.CommandFor(event.raw);
				dispatches.push_back(std::move(dispatch));
			}
			break;
		}
	}
	return dispatches;
}

}  // namespace flux::input
