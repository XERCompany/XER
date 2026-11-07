#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "flux/history/command_history.h"
#include "flux/input/command_parser.h"
#include "flux/input/key_binding_manager.h"

namespace XER::flux::input {

enum class InputDispatchKind {
	kCommand,
	kBinding,
	kInterrupt,
	kHistoryRecall,
};

struct InputDispatch {
	InputDispatchKind kind = InputDispatchKind::kCommand;
	std::string raw;
	std::string buffer;
	ParsedCommand parsed_command;
	std::string binding_command;
};

class InputManager {
public:
	explicit InputManager(std::size_t history_limit = 128);

	XER::flux::history::CommandHistory& history();
	const XER::flux::history::CommandHistory& history() const;
	KeyBindingManager& bindings();
	const KeyBindingManager& bindings() const;

	const std::string& current_buffer() const;
	void ClearCurrentBuffer();

	std::vector<InputDispatch> Feed(std::string_view input);

private:
	XER::flux::history::CommandHistory history_;
	KeyBindingManager bindings_;
	std::string current_buffer_;
};

}  // namespace XER::flux::input
