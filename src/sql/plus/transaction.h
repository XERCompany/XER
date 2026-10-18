#pragma once

#include <string>

namespace Engine::SQL::Storage {
class SQLiteDatabase;
}

namespace Engine::SQL::Plus {

class Transaction {
public:
	explicit Transaction(Storage::SQLiteDatabase& database,
			     std::string* error_out = nullptr);
	~Transaction();

	Transaction(const Transaction&) = delete;
	Transaction& operator=(const Transaction&) = delete;

	bool IsActive() const noexcept;
	bool Commit(std::string* error_out = nullptr);
	bool Rollback(std::string* error_out = nullptr);

private:
	Storage::SQLiteDatabase* database_;
	bool active_;
};

}  // namespace Engine::SQL::Plus