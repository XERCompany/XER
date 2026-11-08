#pragma once

#include <string>

#include "custom_effect_struct.h"

namespace XER::Audio::FX {

bool SerializeCustomEffectPackage(
	const CustomEffectPackage& package,
	std::string* text_out,
	std::string* error_out = nullptr);

bool DeserializeCustomEffectPackage(
	const std::string& text,
	CustomEffectPackage* package_out,
	std::string* error_out = nullptr);

}  // namespace XER::Audio::FX
