#include "flux/input/key_binding_manager.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace XER::Flux::Input {

bool KeyBindingManager::Register(std::string sequence, std::string command, std::string description, std::string* error_out) {
	if (sequence.empty()) {
		if (error_out != nullptr) {
			*error_out = "Key binding sequence must not be empty";
		}
		return false;
	}
	if (command.empty()) {
		if (error_out != nullptr) {
			*error_out = "Key binding command must not be empty";
		}
		return false;
	}
	if (HasBinding(sequence)) {
		if (error_out != nullptr) {
			*error_out = "Key binding already registered";
		}
		return false;
	}
	bindings_.push_back(KeyBinding{std::move(sequence), std::move(command), std::move(description)});
	return true;
}

bool KeyBindingManager::HasBinding(std::string_view sequence) const {
	for (const KeyBinding& binding : bindings_) {
		if (binding.sequence == sequence) {
			return true;
		}
	}
	return false;
}

std::string KeyBindingManager::CommandFor(std::string_view sequence) const {
	for (const KeyBinding& binding : bindings_) {
		if (binding.sequence == sequence) {
			return binding.command;
		}
	}
	return std::string();
}

std::vector<KeyBinding> KeyBindingManager::Bindings() const {
	return bindings_;
}

}  // namespace XER::Flux::Input
