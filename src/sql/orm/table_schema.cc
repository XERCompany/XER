#include "sql/orm/table_schema.h"

#include <string_view>
#include <unordered_set>

namespace Engine::SQL::ORM {
namespace {

std::string QuoteIdentifier(std::string_view identifier) {
	std::string quoted = "\"";
	for (char character : identifier) {
		if (character == '"') {
			quoted += "\"\"";
		} else {
			quoted += character;
		}
	}
	quoted += '"';
	return quoted;
}

const char* SqlType(ColumnType type) {
	switch (type) {
	case ColumnType::Integer: return "INTEGER";
	case ColumnType::Real: return "REAL";
	case ColumnType::Text: return "TEXT";
	case ColumnType::Blob: return "BLOB";
	}
	return "TEXT";
}

void SetError(std::string* error_out, const char* message) {
	if (error_out != nullptr) {
		*error_out = message;
	}
}

}  // namespace

std::optional<std::string> TableSchema::CreateTableSql(
	std::string* error_out) const {
	if (name.empty() || columns.empty()) {
		SetError(error_out, "A table needs a name and at least one column");
		return std::nullopt;
	}

	std::unordered_set<std::string> column_names;
	std::string sql = "CREATE TABLE IF NOT EXISTS " + QuoteIdentifier(name) + " (";
	for (std::size_t index = 0; index < columns.size(); ++index) {
		const Column& column = columns[index];
		if (column.name.empty() || !column_names.insert(column.name).second) {
			SetError(error_out, "Column names must be non-empty and unique");
			return std::nullopt;
		}
		if (index != 0) {
			sql += ", ";
		}
		sql += QuoteIdentifier(column.name);
		sql += ' ';
		sql += SqlType(column.type);
		if (column.primary_key) {
			sql += " PRIMARY KEY";
		}
		if (column.not_null) {
			sql += " NOT NULL";
		}
	}
	sql += ')';
	return sql;
}

}  // namespace Engine::SQL::ORM