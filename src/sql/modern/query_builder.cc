#include "sql/modern/query_builder.h"

#include <array>
#include <sstream>
#include <utility>

namespace Engine::SQL::Modern {
namespace {

void SetError(std::string* error_out, const char* message) {
	if (error_out != nullptr) {
		*error_out = message;
	}
}

constexpr std::array<std::string_view, 6> kAllowedOperations = {
	"=", "!=", "<", "<=", ">", ">="};

}  // namespace

QueryBuilder::QueryBuilder(Kind kind, std::string table)
	: kind_(kind), table_(std::move(table)) {}

QueryBuilder QueryBuilder::Select(std::string table) {
	return QueryBuilder(Kind::Select, std::move(table));
}

QueryBuilder QueryBuilder::InsertInto(std::string table) {
	return QueryBuilder(Kind::Insert, std::move(table));
}

QueryBuilder QueryBuilder::Update(std::string table) {
	return QueryBuilder(Kind::Update, std::move(table));
}

QueryBuilder QueryBuilder::DeleteFrom(std::string table) {
	return QueryBuilder(Kind::Delete, std::move(table));
}

QueryBuilder& QueryBuilder::Columns(std::vector<std::string> columns) {
	columns_ = std::move(columns);
	return *this;
}

QueryBuilder& QueryBuilder::Set(std::string column, Content::Value value) {
	assignments_.emplace_back(std::move(column), std::move(value));
	return *this;
}

QueryBuilder& QueryBuilder::Where(std::string column, std::string operation,
					Content::Value value) {
	predicates_.emplace_back(std::move(column),
					 std::make_pair(std::move(operation), std::move(value)));
	return *this;
}

QueryBuilder& QueryBuilder::OrderBy(std::string column, bool descending) {
	order_ = std::make_pair(std::move(column), descending);
	return *this;
}

QueryBuilder& QueryBuilder::Limit(std::size_t count) {
	limit_ = count;
	return *this;
}

std::optional<std::string> QueryBuilder::QuoteIdentifier(std::string_view identifier) {
	if (identifier.empty() || identifier.find('\0') != std::string_view::npos) {
		return std::nullopt;
	}
	std::string result = "\"";
	for (char character : identifier) {
		if (character == '"') {
			result += "\"\"";
		} else {
			result += character;
		}
	}
	result += '"';
	return result;
}

std::optional<Query> QueryBuilder::Build(std::string* error_out) const {
	const auto quoted_table = QuoteIdentifier(table_);
	if (!quoted_table) {
		SetError(error_out, "Table name is empty or invalid");
		return std::nullopt;
	}

	Query query;
	std::ostringstream sql;
	switch (kind_) {
	case Kind::Select: {
		sql << "SELECT ";
		if (columns_.empty()) {
			sql << '*';
		} else {
			for (std::size_t index = 0; index < columns_.size(); ++index) {
				const auto quoted_column = QuoteIdentifier(columns_[index]);
				if (!quoted_column) {
					SetError(error_out, "Column name is empty or invalid");
					return std::nullopt;
				}
				if (index != 0) {
					sql << ", ";
				}
				sql << *quoted_column;
			}
		}
		sql << " FROM " << *quoted_table;
		break;
	}
	case Kind::Insert:
		if (assignments_.empty()) {
			SetError(error_out, "INSERT requires at least one value");
			return std::nullopt;
		}
		sql << "INSERT INTO " << *quoted_table << " (";
		for (std::size_t index = 0; index < assignments_.size(); ++index) {
			const auto quoted_column = QuoteIdentifier(assignments_[index].first);
			if (!quoted_column) {
				SetError(error_out, "Column name is empty or invalid");
				return std::nullopt;
			}
			if (index != 0) {
				sql << ", ";
			}
			sql << *quoted_column;
			query.parameters.push_back(assignments_[index].second);
		}
		sql << ") VALUES (";
		for (std::size_t index = 0; index < assignments_.size(); ++index) {
			if (index != 0) {
				sql << ", ";
			}
		sql << '?';
		}
		sql << ')';
		break;
	case Kind::Update:
		if (assignments_.empty()) {
			SetError(error_out, "UPDATE requires at least one value");
			return std::nullopt;
		}
		sql << "UPDATE " << *quoted_table << " SET ";
		for (std::size_t index = 0; index < assignments_.size(); ++index) {
			const auto quoted_column = QuoteIdentifier(assignments_[index].first);
			if (!quoted_column) {
				SetError(error_out, "Column name is empty or invalid");
				return std::nullopt;
			}
			if (index != 0) {
				sql << ", ";
			}
			sql << *quoted_column << " = ?";
			query.parameters.push_back(assignments_[index].second);
		}
		break;
	case Kind::Delete:
		sql << "DELETE FROM " << *quoted_table;
		break;
	}

	for (std::size_t index = 0; index < predicates_.size(); ++index) {
		const auto& predicate = predicates_[index];
		const auto quoted_column = QuoteIdentifier(predicate.first);
		bool operation_is_allowed = false;
		for (std::string_view allowed : kAllowedOperations) {
			operation_is_allowed |= predicate.second.first == allowed;
		}
		if (!quoted_column || !operation_is_allowed) {
			SetError(error_out, "Predicate column or operator is invalid");
			return std::nullopt;
		}
		sql << (index == 0 ? " WHERE " : " AND ")
		    << *quoted_column << ' ' << predicate.second.first << " ?";
		query.parameters.push_back(predicate.second.second);
	}

	if (order_) {
		const auto quoted_column = QuoteIdentifier(order_->first);
		if (!quoted_column) {
			SetError(error_out, "ORDER BY column is empty or invalid");
			return std::nullopt;
		}
		sql << " ORDER BY " << *quoted_column
		    << (order_->second ? " DESC" : " ASC");
	}
	if (limit_) {
		sql << " LIMIT " << *limit_;
	}
	query.text = sql.str();
	return query;
}

}  // namespace Engine::SQL::Modern