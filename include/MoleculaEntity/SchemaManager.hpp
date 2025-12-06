#pragma once

#include "IDatabaseManager.hpp"
#include "BaseEntity.hpp"
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

    void initialize() {
        createMigrationsTable();
    }

    template<typename TEntity>
    void syncEntity() {
        TEntity entity;
        const std::string tableName = entity.tableName();
        const int targetVersion = entity.tableVersion();

        if (!tableExists(tableName)) {
            createTable(entity);
            recordMigration(tableName, targetVersion, "Initial table creation");
        } else {
            int currentVersion = getTableVersion(tableName);
            if (currentVersion < targetVersion) {
                runMigrations(entity, currentVersion, targetVersion);
            }
        }
    }

    template<typename TEntity>
    void dropTable() {
        TEntity entity;
        std::string sql = "DROP TABLE IF EXISTS " + entity.tableName();
        db_->execute(sql);
    }

    [[nodiscard]] bool tableExists(const std::string& tableName) const {
        std::string sql = "SELECT name FROM sqlite_master WHERE type='table' AND name=?";
        auto result = db_->query(sql, {tableName});
        return !result.empty();
    }

    [[nodiscard]] int getTableVersion(const std::string& tableName) const {
        std::string sql = "SELECT MAX(version) FROM __schema_migrations WHERE table_name = ?";
        auto result = db_->query(sql, {tableName});

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
    void createMigrationsTable() {
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
        db_->execute(sql);
    }

    template<typename TEntity>
    void createTable(const TEntity& entity) {
        std::ostringstream sql;
        sql << "CREATE TABLE IF NOT EXISTS " << entity.tableName() << " (";

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

            sql << col.name << " " << columnTypeToSql(col.type);

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
                fk << "FOREIGN KEY (" << col.name << ") REFERENCES "
                   << col.foreignKeyTable.value() << "(" << col.foreignKeyColumn.value() << ")";
                foreignKeys.push_back(fk.str());
            }
        }

        for (const auto& fk : foreignKeys) {
            sql << ", " << fk;
        }

        sql << ")";

        db_->execute(sql.str());
        createUpdatedAtTrigger(entity.tableName());
        createIdIndex(entity.tableName());
    }

    void createUpdatedAtTrigger(const std::string& tableName) {
        std::ostringstream sql;
        sql << "CREATE TRIGGER IF NOT EXISTS " << tableName << "_updated_at_trigger "
            << "AFTER UPDATE ON " << tableName << " "
            << "FOR EACH ROW BEGIN "
            << "UPDATE " << tableName << " SET updated_at = CURRENT_TIMESTAMP "
            << "WHERE idx = OLD.idx; "
            << "END";
        db_->execute(sql.str());
    }

    void createIdIndex(const std::string& tableName) {
        std::ostringstream sql;
        sql << "CREATE INDEX IF NOT EXISTS idx_" << tableName << "_id ON " << tableName << "(id)";
        db_->execute(sql.str());
    }

    template<typename TEntity>
    void runMigrations(const TEntity& entity, int fromVersion, int toVersion) {
        auto migrations = entity.migrations();

        db_->beginTransaction();
        try {
            for (const auto& migration : migrations) {
                if (migration.version > fromVersion && migration.version <= toVersion) {
                    db_->execute(migration.upSql);
                    recordMigration(entity.tableName(), migration.version, migration.description);
                }
            }
            db_->commit();
        } catch (...) {
            db_->rollback();
            throw;
        }
    }

    void recordMigration(const std::string& tableName, int version, const std::string& description) {
        std::string sql = "INSERT INTO __schema_migrations (table_name, version, description) VALUES (?, ?, ?)";
        db_->execute(sql, {tableName, static_cast<int64_t>(version), description});
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
};

} // namespace MoleculaEntity
