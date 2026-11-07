#include "sql/storage/sqlite_database.h"

#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace XER::Engine::SQL::Storage {
namespace {

void SetError(std::string* error_out, const char* message) {
	if (error_out != nullptr) {
		*error_out = message;
	}
}

bool BindValue(sqlite3_stmt* statement,
	       int index,
	       const XER::Content::Value& value) {
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

std::int64_t SQLiteDatabase::LastInsertRowId() const noexcept {
	return handle_ != nullptr ? sqlite3_last_insert_rowid(handle_) : 0;
}

std::int64_t SQLiteDatabase::Changes() const noexcept {
	return handle_ != nullptr ? sqlite3_changes64(handle_) : 0;
}

bool SQLiteDatabase::Execute(std::string_view sql,
			     const std::vector<XER::Content::Value>& parameters,
			     std::string* error_out) const {
	Modern::Query query;
	query.text = std::string(sql);
	query.parameters = parameters;
	return Execute(query, error_out);
}

bool SQLiteDatabase::Query(const Modern::Query& query,
			   std::vector<Row>& rows_out,
			   std::string* error_out) const {
	rows_out.clear();
	if (handle_ == nullptr || query.text.empty() ||
	    query.text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		SetError(error_out, "Invalid SQLite database or query");
		return false;
	}

	auto statement = SQLiteStatement::Prepare(*this, query.text, error_out);
	if (statement == nullptr) {
		return false;
	}
	if (!statement->BindAll(query.parameters, error_out)) {
		return false;
	}
	for (;;) {
		if (!statement->Step(error_out)) {
			if (statement->IsDone()) {
				return true;
			}
			return false;
		}
		Row row;
		row.columns.reserve(statement->ColumnCount());
		for (std::size_t i = 0; i < statement->ColumnCount(); ++i) {
			row.columns.push_back(statement->ColumnValue(i));
		}
		rows_out.push_back(std::move(row));
	}
}

bool SQLiteDatabase::Query(std::string_view sql,
			   const std::vector<XER::Content::Value>& parameters,
			   std::vector<Row>& rows_out,
			   std::string* error_out) const {
	Modern::Query query;
	query.text = std::string(sql);
	query.parameters = parameters;
	return Query(query, rows_out, error_out);
}

// ── SQLiteStatement ────────────────────────────────────────────────────────

SQLiteStatement::SQLiteStatement(sqlite3_stmt* statement) noexcept
	: statement_(statement) {}

SQLiteStatement::~SQLiteStatement() {
	if (statement_ != nullptr) {
		sqlite3_finalize(statement_);
	}
}

std::unique_ptr<SQLiteStatement> SQLiteStatement::Prepare(
	const SQLiteDatabase& database,
	std::string_view sql,
	std::string* error_out) {
	if (database.NativeHandle() == nullptr || sql.empty() ||
	    sql.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		SetError(error_out, "Invalid SQLite database or statement");
		return nullptr;
	}
	sqlite3_stmt* raw = nullptr;
	const int result = sqlite3_prepare_v2(
		database.NativeHandle(), sql.data(), static_cast<int>(sql.size()), &raw, nullptr);
	if (result != SQLITE_OK) {
		SetError(error_out, sqlite3_errmsg(database.NativeHandle()));
		return nullptr;
	}
	if (raw == nullptr) {
		SetError(error_out, "SQLite statement is empty");
		return nullptr;
	}
	return std::unique_ptr<SQLiteStatement>(new SQLiteStatement(raw));
}

bool SQLiteStatement::Bind(std::size_t index_one_based,
			   const XER::Content::Value& value,
			   std::string* error_out) {
	if (statement_ == nullptr || index_one_based == 0 ||
	    index_one_based > static_cast<std::size_t>(sqlite3_bind_parameter_count(statement_))) {
		SetError(error_out, "Invalid SQLite bind index");
		return false;
	}
	if (!BindValue(statement_, static_cast<int>(index_one_based), value)) {
		SetError(error_out, "SQLite bind failed");
		return false;
	}
	return true;
}

bool SQLiteStatement::BindAll(const std::vector<XER::Content::Value>& values,
			      std::string* error_out) {
	const int expected = sqlite3_bind_parameter_count(statement_);
	if (expected != static_cast<int>(values.size())) {
		SetError(error_out, "SQLite parameter count does not match statement");
		return false;
	}
	for (std::size_t i = 0; i < values.size(); ++i) {
		if (!Bind(i + 1, values[i], error_out)) {
			return false;
		}
	}
	return true;
}

void SQLiteStatement::Reset() {
	if (statement_ != nullptr) {
		sqlite3_reset(statement_);
		sqlite3_clear_bindings(statement_);
	}
	status_ = SQLITE_OK;
}

bool SQLiteStatement::Step(std::string* error_out) {
	if (statement_ == nullptr) {
		SetError(error_out, "SQLite statement is not prepared");
		return false;
	}
	status_ = sqlite3_step(statement_);
	if (status_ == SQLITE_ROW) {
		return true;
	}
	if (status_ == SQLITE_DONE) {
		return false;
	}
	SetError(error_out, sqlite3_errmsg(sqlite3_db_handle(statement_)));
	return false;
}

bool SQLiteStatement::IsDone() const noexcept {
	return status_ == SQLITE_DONE;
}

std::size_t SQLiteStatement::ColumnCount() const noexcept {
	return statement_ != nullptr
		? static_cast<std::size_t>(sqlite3_column_count(statement_))
		: 0;
}

std::string SQLiteStatement::ColumnName(std::size_t index_zero_based) const {
	if (statement_ == nullptr) {
		return {};
	}
	const char* name = sqlite3_column_name(statement_, static_cast<int>(index_zero_based));
	return name != nullptr ? std::string(name) : std::string{};
}

XER::Content::Value SQLiteStatement::ColumnValue(std::size_t index_zero_based) const {
	if (statement_ == nullptr) {
		return {};
	}
	const int col = static_cast<int>(index_zero_based);
	switch (sqlite3_column_type(statement_, col)) {
	case SQLITE_INTEGER:
		return XER::Content::Value{sqlite3_column_int64(statement_, col)};
	case SQLITE_FLOAT:
		return XER::Content::Value{sqlite3_column_double(statement_, col)};
	case SQLITE_TEXT: {
		const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement_, col));
		const int bytes = sqlite3_column_bytes(statement_, col);
		return XER::Content::Value{std::string(text != nullptr ? text : "",
						   text != nullptr ? static_cast<std::size_t>(bytes) : 0)};
	}
	case SQLITE_BLOB: {
		const auto* blob = sqlite3_column_blob(statement_, col);
		const int bytes = sqlite3_column_bytes(statement_, col);
		if (blob == nullptr || bytes <= 0) {
			return XER::Content::Value{XER::Content::Blob{}};
		}
		const auto* first = static_cast<const std::uint8_t*>(blob);
		return XER::Content::Value{XER::Content::Blob(first, first + bytes)};
	}
	default:
		return {};
	}
}

}  // namespace XER::Engine::SQL::Storage