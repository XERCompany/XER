#include "sql/storage/sqlite_database.h"

#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace Engine::SQL::Storage {
namespace {

void SetError(std::string* error_out, const char* message) {
	if (error_out != nullptr) {
		*error_out = message;
	}
}

bool BindValue(sqlite3_stmt* statement,
	       int index,
	       const Content::Value& value) {
	return std::visit([statement, index](const auto& item) {
		using Item = std::decay_t<decltype(item)>;
		if constexpr (std::is_same_v<Item, std::monostate>) {
			return sqlite3_bind_null(statement, index) == SQLITE_OK;
		} else if constexpr (std::is_same_v<Item, std::int64_t>) {
			return sqlite3_bind_int64(statement, index, item) == SQLITE_OK;
		} else if constexpr (std::is_same_v<Item, double>) {
			return sqlite3_bind_double(statement, index, item) == SQLITE_OK;
		} else if constexpr (std::is_same_v<Item, std::string>) {
			if (item.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
				return false;
			}
			return sqlite3_bind_text(statement, index, item.data(),
						 static_cast<int>(item.size()), SQLITE_TRANSIENT) == SQLITE_OK;
		} else {
			if (item.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
				return false;
			}
			if (item.empty()) {
				return sqlite3_bind_zeroblob(statement, index, 0) == SQLITE_OK;
			}
			return sqlite3_bind_blob(statement, index, item.data(),
						 static_cast<int>(item.size()), SQLITE_TRANSIENT) == SQLITE_OK;
		}
	}, value);
}

}  // namespace

SQLiteDatabase::SQLiteDatabase(sqlite3* handle) noexcept : handle_(handle) {}

std::unique_ptr<SQLiteDatabase> SQLiteDatabase::Open(
	std::string_view path,
	std::string* error_out) {
	sqlite3* handle = nullptr;
	const std::string owned_path(path);
	const int result = sqlite3_open_v2(owned_path.c_str(), &handle,
					   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
						   SQLITE_OPEN_FULLMUTEX,
					   nullptr);
	if (result != SQLITE_OK) {
		SetError(error_out, handle != nullptr ? sqlite3_errmsg(handle) : "SQLite open failed");
		if (handle != nullptr) {
			sqlite3_close_v2(handle);
		}
		return nullptr;
	}
	return std::unique_ptr<SQLiteDatabase>(new SQLiteDatabase(handle));
}

SQLiteDatabase::~SQLiteDatabase() {
	if (handle_ != nullptr) {
		sqlite3_close_v2(handle_);
	}
}

bool SQLiteDatabase::Execute(const Modern::Query& query,
			     std::string* error_out) const {
	if (handle_ == nullptr || query.text.empty() ||
	    query.text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		SetError(error_out, "Invalid SQLite database or query");
		return false;
	}

	sqlite3_stmt* raw_statement = nullptr;
	const int prepare_result = sqlite3_prepare_v2(
		handle_, query.text.data(), static_cast<int>(query.text.size()),
		&raw_statement, nullptr);
	std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(
		raw_statement, sqlite3_finalize);
	if (prepare_result != SQLITE_OK) {
		SetError(error_out, sqlite3_errmsg(handle_));
		return false;
	}
	if (statement == nullptr) {
		SetError(error_out, "SQLite query did not contain a statement");
		return false;
	}
	if (sqlite3_bind_parameter_count(statement.get()) !=
	    static_cast<int>(query.parameters.size())) {
		SetError(error_out, "SQLite parameter count does not match query");
		return false;
	}

	for (std::size_t parameter_index = 0;
	     parameter_index < query.parameters.size(); ++parameter_index) {
		if (!BindValue(statement.get(), static_cast<int>(parameter_index + 1),
			       query.parameters[parameter_index])) {
			SetError(error_out, sqlite3_errmsg(handle_));
			return false;
		}
	}

	int step_result = SQLITE_OK;
	do {
		step_result = sqlite3_step(statement.get());
	} while (step_result == SQLITE_ROW);
	if (step_result != SQLITE_DONE) {
		SetError(error_out, sqlite3_errmsg(handle_));
		return false;
	}
	return true;
}

sqlite3* SQLiteDatabase::NativeHandle() const noexcept {
	return handle_;
}

}  // namespace Engine::SQL::Storage