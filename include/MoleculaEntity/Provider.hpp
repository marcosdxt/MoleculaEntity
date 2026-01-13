#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <typeindex>
#include <sstream>
#include <stdexcept>

#include "IDatabaseDriver.hpp"
#include "BaseEntity.hpp"
#include "ColumnMeta.hpp"
#include "Repository.hpp"

namespace MoleculaEntity {

class Provider {
public:
    Provider() = default;
    ~Provider() = default;

    // Set database driver
    void setDriver(DatabaseDriverPtr driver) {
        driver_ = std::move(driver);
        repositories_.clear();
    }

    // Get database driver
    [[nodiscard]] DatabaseDriverPtr getDriver() const noexcept {
        return driver_;
    }

    // Register entity types for synchronization
    template<typename... TEntities>
    void registerEntities() {
        (registerEntity<TEntities>(), ...);
    }

    // Register single entity
    template<typename TEntity>
    void registerEntity() {
        static_assert(std::is_base_of_v<BaseEntity, TEntity>, "TEntity must derive from BaseEntity");

        EntityInfo info;
        info.tableName = TEntity::tableName;
        info.columns = TEntity::columns();
        info.syncFunc = [this]() { syncTable<TEntity>(); };

        entities_[std::type_index(typeid(TEntity))] = info;
        entityOrder_.push_back(std::type_index(typeid(TEntity)));
    }

    // Synchronize all registered entities (create tables, FK constraints, indexes)
    void synchronize() {
        if (!driver_) {
            throw std::runtime_error("No database driver set");
        }

        // Enable foreign keys for SQLite
        driver_->execute("PRAGMA foreign_keys = ON");

        // Create tables in registration order (important for FK dependencies)
        for (const auto& typeIdx : entityOrder_) {
            auto it = entities_.find(typeIdx);
            if (it != entities_.end()) {
                it->second.syncFunc();
            }
        }
    }

    // Get repository for entity type
    template<typename TEntity>
    Repository<TEntity>& getRepository() {
        static_assert(std::is_base_of_v<BaseEntity, TEntity>, "TEntity must derive from BaseEntity");

        if (!driver_) {
            throw std::runtime_error("No database driver set");
        }

        auto typeIdx = std::type_index(typeid(TEntity));
        auto it = repositories_.find(typeIdx);

        if (it == repositories_.end()) {
            auto repo = std::make_shared<Repository<TEntity>>(driver_);
            repositories_[typeIdx] = repo;
            return *repo;
        }

        return *std::static_pointer_cast<Repository<TEntity>>(it->second);
    }

    // Begin transaction
    bool beginTransaction() {
        if (!driver_) return false;
        return driver_->beginTransaction();
    }

    // Commit transaction
    bool commit() {
        if (!driver_) return false;
        return driver_->commit();
    }

    // Rollback transaction
    bool rollback() {
        if (!driver_) return false;
        return driver_->rollback();
    }

private:
    struct EntityInfo {
        std::string tableName;
        std::vector<ColumnMeta> columns;
        std::function<void()> syncFunc;
    };

    template<typename TEntity>
    void syncTable() {
        const std::string tableName = TEntity::tableName;

        if (!driver_->tableExists(tableName)) {
            createTable<TEntity>();
        }

        // Create indexes for FK columns
        auto columns = TEntity::columns();
        for (const auto& col : columns) {
            if (col.fk.has_value()) {
                createFkIndex(tableName, col.name);
            }
        }

        // Create updated_at trigger
        createUpdatedAtTrigger(tableName);
    }

    template<typename TEntity>
    void createTable() {
        const std::string tableName = TEntity::tableName;
        auto columns = TEntity::columns();

        std::ostringstream sql;
        sql << "CREATE TABLE IF NOT EXISTS " << tableName << " (\n";
        sql << "    idx INTEGER PRIMARY KEY AUTOINCREMENT,\n";
        sql << "    id TEXT UNIQUE NOT NULL,\n";
        sql << "    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,\n";
        sql << "    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP";

        // Entity columns
        for (const auto& col : columns) {
            sql << ",\n    " << col.toSql();
        }

        // Foreign key constraints
        for (const auto& col : columns) {
            if (col.fk.has_value()) {
                sql << ",\n    " << col.fk->toSql(col.name);
            }
        }

        sql << "\n)";

        driver_->execute(sql.str());

        // Create index on id
        std::string idxSql = "CREATE INDEX IF NOT EXISTS idx_" + tableName + "_id ON " + tableName + "(id)";
        driver_->execute(idxSql);
    }

    void createFkIndex(const std::string& tableName, const std::string& columnName) {
        std::string indexName = "idx_" + tableName + "_" + columnName;
        std::string sql = "CREATE INDEX IF NOT EXISTS " + indexName + " ON " + tableName + "(" + columnName + ")";
        driver_->execute(sql);
    }

    void createUpdatedAtTrigger(const std::string& tableName) {
        std::string triggerName = tableName + "_updated_at_trigger";

        // Drop existing trigger first
        driver_->execute("DROP TRIGGER IF EXISTS " + triggerName);

        std::ostringstream sql;
        sql << "CREATE TRIGGER " << triggerName << "\n";
        sql << "AFTER UPDATE ON " << tableName << "\n";
        sql << "FOR EACH ROW\n";
        sql << "BEGIN\n";
        sql << "    UPDATE " << tableName << " SET updated_at = CURRENT_TIMESTAMP WHERE idx = NEW.idx;\n";
        sql << "END";

        driver_->execute(sql.str());
    }

    DatabaseDriverPtr driver_;
    std::unordered_map<std::type_index, EntityInfo> entities_;
    std::vector<std::type_index> entityOrder_;  // Preserve registration order for FK dependencies
    std::unordered_map<std::type_index, std::shared_ptr<void>> repositories_;
};

} // namespace MoleculaEntity
