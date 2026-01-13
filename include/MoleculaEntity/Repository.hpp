#pragma once

#include <string>
#include <vector>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "IDatabaseDriver.hpp"
#include "BaseEntity.hpp"
#include "QueryBuilder.hpp"
#include "UuidGenerator.hpp"

namespace MoleculaEntity {

template<typename TEntity>
class Repository {
    static_assert(std::is_base_of_v<BaseEntity, TEntity>, "TEntity must derive from BaseEntity");

public:
    explicit Repository(DatabaseDriverPtr db) : db_(std::move(db)) {}

    // Save (insert or update) - lvalue ref
    TEntity save(TEntity& entity) {
        if (entity.isPersisted()) {
            return update(entity);
        }
        return insert(entity);
    }

    // Save (insert or update) - rvalue ref
    TEntity save(TEntity&& entity) {
        TEntity e = std::move(entity);
        return save(e);
    }

    // Insert new entity - lvalue ref
    TEntity insert(TEntity& entity) {
        if (entity.getId().empty()) {
            entity.setId(UuidGenerator::generate());
        }

        auto columns = TEntity::columns();
        std::ostringstream sql;
        sql << "INSERT INTO " << TEntity::tableName << " (id";

        for (const auto& col : columns) {
            sql << ", " << col.name;
        }
        sql << ") VALUES (?";

        for (size_t i = 0; i < columns.size(); ++i) {
            sql << ", ?";
        }
        sql << ")";

        std::vector<DbValue> params;
        params.push_back(entity.getId());

        auto values = entity.toValues();
        for (const auto& val : values) {
            params.push_back(val);
        }

        db_->execute(sql.str(), params);
        entity.setIdx(db_->lastInsertRowId());

        return entity;
    }

    // Update existing entity
    TEntity update(TEntity& entity) {
        if (!entity.isPersisted()) {
            throw std::runtime_error("Cannot update entity that is not persisted");
        }

        auto columns = TEntity::columns();
        std::ostringstream sql;
        sql << "UPDATE " << TEntity::tableName << " SET ";

        bool first = true;
        for (const auto& col : columns) {
            if (!first) sql << ", ";
            sql << col.name << " = ?";
            first = false;
        }
        sql << " WHERE idx = ?";

        auto values = entity.toValues();
        values.push_back(entity.getIdx().value());

        db_->execute(sql.str(), values);

        return entity;
    }

    // Remove entity
    bool remove(const TEntity& entity) {
        if (!entity.isPersisted()) {
            return false;
        }
        return removeByIdx(entity.getIdx().value());
    }

    // Remove by idx
    bool removeByIdx(int64_t idx) {
        std::string sql = "DELETE FROM " + std::string(TEntity::tableName) + " WHERE idx = ?";
        db_->execute(sql, {idx});
        return db_->affectedRows() > 0;
    }

    // Remove by UUID
    bool removeByUuid(const std::string& uuid) {
        std::string sql = "DELETE FROM " + std::string(TEntity::tableName) + " WHERE id = ?";
        db_->execute(sql, {uuid});
        return db_->affectedRows() > 0;
    }

    // Find by idx
    [[nodiscard]] std::optional<TEntity> findByIdx(int64_t idx) {
        std::string sql = "SELECT * FROM " + std::string(TEntity::tableName) + " WHERE idx = ?";
        auto result = db_->query(sql, {idx});

        if (result.empty()) {
            return std::nullopt;
        }

        return TEntity::fromRow(result[0]);
    }

    // Find by UUID
    [[nodiscard]] std::optional<TEntity> findByUuid(const std::string& uuid) {
        std::string sql = "SELECT * FROM " + std::string(TEntity::tableName) + " WHERE id = ?";
        auto result = db_->query(sql, {uuid});

        if (result.empty()) {
            return std::nullopt;
        }

        return TEntity::fromRow(result[0]);
    }

    // Find all
    [[nodiscard]] std::vector<TEntity> findAll() {
        std::string sql = "SELECT * FROM " + std::string(TEntity::tableName);
        auto result = db_->query(sql);

        std::vector<TEntity> entities;
        entities.reserve(result.size());

        for (const auto& row : result) {
            entities.push_back(TEntity::fromRow(row));
        }

        return entities;
    }

    // Find with QueryBuilder
    [[nodiscard]] std::vector<TEntity> find(const QueryBuilder& qb) {
        std::string sql = "SELECT * FROM " + std::string(TEntity::tableName) + qb.buildFullClause();
        auto params = qb.getParams();
        auto result = db_->query(sql, params);

        std::vector<TEntity> entities;
        entities.reserve(result.size());

        for (const auto& row : result) {
            entities.push_back(TEntity::fromRow(row));
        }

        return entities;
    }

    // Find one with QueryBuilder
    [[nodiscard]] std::optional<TEntity> findOne(const QueryBuilder& qb) {
        QueryBuilder limited = qb;
        limited.limit(1);

        auto result = find(limited);
        if (result.empty()) {
            return std::nullopt;
        }

        return result[0];
    }

    // Count all
    [[nodiscard]] int64_t count() {
        std::string sql = "SELECT COUNT(*) FROM " + std::string(TEntity::tableName);
        auto result = db_->query(sql);

        if (!result.empty() && !result[0].empty()) {
            return getInt64(result[0][0]);
        }

        return 0;
    }

    // Count with QueryBuilder
    [[nodiscard]] int64_t count(const QueryBuilder& qb) {
        std::string sql = "SELECT COUNT(*) FROM " + std::string(TEntity::tableName) + qb.buildWhereClause();
        auto params = qb.getParams();
        auto result = db_->query(sql, params);

        if (!result.empty() && !result[0].empty()) {
            return getInt64(result[0][0]);
        }

        return 0;
    }

    // Check if exists
    [[nodiscard]] bool exists(int64_t idx) {
        std::string sql = "SELECT 1 FROM " + std::string(TEntity::tableName) + " WHERE idx = ? LIMIT 1";
        auto result = db_->query(sql, {idx});
        return !result.empty();
    }

    // Check if exists by UUID
    [[nodiscard]] bool existsByUuid(const std::string& uuid) {
        std::string sql = "SELECT 1 FROM " + std::string(TEntity::tableName) + " WHERE id = ? LIMIT 1";
        auto result = db_->query(sql, {uuid});
        return !result.empty();
    }

    // Get database driver
    [[nodiscard]] DatabaseDriverPtr getDriver() const noexcept {
        return db_;
    }

protected:
    DatabaseDriverPtr db_;
};

} // namespace MoleculaEntity
