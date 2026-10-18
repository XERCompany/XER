#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "sql/modern/query.h"

namespace Engine::SQL::Modern {

class QueryBuilder {
public:
	static QueryBuilder Select(std::string table);
	static QueryBuilder InsertInto(std::string table);
	static QueryBuilder Update(std::string table);
	static QueryBuilder DeleteFrom(std::string table);

	QueryBuilder& Columns(std::vector<std::string> columns);
	QueryBuilder& Set(std::string column, Content::Value value);
	QueryBuilder& Where(std::string column, std::string operation,
			    Content::Value value);
	QueryBuilder& OrderBy(std::string column, bool descending = false);
	QueryBuilder& Limit(std::size_t count);

	std::optional<Query> Build(std::string* error_out = nullptr) const;

private:
	enum class Kind {
		Select,
		Insert,
		Update,
		Delete,
	};

	QueryBuilder(Kind kind, std::string table);
	static std::optional<std::string> QuoteIdentifier(std::string_view identifier);

	Kind kind_;
	std::string table_;
	std::vector<std::string> columns_;
	std::vector<std::pair<std::string, Content::Value>> assignments_;
	std::vector<std::pair<std::string, std::pair<std::string, Content::Value>>> predicates_;
	std::optional<std::pair<std::string, bool>> order_;
	std::optional<std::size_t> limit_;
};

}  // namespace Engine::SQL::Modern