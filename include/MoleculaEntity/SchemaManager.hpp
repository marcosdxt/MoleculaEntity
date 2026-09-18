#pragma once

#include "IDatabaseManager.hpp"
#include "BaseEntity.hpp"
#include "Identifier.hpp"
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace MoleculaEntity {

class SchemaManager {
public:
    explicit SchemaManager(DatabaseManagerPtr db) : db_(std::move(db)) {}
    ~SchemaManager() = default;

    SchemaManager(const SchemaManager&) = delete;
    SchemaManager& operator=(const SchemaManager&) = delete;
    SchemaManager(SchemaManager&&) = default;
    SchemaManager& operator=(SchemaManager&&) = default;

    // Every method that touches the database returns `bool`, and returns it
    // [[nodiscard]].
    //
    // The signature is part of the fix, not decoration: while `syncEntity` was
    // `void`, the caller had NO WAY of knowing a migration failed — and
    // SchemaManager itself ignored the driver's return values. The result was the
    // worst of both worlds: the version was recorded, the transaction committed,
    // and the database went on claiming a schema it didn't have. The next startup
    // saw the new version and never tried again; the column never arrived, in
    // silence.
    //
    // When something fails, `lastError()` says what.
    [[nodiscard]] bool initialize() {
        return createMigrationsTable();
    }

    template<typename TEntity>
    [[nodiscard]] bool syncEntity() {
        TEntity entity;
        const std::string tableName = entity.tableName();
        const int targetVersion = entity.tableVersion();

        const auto exists = tableExists(tableName);
        if (!exists.has_value()) {
            return fail("could not query the schema: " + db_->lastError());
        }

        if (!*exists) {
            // Creating the table and recording the version are one thing. Split
            // apart, a failure in the recording would leave the table existing
            // with no version at all — and the next startup would try to apply
            // the migrations from the beginning, over a table that is already in
            // its final shape.
            if (!db_->beginTransaction()) {
                return fail(db_->lastError());
            }
            if (!createTable(entity) || !recordMigration(tableName, targetVersion, "Initial table creation")) {
                const std::string reason = db_->lastError();
                db_->rollback();
                return fail(reason);
            }
            if (!db_->commit()) {
                return fail(db_->lastError());
            }
            error_.clear();
            return true;
        }

        const auto current = getTableVersion(tableName);
        if (!current.has_value()) {
            return fail("could not read the table version: " + db_->lastError());
        }

        if (*current >= targetVersion) {
            error_.clear();
            return true;   // already up to date
        }

        return runMigrations(entity, *current, targetVersion);
    }

    template<typename TEntity>
    [[nodiscard]] bool dropTable() {
        TEntity entity;
        std::string sql = "DROP TABLE IF EXISTS " + quoteIdentifier(entity.tableName());
        if (!db_->execute(sql)) {
            return fail(db_->lastError());
        }
        error_.clear();
        return true;
    }

    /// The reason for the last failure, empty when there wasn't one.
    [[nodiscard]] const std::string& lastError() const noexcept { return error_; }

    /// Empty when the schema query failed — which is different from "the table
    /// doesn't exist". Without that distinction, an unreachable database would
    /// look like an empty one, and the next step would be to try creating
    /// everything again.
    [[nodiscard]] std::optional<bool> tableExists(const std::string& tableName) const {
        std::string sql = "SELECT name FROM sqlite_master WHERE type='table' AND name=?";
        auto result = db_->query(sql, {tableName});
        if (!db_->ok()) {
            return std::nullopt;
        }
        return !result.empty();
    }

    /// Empty when the query failed. Zero means "table with no migration
    /// recorded" — and confusing the two would make an unreachable database look
    /// like a brand new one.
    [[nodiscard]] std::optional<int> getTableVersion(const std::string& tableName) const {
        std::string sql = "SELECT MAX(version) FROM __schema_migrations WHERE table_name = ?";
        auto result = db_->query(sql, {tableName});

        if (!db_->ok()) {
            return std::nullopt;
        }

        if (result.empty() || result[0].empty()) {
            return 0;
        }

        const auto& val = result[0][0];
        if (std::holds_alternative<std::nullptr_t>(val)) {
            return 0;
        }

        return static_cast<int>(std::get<int64_t>(val));
    }

private:
    bool createMigrationsTable() {
        std::string sql = R"(
            CREATE TABLE IF NOT EXISTS __schema_migrations (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                table_name TEXT NOT NULL,
                version INTEGER NOT NULL,
                description TEXT,
                applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE(table_name, version)
            )
        )";
        if (!db_->execute(sql)) {
            return fail(db_->lastError());
        }
        error_.clear();
        return true;
    }

    template<typename TEntity>
    bool createTable(const TEntity& entity) {
        std::ostringstream sql;
        sql << "CREATE TABLE IF NOT EXISTS " << quoteIdentifier(entity.tableName()) << " (";

        auto baseColumns = BaseEntity::baseColumns();
        auto entityColumns = entity.columns();

        std::vector<ColumnDefinition> allColumns;
        allColumns.reserve(baseColumns.size() + entityColumns.size());
        allColumns.insert(allColumns.end(), baseColumns.begin(), baseColumns.end());
        allColumns.insert(allColumns.end(), entityColumns.begin(), entityColumns.end());

        std::vector<std::string> foreignKeys;

        for (size_t i = 0; i < allColumns.size(); ++i) {
            const auto& col = allColumns[i];

            if (i > 0) sql << ", ";

            sql << quoteIdentifier(col.name) << " " << columnTypeToSql(col.type);

            if (col.primaryKey) {
                sql << " PRIMARY KEY AUTOINCREMENT";
            }

            if (!col.nullable && !col.primaryKey) {
                sql << " NOT NULL";
            }

            if (col.unique && !col.primaryKey) {
                sql << " UNIQUE";
            }

            if (col.defaultValue.has_value()) {
                sql << " DEFAULT " << col.defaultValue.value();
            }

            if (col.foreignKeyTable.has_value() && col.foreignKeyColumn.has_value()) {
                std::ostringstream fk;
                fk << "FOREIGN KEY (" << quoteIdentifier(col.name) << ") REFERENCES "
                   << quoteIdentifier(col.foreignKeyTable.value())
                   << "(" << quoteIdentifier(col.foreignKeyColumn.value()) << ")";
                foreignKeys.push_back(fk.str());
            }
        }

        for (const auto& fk : foreignKeys) {
            sql << ", " << fk;
        }

        sql << ")";

        db_->execute(sql.str());
        createUpdatedAtTrigger(entity.tableName());
        return createIdIndex(entity.tableName());
    }

    bool createUpdatedAtTrigger(const std::string& tableName) {
        std::ostringstream sql;
        sql << "CREATE TRIGGER IF NOT EXISTS " << quoteIdentifier(tableName + "_updated_at_trigger") << " "
            << "AFTER UPDATE ON " << quoteIdentifier(tableName) << " "
            << "FOR EACH ROW BEGIN "
            << "UPDATE " << quoteIdentifier(tableName) << " SET updated_at = CURRENT_TIMESTAMP "
            << "WHERE idx = OLD.idx; "
            << "END";
        return db_->execute(sql.str());
    }

    bool createIdIndex(const std::string& tableName) {
        std::ostringstream sql;
        sql << "CREATE INDEX IF NOT EXISTS " << quoteIdentifier("idx_" + tableName + "_id")
            << " ON " << quoteIdentifier(tableName) << "(\"id\")";
        return db_->execute(sql.str());
    }

    template<typename TEntity>
    bool runMigrations(const TEntity& entity, int fromVersion, int toVersion) {
        auto migrations = entity.migrations();

        if (!db_->beginTransaction()) {
            return fail(db_->lastError());
        }

        int reached = fromVersion;

        // The `try` is still here because of third-party drivers: the interface
        // doesn't forbid throwing, and one that throws halfway through needs the
        // transaction undone too. But it is NOT the mechanism — the mechanism is
        // checking every return value, just below. Relying on the catch alone was
        // the defect: with a driver that reports errors by returning `false`, the
        // catch never fired.
        try {
            for (const auto& migration : migrations) {
                if (migration.version <= fromVersion || migration.version > toVersion) {
                    continue;
                }

                if (!db_->execute(migration.upSql)) {
                    return rollbackWith("migration " + std::to_string(migration.version) + " (" +
                                        migration.description + ") failed: " + db_->lastError());
                }

                if (!recordMigration(entity.tableName(), migration.version, migration.description)) {
                    return rollbackWith("could not record migration " +
                                        std::to_string(migration.version) + ": " + db_->lastError());
                }

                reached = migration.version;
            }
        } catch (...) {
            db_->rollback();
            error_ = "the driver threw during the migration";
            throw;
        }

        // A declared version with no migration that reaches it. Without this
        // check, the case passed as success and did nothing: the table stayed in
        // the old shape, the recorded version never moved, and every startup
        // repeated the doing-nothing — quietly.
        if (reached < toVersion) {
            return rollbackWith("the entity declares version " + std::to_string(toVersion) +
                                ", but there is no migration leaving version " +
                                std::to_string(reached) + ". Declare it in migrations().");
        }

        if (!db_->commit()) {
            return fail(db_->lastError());
        }

        error_.clear();
        return true;
    }

    bool rollbackWith(std::string reason) {
        db_->rollback();
        error_ = std::move(reason);
        return false;
    }

    bool fail(std::string reason) {
        error_ = std::move(reason);
        return false;
    }

    bool recordMigration(const std::string& tableName, int version, const std::string& description) {
        std::string sql = "INSERT INTO __schema_migrations (table_name, version, description) VALUES (?, ?, ?)";
        return db_->execute(sql, {tableName, static_cast<int64_t>(version), description});
    }

    [[nodiscard]] static std::string columnTypeToSql(ColumnType type) {
        switch (type) {
            case ColumnType::Integer: return "INTEGER";
            case ColumnType::BigInt: return "INTEGER";
            case ColumnType::Real: return "REAL";
            case ColumnType::Text: return "TEXT";
            case ColumnType::Blob: return "BLOB";
            case ColumnType::Boolean: return "INTEGER";
            case ColumnType::Timestamp: return "TIMESTAMP";
            default: return "TEXT";
        }
    }

    DatabaseManagerPtr db_;
    std::string error_;
};

} // namespace MoleculaEntity
