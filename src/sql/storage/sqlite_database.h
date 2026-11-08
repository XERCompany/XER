#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

#include "sql/content/value.h"
#include "sql/modern/query.h"

namespace XER::SQL::Storage {

class SQLiteDatabase;

// One result row: column values in SELECT order.
struct Row {
	std::vector<XER::SQL::Content::Value> columns;
};

// Prepared statement with positional parameter binding and row iteration.
// Typical use:
//   auto stmt = SQLiteStatement::Prepare(db, "SELECT * FROM t WHERE id = ?");
//   stmt->Bind(1, XER::SQL::Content::Value{int64_t{42}});
//   while (stmt->Step()) { auto v = stmt->ColumnValue(0); }
class SQLiteStatement {
public:
	static std::unique_ptr<SQLiteStatement> Prepare(
		const SQLiteDatabase& database,
		std::string_view sql,
		std::string* error_out = nullptr);
	~SQLiteStatement();

	SQLiteStatement(const SQLiteStatement&) = delete;
	SQLiteStatement& operator=(const SQLiteStatement&) = delete;

	bool Bind(std::size_t index_one_based, const XER::SQL::Content::Value& value,
		  std::string* error_out = nullptr);
	bool BindAll(const std::vector<XER::SQL::Content::Value>& values,
		     std::string* error_out = nullptr);
	void Reset();

	// Returns true while a row is available; false on SQLITE_DONE.
	bool Step(std::string* error_out = nullptr);
	bool IsDone() const noexcept;

	std::size_t ColumnCount() const noexcept;
	std::string ColumnName(std::size_t index_zero_based) const;
	XER::SQL::Content::Value ColumnValue(std::size_t index_zero_based) const;

	int NativeStatus() const noexcept { return status_; }

private:
	explicit SQLiteStatement(sqlite3_stmt* statement) noexcept;

	sqlite3_stmt* statement_;
	int status_ = SQLITE_OK;
};

class SQLiteDatabase {
public:
	static std::unique_ptr<SQLiteDatabase> Open(
		std::string_view path,
		std::string* error_out = nullptr);
	~SQLiteDatabase();

	SQLiteDatabase(const SQLiteDatabase&) = delete;
	SQLiteDatabase& operator=(const SQLiteDatabase&) = delete;

	bool Execute(const Modern::Query& query,
		     std::string* error_out = nullptr) const;

	// Runs a query and collects every result row. Non-SELECT statements
	// simply produce an empty row set after being stepped to completion.
	bool Query(const Modern::Query& query,
		   std::vector<Row>& rows_out,
		   std::string* error_out = nullptr) const;

	// Convenience: same as Query but takes raw SQL + parameters.
	bool Query(std::string_view sql,
		   const std::vector<XER::SQL::Content::Value>& parameters,
		   std::vector<Row>& rows_out,
		   std::string* error_out = nullptr) const;

	bool Execute(std::string_view sql,
		     const std::vector<XER::SQL::Content::Value>& parameters = {},
		     std::string* error_out = nullptr) const;

	std::int64_t LastInsertRowId() const noexcept;
	std::int64_t Changes() const noexcept;

	sqlite3* NativeHandle() const noexcept;

private:
	explicit SQLiteDatabase(sqlite3* handle) noexcept;

	sqlite3* handle_;
};

}  // namespace XER::SQL::Storage