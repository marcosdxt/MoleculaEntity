#pragma once

#include "IDatabaseManager.hpp"
#include "BaseEntity.hpp"
#include "QueryBuilder.hpp"
#include "UuidGenerator.hpp"
#include "Identifier.hpp"
#include "Time.hpp"
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <functional>

namespace MoleculaEntity {

template<typename TEntity>
class BaseRepository {
    static_assert(std::is_base_of_v<BaseEntity, TEntity>, "TEntity must derive from BaseEntity");

public:
    explicit BaseRepository(DatabaseManagerPtr db) : db_(std::move(db)) {}
    virtual ~BaseRepository() = default;

    BaseRepository(const BaseRepository&) = delete;
    BaseRepository& operator=(const BaseRepository&) = delete;
    BaseRepository(BaseRepository&&) = default;
    BaseRepository& operator=(BaseRepository&&) = default;

    TEntity save(TEntity entity) {
        if (entity.getId().empty()) {
            entity.setId(UuidGenerator::generate());
        }

        if (entity.isPersisted()) {
            return update(std::move(entity));
        }

        std::ostringstream sql;
        sql << "INSERT INTO " << quoteIdentifier(entity.tableName()) << " (";

        auto columns = getInsertColumns(entity);
        auto values = getInsertValues(entity);

        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << quoteIdentifier(columns[i]);
        }

        sql << ") VALUES (";

        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << "?";
        }

        sql << ")";

        if (!db_->execute(sql.str(), values)) {
            throw std::runtime_error("Failed to save entity");
        }

        entity.setIdx(db_->lastInsertRowId());

        return findOne(entity.getIdx().value()).value_or(entity);
    }

    TEntity update(TEntity entity) {
        if (!entity.isPersisted()) {
            throw std::runtime_error("Cannot update non-persisted entity");
        }

        std::ostringstream sql;
        sql << "UPDATE " << quoteIdentifier(entity.tableName()) << " SET ";

        auto columns = getUpdateColumns(entity);
        auto values = getUpdateValues(entity);

        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) sql << ", ";
            sql << quoteIdentifier(columns[i]) << " = ?";
        }

        sql << " WHERE idx = ?";
        values.push_back(entity.getIdx().value());

        if (!db_->execute(sql.str(), values)) {
            throw std::runtime_error("Failed to update entity");
        }

        return findOne(entity.getIdx().value()).value_or(entity);
    }

    bool remove(const TEntity& entity) {
        if (!entity.isPersisted()) {
            return false;
        }

        return removeById(entity.getIdx().value());
    }

    bool removeById(int64_t idx) {
        TEntity temp;
        std::string sql = "DELETE FROM " + quoteIdentifier(temp.tableName()) + " WHERE idx = ?";
        return db_->execute(sql, {idx});
    }

    bool removeByUuid(const std::string& id) {
        TEntity temp;
        std::string sql = "DELETE FROM " + quoteIdentifier(temp.tableName()) + " WHERE id = ?";
        return db_->execute(sql, {id});
    }

    [[nodiscard]] std::optional<TEntity> findOne(int64_t idx) {
        QueryBuilder qb;
        qb.where("idx", CompareOp::Equals, idx);
        auto results = find(qb);
        return results.empty() ? std::nullopt : std::make_optional(results[0]);
    }

    [[nodiscard]] std::optional<TEntity> findByUuid(const std::string& id) {
        QueryBuilder qb;
        qb.where("id", CompareOp::Equals, id);
        auto results = find(qb);
        return results.empty() ? std::nullopt : std::make_optional(results[0]);
    }

    [[nodiscard]] std::vector<TEntity> find(const QueryBuilder& query = QueryBuilder{}) {
        TEntity temp;
        std::ostringstream sql;
        sql << "SELECT * FROM " << quoteIdentifier(temp.tableName());
        sql << query.buildFullClause();

        auto result = db_->query(sql.str(), query.getParams());
        return mapResultsToEntities(result);
    }

    [[nodiscard]] std::vector<TEntity> findAll() {
        return find(QueryBuilder{});
    }

    [[nodiscard]] int64_t count(const QueryBuilder& query = QueryBuilder{}) {
        TEntity temp;
        std::ostringstream sql;
        sql << "SELECT COUNT(*) FROM " << quoteIdentifier(temp.tableName());
        sql << query.buildWhereClause();

        auto result = db_->query(sql.str(), query.getParams());
        if (result.empty() || result[0].empty()) {
            return 0;
        }

        return std::get<int64_t>(result[0][0]);
    }

    [[nodiscard]] bool exists(const QueryBuilder& query) {
        return count(query) > 0;
    }

    [[nodiscard]] bool existsById(int64_t idx) {
        QueryBuilder qb;
        qb.where("idx", CompareOp::Equals, idx);
        return exists(qb);
    }

    [[nodiscard]] bool existsByUuid(const std::string& id) {
        QueryBuilder qb;
        qb.where("id", CompareOp::Equals, id);
        return exists(qb);
    }

protected:
    virtual std::vector<std::string> getInsertColumns(const TEntity& entity) const = 0;
    virtual std::vector<DbValue> getInsertValues(const TEntity& entity) const = 0;
    virtual std::vector<std::string> getUpdateColumns(const TEntity& entity) const = 0;
    virtual std::vector<DbValue> getUpdateValues(const TEntity& entity) const = 0;
    virtual TEntity mapRowToEntity(const DbRow& row) const = 0;

    [[nodiscard]] std::vector<TEntity> mapResultsToEntities(const DbResult& result) const {
        std::vector<TEntity> entities;
        entities.reserve(result.size());

        for (const auto& row : result) {
            entities.push_back(mapRowToEntity(row));
        }

        return entities;
    }

    [[nodiscard]] static std::optional<BaseEntity::Timestamp> parseTimestamp(const DbValue& value) {
        if (std::holds_alternative<std::nullptr_t>(value)) {
            return std::nullopt;
        }

        if (std::holds_alternative<std::string>(value)) {
            // UTC, always. The `CURRENT_TIMESTAMP` that fills `created_at` and the
            // `updated_at` trigger both write UTC; an earlier version read them
            // with `std::mktime`, which interprets LOCAL time, and shifted every
            // timestamp by the machine's offset — three hours in Brazil, and zero
            // on a CI running in UTC, which is what kept the defect alive through
            // the test suite.
            return time_utils::parseUtc(std::get<std::string>(value));
        }

        if (std::holds_alternative<int64_t>(value)) {
            return std::chrono::system_clock::from_time_t(std::get<int64_t>(value));
        }

        return std::nullopt;
    }

    [[nodiscard]] static std::string getStringValue(const DbValue& value) {
        if (std::holds_alternative<std::string>(value)) {
            return std::get<std::string>(value);
        }
        return "";
    }

    [[nodiscard]] static int64_t getInt64Value(const DbValue& value) {
        if (std::holds_alternative<int64_t>(value)) {
            return std::get<int64_t>(value);
        }
        return 0;
    }

    [[nodiscard]] static double getDoubleValue(const DbValue& value) {
        if (std::holds_alternative<double>(value)) {
            return std::get<double>(value);
        }
        if (std::holds_alternative<int64_t>(value)) {
            return static_cast<double>(std::get<int64_t>(value));
        }
        return 0.0;
    }

    [[nodiscard]] static bool isNull(const DbValue& value) {
        return std::holds_alternative<std::nullptr_t>(value);
    }

    DatabaseManagerPtr db_;
};

} // namespace MoleculaEntity
