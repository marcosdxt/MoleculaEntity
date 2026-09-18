#pragma once

/// The library's SQLite3 driver.
///
/// This is the only header that includes <sqlite3.h>, and therefore the only one
/// that forces you to link SQLite. Anyone using another database implements
/// `IDatabaseManager` and never includes this file; in CMake the target is
/// `MoleculaEntity::SQLite3`, kept apart from `MoleculaEntity` precisely so the
/// choice is explicit.
///
/// **No database error becomes an exception.** A SQL failure — opening,
/// preparing, binding a parameter, executing — becomes `false` (or an empty
/// result) plus `ok()` and `lastError()`. The reason isn't taste: this library is
/// used inside services that can't unwind the stack on the I/O path — and a
/// library that throws where the caller can't handle it forces the caller to wrap
/// every call in a try/catch, which nobody does until the day of the first
/// `terminate`.
///
/// This is NOT `noexcept`, and the difference matters. These methods build
/// `std::string` and `std::vector`, so they can throw `std::bad_alloc` under
/// memory pressure, like any C++ code that allocates. Anyone needing a strong
/// guarantee has to handle that outside; what's promised here is that **the
/// database is not a source of exceptions** — no failing `sqlite3_*` reaches the
/// caller as a `throw`.
///
/// If you prefer exceptions, `open()` returning null is easy to turn into one;
/// the other direction isn't.

#include "IDatabaseManager.hpp"

#include <sqlite3.h>

#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace MoleculaEntity {

class SQLite3DatabaseManager final : public IDatabaseManager {
public:
    /// What SQLite decides at open time, and that changes the behaviour of the
    /// whole database. The defaults are those of a service that writes to a real
    /// disk, not those of a test.
    struct Options {
        /// WAL: readers don't block the writer, nor the other way around. In a
        /// process with one thread writing and another reading — the normal case
        /// for a service — without this the reads hit SQLITE_BUSY.
        /// `:memory:` databases ignore it (WAL needs a file).
        bool walJournal = true;

        /// How long to wait for a lock before giving up. SQLite's default is
        /// ZERO: any contention returns SQLITE_BUSY immediately, and the symptom
        /// is an intermittent error that never reproduces on the bench.
        std::chrono::milliseconds busyTimeout{5000};

        /// `Full` survives a power cut without corruption; `Normal` with WAL
        /// survives a PROCESS crash, and can lose the last transaction on a power
        /// cut. On equipment that switches off without warning, pick `Full` — the
        /// cost is one extra fsync per commit.
        enum class Synchronous { Off, Normal, Full };
        Synchronous synchronous = Synchronous::Full;

        /// Foreign keys are only enforced when turned on per connection.
        /// SQLite's default is off, for historical compatibility.
        bool foreignKeys = true;

        /// Refuses a double-quoted string where SQL asks for an identifier.
        ///
        /// For old MySQL compatibility, SQLite accepts `"text"` as a literal when
        /// no column by that name exists. The effect is perverse for anyone
        /// quoting identifiers: a wrong column name stops being an error and
        /// becomes a comparison against the string holding the column's name —
        /// the query runs, returns nothing, and nobody finds out. Turned off, the
        /// database answers "no such column", which is the truth.
        ///
        /// Needs SQLite >= 3.29; on earlier versions the option is ignored.
        bool strictIdentifiers = true;
    };

    /// Opens the database. Returns null — without throwing — when it can't, and
    /// writes the reason into `error`, if one is passed.
    ///
    ///     std::string error;
    ///     auto db = SQLite3DatabaseManager::open("/var/lib/app/data.db", {}, &error);
    ///     if (!db) { /* `error` says what happened */ }
    /// With the default options. An overload instead of a defaulted argument
    /// because `Options` is a nested class: inside the body of its enclosing
    /// class it isn't complete yet, and `= {}` on a parameter doesn't compile.
    [[nodiscard]] static std::shared_ptr<SQLite3DatabaseManager>
    open(const std::string& path, std::string* error = nullptr)
    {
        return open(path, Options{}, error);
    }

    [[nodiscard]] static std::shared_ptr<SQLite3DatabaseManager>
    open(const std::string& path, const Options& options, std::string* error = nullptr)
    {
        sqlite3* handle = nullptr;
        const int rc = sqlite3_open(path.c_str(), &handle);
        if (rc != SQLITE_OK) {
            if (error != nullptr) {
                // sqlite3_open returns a handle even when it fails, so the
                // message can be read; closing it is on us.
                *error = handle != nullptr ? sqlite3_errmsg(handle) : "sqlite3_open failed";
            }
            sqlite3_close(handle);
            return nullptr;
        }

        auto db = std::shared_ptr<SQLite3DatabaseManager>(new SQLite3DatabaseManager(handle));
        if (!db->applyOptions(options)) {
            if (error != nullptr) {
                *error = db->lastError();
            }
            return nullptr;
        }
        return db;
    }

    ~SQLite3DatabaseManager() override
    {
        if (db_ != nullptr) {
            sqlite3_close(db_);
        }
    }

    SQLite3DatabaseManager(const SQLite3DatabaseManager&) = delete;
    SQLite3DatabaseManager& operator=(const SQLite3DatabaseManager&) = delete;

    SQLite3DatabaseManager(SQLite3DatabaseManager&& other) noexcept
        : db_(std::exchange(other.db_, nullptr))
        , lastError_(std::move(other.lastError_))
        , ok_(other.ok_) {}

    SQLite3DatabaseManager& operator=(SQLite3DatabaseManager&& other) noexcept
    {
        if (this != &other) {
            if (db_ != nullptr) {
                sqlite3_close(db_);
            }
            db_ = std::exchange(other.db_, nullptr);
            lastError_ = std::move(other.lastError_);
            ok_ = other.ok_;
        }
        return *this;
    }

    bool execute(const std::string& sql) override { return execute(sql, {}); }

    bool execute(const std::string& sql, const std::vector<DbValue>& params) override
    {
        Statement stmt(*this, sql, params);
        if (!stmt) {
            return false;
        }

        const int rc = sqlite3_step(stmt.get());
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            return fail(sql);
        }

        return succeed();
    }

    DbResult query(const std::string& sql) override { return query(sql, {}); }

    DbResult query(const std::string& sql, const std::vector<DbValue>& params) override
    {
        DbResult result;

        Statement stmt(*this, sql, params);
        if (!stmt) {
            return result;
        }

        const int columns = sqlite3_column_count(stmt.get());

        int rc = SQLITE_OK;
        while ((rc = sqlite3_step(stmt.get())) == SQLITE_ROW) {
            DbRow row;
            row.reserve(static_cast<std::size_t>(columns));

            for (int i = 0; i < columns; ++i) {
                row.push_back(readColumn(stmt.get(), i));
            }

            result.push_back(std::move(row));
        }

        if (rc != SQLITE_DONE) {
            fail(sql);
            return DbResult{};   // empty AND ok()==false: see the note on `ok()`
        }

        succeed();
        return result;
    }

    int64_t lastInsertRowId() override { return sqlite3_last_insert_rowid(db_); }

    int affectedRows() override { return sqlite3_changes(db_); }

    bool beginTransaction() override { return execute("BEGIN TRANSACTION"); }
    bool commit() override { return execute("COMMIT"); }
    bool rollback() override { return execute("ROLLBACK"); }

    [[nodiscard]] bool ok() const noexcept override { return ok_; }
    [[nodiscard]] const std::string& lastError() const noexcept override { return lastError_; }

    /// The raw handle, for what the interface doesn't cover (online backup,
    /// user-defined functions, `sqlite3_wal_checkpoint`). It's still ours: don't
    /// close it.
    [[nodiscard]] sqlite3* handle() const noexcept { return db_; }

private:
    explicit SQLite3DatabaseManager(sqlite3* handle) noexcept : db_(handle) {}

    /// RAII over the statement: without this, every error path has to remember
    /// `sqlite3_finalize`, and one forgotten `return` leaks the statement and
    /// holds the database lock until the process dies.
    class Statement {
    public:
        Statement(SQLite3DatabaseManager& owner, const std::string& sql,
                  const std::vector<DbValue>& params)
        {
            if (sqlite3_prepare_v2(owner.db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
                owner.fail(sql);
                return;
            }

            // Binding really does fail: SQLITE_RANGE when the number of values
            // doesn't match the `?` in the SQL, SQLITE_NOMEM under memory
            // pressure. Ignoring the return made the statement run with the
            // parameter missing — to SQLite, a `?` nobody bound is NULL. An
            // UPDATE would become "clear the column", a WHERE would find nothing,
            // and none of it would show up as an error.
            const int expected = sqlite3_bind_parameter_count(stmt_);
            if (expected != static_cast<int>(params.size())) {
                // Own message, not sqlite3_errmsg: no SQLite call failed here, so
                // errmsg would cheerfully answer "not an error" — which is the
                // least useful thing a diagnostic can say.
                owner.failWith("the statement has " + std::to_string(expected) +
                               " parameter(s) and " + std::to_string(params.size()) +
                               " value(s) were passed", sql);
                sqlite3_finalize(stmt_);
                stmt_ = nullptr;
                return;
            }

            if (!bind(params)) {
                owner.fail(sql);
                sqlite3_finalize(stmt_);
                stmt_ = nullptr;
            }
        }

        ~Statement()
        {
            if (stmt_ != nullptr) {
                sqlite3_finalize(stmt_);
            }
        }

        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;

        explicit operator bool() const noexcept { return stmt_ != nullptr; }
        [[nodiscard]] sqlite3_stmt* get() const noexcept { return stmt_; }

    private:
        [[nodiscard]] bool bind(const std::vector<DbValue>& params)
        {
            for (std::size_t i = 0; i < params.size(); ++i) {
                const auto index = static_cast<int>(i + 1);

                const int rc = std::visit([this, index](auto&& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::nullptr_t>) {
                        return sqlite3_bind_null(stmt_, index);
                    } else if constexpr (std::is_same_v<T, int64_t>) {
                        return sqlite3_bind_int64(stmt_, index, value);
                    } else if constexpr (std::is_same_v<T, double>) {
                        return sqlite3_bind_double(stmt_, index, value);
                    } else {
                        // SQLITE_TRANSIENT: SQLite copies. Without it, it keeps
                        // the pointer, and the text may be a temporary that dies
                        // before the step.
                        return sqlite3_bind_text(stmt_, index, value.c_str(),
                                                 static_cast<int>(value.size()), SQLITE_TRANSIENT);
                    }
                }, params[i]);

                if (rc != SQLITE_OK) {
                    return false;
                }
            }

            return true;
        }

        sqlite3_stmt* stmt_ = nullptr;
    };

    [[nodiscard]] static DbValue readColumn(sqlite3_stmt* stmt, int index)
    {
        switch (sqlite3_column_type(stmt, index)) {
            case SQLITE_INTEGER:
                return static_cast<int64_t>(sqlite3_column_int64(stmt, index));
            case SQLITE_FLOAT:
                return sqlite3_column_double(stmt, index);
            case SQLITE_TEXT: {
                const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, index));
                const int size = sqlite3_column_bytes(stmt, index);
                return text != nullptr ? std::string(text, static_cast<std::size_t>(size))
                                       : std::string{};
            }
            case SQLITE_BLOB: {
                // `DbValue` has no binary alternative yet; a BLOB comes back as
                // a byte string, which preserves the content (including `\0`,
                // because the size comes separately) but is indistinguishable
                // from TEXT on the way back. A BLOB column stays usable; faithful
                // typing is work for when `DbValue` gains the alternative.
                const auto* bytes = static_cast<const char*>(sqlite3_column_blob(stmt, index));
                const int size = sqlite3_column_bytes(stmt, index);
                return bytes != nullptr ? std::string(bytes, static_cast<std::size_t>(size))
                                        : std::string{};
            }
            case SQLITE_NULL:
            default:
                return nullptr;
        }
    }

    bool applyOptions(const Options& options)
    {
        if (options.strictIdentifiers) {
#ifdef SQLITE_DBCONFIG_DQS_DML
            sqlite3_db_config(db_, SQLITE_DBCONFIG_DQS_DML, 0, nullptr);
#endif
#ifdef SQLITE_DBCONFIG_DQS_DDL
            sqlite3_db_config(db_, SQLITE_DBCONFIG_DQS_DDL, 0, nullptr);
#endif
        }

        if (options.foreignKeys && !execute("PRAGMA foreign_keys = ON")) {
            return false;
        }

        if (options.busyTimeout.count() > 0) {
            sqlite3_busy_timeout(db_, static_cast<int>(options.busyTimeout.count()));
        }

        // WAL doesn't exist for an in-memory database, and asking for it there
        // returns the mode that ended up in effect rather than an error — so we
        // don't treat it as a failure.
        if (options.walJournal) {
            execute("PRAGMA journal_mode = WAL");
        }

        switch (options.synchronous) {
            case Options::Synchronous::Off:    return execute("PRAGMA synchronous = OFF");
            case Options::Synchronous::Normal: return execute("PRAGMA synchronous = NORMAL");
            case Options::Synchronous::Full:   break;
        }
        return execute("PRAGMA synchronous = FULL");
    }

    bool fail(const std::string& sql)
    {
        return failWith(sqlite3_errmsg(db_), sql);
    }

    bool failWith(const std::string& reason, const std::string& sql)
    {
        lastError_ = reason + " — SQL: " + sql;
        ok_ = false;
        return false;
    }

    bool succeed()
    {
        lastError_.clear();
        ok_ = true;
        return true;
    }

    sqlite3* db_ = nullptr;
    std::string lastError_;
    bool ok_ = true;
};

}  // namespace MoleculaEntity
