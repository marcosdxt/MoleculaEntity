#pragma once

#include <MoleculaEntity/IDatabaseDriver.hpp>
#include <sqlite3.h>
#include <stdexcept>
#include <memory>

namespace MoleculaEntity {

class SQLite3Driver : public IDatabaseDriver {
public:
    explicit SQLite3Driver(const std::string& dbPath = ":memory:") {
        int rc = sqlite3_open(dbPath.c_str(), &db_);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("Failed to open database: " + std::string(sqlite3_errmsg(db_)));
        }
        // Enable foreign keys
        sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    }

    ~SQLite3Driver() override {
        if (db_) {
            sqlite3_close(db_);
        }
    }

    // Non-copyable
    SQLite3Driver(const SQLite3Driver&) = delete;
    SQLite3Driver& operator=(const SQLite3Driver&) = delete;

    // Movable
    SQLite3Driver(SQLite3Driver&& other) noexcept : db_(other.db_) {
        other.db_ = nullptr;
    }

    SQLite3Driver& operator=(SQLite3Driver&& other) noexcept {
        if (this != &other) {
            if (db_) {
                sqlite3_close(db_);
            }
            db_ = other.db_;
            other.db_ = nullptr;
        }
        return *this;
    }

    bool execute(const std::string& sql) override {
        char* errMsg = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            std::string error = errMsg ? errMsg : "Unknown error";
            sqlite3_free(errMsg);
            throw std::runtime_error("SQL execution failed: " + error + " - SQL: " + sql);
        }
        return true;
    }

    bool execute(const std::string& sql, const std::vector<DbValue>& params) override {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("SQL prepare failed: " + std::string(sqlite3_errmsg(db_)) + " - SQL: " + sql);
        }

        bindParams(stmt, params);

        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            throw std::runtime_error("SQL execution failed: " + std::string(sqlite3_errmsg(db_)));
        }

        return true;
    }

    DbResult query(const std::string& sql) override {
        return query(sql, {});
    }

    DbResult query(const std::string& sql, const std::vector<DbValue>& params) override {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            throw std::runtime_error("SQL prepare failed: " + std::string(sqlite3_errmsg(db_)) + " - SQL: " + sql);
        }

        bindParams(stmt, params);

        DbResult result;
        int colCount = sqlite3_column_count(stmt);

        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            DbRow row;
            row.reserve(colCount);

            for (int i = 0; i < colCount; ++i) {
                int colType = sqlite3_column_type(stmt, i);

                switch (colType) {
                    case SQLITE_INTEGER:
                        row.push_back(static_cast<int64_t>(sqlite3_column_int64(stmt, i)));
                        break;
                    case SQLITE_FLOAT:
                        row.push_back(sqlite3_column_double(stmt, i));
                        break;
                    case SQLITE_TEXT: {
                        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                        row.push_back(std::string(text ? text : ""));
                        break;
                    }
                    case SQLITE_BLOB:
                    case SQLITE_NULL:
                    default:
                        row.push_back(nullptr);
                        break;
                }
            }

            result.push_back(std::move(row));
        }

        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            throw std::runtime_error("SQL query failed: " + std::string(sqlite3_errmsg(db_)));
        }

        return result;
    }

    int64_t lastInsertRowId() override {
        return sqlite3_last_insert_rowid(db_);
    }

    int affectedRows() override {
        return sqlite3_changes(db_);
    }

    bool beginTransaction() override {
        return execute("BEGIN TRANSACTION");
    }

    bool commit() override {
        return execute("COMMIT");
    }

    bool rollback() override {
        return execute("ROLLBACK");
    }

    std::string escapeString(const std::string& str) override {
        std::string result;
        result.reserve(str.size() * 2);
        for (char c : str) {
            if (c == '\'') {
                result += "''";
            } else {
                result += c;
            }
        }
        return result;
    }

    bool tableExists(const std::string& tableName) override {
        std::string sql = "SELECT name FROM sqlite_master WHERE type='table' AND name=?";
        auto result = query(sql, {tableName});
        return !result.empty();
    }

    // Get raw SQLite handle (for advanced use)
    sqlite3* getHandle() const noexcept {
        return db_;
    }

private:
    void bindParams(sqlite3_stmt* stmt, const std::vector<DbValue>& params) {
        for (size_t i = 0; i < params.size(); ++i) {
            int paramIdx = static_cast<int>(i + 1);
            const auto& param = params[i];

            std::visit([stmt, paramIdx](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    sqlite3_bind_null(stmt, paramIdx);
                } else if constexpr (std::is_same_v<T, int64_t>) {
                    sqlite3_bind_int64(stmt, paramIdx, arg);
                } else if constexpr (std::is_same_v<T, double>) {
                    sqlite3_bind_double(stmt, paramIdx, arg);
                } else if constexpr (std::is_same_v<T, std::string>) {
                    sqlite3_bind_text(stmt, paramIdx, arg.c_str(), -1, SQLITE_TRANSIENT);
                }
            }, param);
        }
    }

    sqlite3* db_ = nullptr;
};

} // namespace MoleculaEntity
