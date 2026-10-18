#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <sqlite3.h>

#include "sql/modern/query.h"

namespace Engine::SQL::Storage {

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
	sqlite3* NativeHandle() const noexcept;

private:
	explicit SQLiteDatabase(sqlite3* handle) noexcept;

	sqlite3* handle_;
};

}  // namespace Engine::SQL::Storage