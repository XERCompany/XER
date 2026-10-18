#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sql/content/value.h"

namespace Engine::SQL::Content {

class Row {
public:
	Row() = default;
	Row(std::vector<std::string> columns, std::vector<Value> values);

	std::size_t Size() const noexcept;
	const std::vector<std::string>& Columns() const noexcept;
	const std::vector<Value>& Values() const noexcept;
	const Value* At(std::size_t index) const noexcept;
	const Value* Find(std::string_view column) const noexcept;
	std::optional<std::int64_t> GetInteger(std::string_view column) const;
	std::optional<double> GetReal(std::string_view column) const;
	std::optional<std::string_view> GetText(std::string_view column) const;
	const Blob* GetBlob(std::string_view column) const noexcept;

private:
	std::vector<std::string> columns_;
	std::vector<Value> values_;
};

}  // namespace Engine::SQL::Content