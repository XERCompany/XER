#include "sql/content/row.h"

#include <utility>

namespace Engine::SQL::Content {

Row::Row(std::vector<std::string> columns, std::vector<Value> values)
	: columns_(std::move(columns)), values_(std::move(values)) {}

std::size_t Row::Size() const noexcept {
	return values_.size();
}

const std::vector<std::string>& Row::Columns() const noexcept {
	return columns_;
}

const std::vector<Value>& Row::Values() const noexcept {
	return values_;
}

const Value* Row::At(std::size_t index) const noexcept {
	return index < values_.size() ? &values_[index] : nullptr;
}

const Value* Row::Find(std::string_view column) const noexcept {
	for (std::size_t index = 0; index < columns_.size() && index < values_.size(); ++index) {
		if (columns_[index] == column) {
			return &values_[index];
		}
	}
	return nullptr;
}

std::optional<std::int64_t> Row::GetInteger(std::string_view column) const {
	const Value* value = Find(column);
	if (value == nullptr) {
		return std::nullopt;
	}
	if (const auto* integer = std::get_if<std::int64_t>(value)) {
		return *integer;
	}
	return std::nullopt;
}

std::optional<double> Row::GetReal(std::string_view column) const {
	const Value* value = Find(column);
	if (value == nullptr) {
		return std::nullopt;
	}
	if (const auto* real = std::get_if<double>(value)) {
		return *real;
	}
	if (const auto* integer = std::get_if<std::int64_t>(value)) {
		return static_cast<double>(*integer);
	}
	return std::nullopt;
}

std::optional<std::string_view> Row::GetText(std::string_view column) const {
	const Value* value = Find(column);
	if (value == nullptr) {
		return std::nullopt;
	}
	if (const auto* text = std::get_if<std::string>(value)) {
		return std::string_view(*text);
	}
	return std::nullopt;
}

const Blob* Row::GetBlob(std::string_view column) const noexcept {
	const Value* value = Find(column);
	return value == nullptr ? nullptr : std::get_if<Blob>(value);
}

}  // namespace Engine::SQL::Content