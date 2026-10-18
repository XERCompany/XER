#pragma once

#include <optional>
#include <string>
#include <vector>

namespace Engine::SQL::ORM {

enum class ColumnType {
	Integer,
	Real,
	Text,
	Blob,
};

struct Column {
	std::string name;
	ColumnType type = ColumnType::Text;
	bool not_null = false;
	bool primary_key = false;
};

class TableSchema {
public:
	std::string name;
	std::vector<Column> columns;

	std::optional<std::string> CreateTableSql(
		std::string* error_out = nullptr) const;
};

}  // namespace Engine::SQL::ORM