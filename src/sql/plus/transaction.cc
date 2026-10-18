#include "sql/plus/transaction.h"

#include "sql/modern/query.h"
#include "sql/storage/sqlite_database.h"

namespace Engine::SQL::Plus {

Transaction::Transaction(Storage::SQLiteDatabase& database,
			 std::string* error_out)
	: database_(&database), active_(database_->Execute({"BEGIN IMMEDIATE", {}}, error_out)) {}

Transaction::~Transaction() {
	if (active_) {
		Rollback();
	}
}

bool Transaction::IsActive() const noexcept {
	return active_;
}

bool Transaction::Commit(std::string* error_out) {
	if (!active_ || !database_->Execute({"COMMIT", {}}, error_out)) {
		return false;
	}
	active_ = false;
	return true;
}

bool Transaction::Rollback(std::string* error_out) {
	if (!active_ || !database_->Execute({"ROLLBACK", {}}, error_out)) {
		return false;
	}
	active_ = false;
	return true;
}

}  // namespace Engine::SQL::Plus