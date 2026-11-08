#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace XER::Flux::Input {

struct KeyBinding {
	std::string sequence;
	std::string command;
	std::string description;
};

class KeyBindingManager {
public:
	bool Register(std::string sequence, std::string command, std::string description = {}, std::string* error_out = nullptr);
	bool HasBinding(std::string_view sequence) const;
	std::string CommandFor(std::string_view sequence) const;
	std::vector<KeyBinding> Bindings() const;

private:
	std::vector<KeyBinding> bindings_;
};

}  // namespace XER::Flux::Input
