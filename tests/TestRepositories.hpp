#pragma once

#include <MoleculaEntity/MoleculaEntity.hpp>
#include "TestEntities.hpp"

namespace MoleculaEntity::Test {

class UserRepository : public BaseRepository<UserEntity> {
public:
    explicit UserRepository(DatabaseManagerPtr db) : BaseRepository<UserEntity>(std::move(db)) {}

    [[nodiscard]] std::optional<UserEntity> findByEmail(const std::string& email) {
        QueryBuilder qb;
        qb.where("email", CompareOp::Equals, email);
        auto results = find(qb);
        return results.empty() ? std::nullopt : std::make_optional(results[0]);
    }

    [[nodiscard]] std::vector<UserEntity> findActiveUsers() {
        QueryBuilder qb;
        qb.where("active", CompareOp::Equals, static_cast<int64_t>(1));
        return find(qb);
    }

    [[nodiscard]] std::vector<UserEntity> findByAgeRange(int minAge, int maxAge) {
        QueryBuilder qb;
        qb.whereBetween("age", static_cast<int64_t>(minAge), static_cast<int64_t>(maxAge));
        return find(qb);
    }

protected:
    [[nodiscard]] std::vector<std::string> getInsertColumns(const UserEntity&) const override {
        return {"id", "name", "email", "age", "active"};
    }

    [[nodiscard]] std::vector<DbValue> getInsertValues(const UserEntity& entity) const override {
        std::vector<DbValue> values;
        values.push_back(entity.getId());
        values.push_back(entity.getName());
        values.push_back(entity.getEmail());

        if (entity.getAge().has_value()) {
            values.push_back(static_cast<int64_t>(entity.getAge().value()));
        } else {
            values.push_back(nullptr);
        }

        values.push_back(static_cast<int64_t>(entity.isActive() ? 1 : 0));
        return values;
    }

    [[nodiscard]] std::vector<std::string> getUpdateColumns(const UserEntity&) const override {
        return {"name", "email", "age", "active"};
    }

    [[nodiscard]] std::vector<DbValue> getUpdateValues(const UserEntity& entity) const override {
        std::vector<DbValue> values;
        values.push_back(entity.getName());
        values.push_back(entity.getEmail());

        if (entity.getAge().has_value()) {
            values.push_back(static_cast<int64_t>(entity.getAge().value()));
        } else {
            values.push_back(nullptr);
        }

        values.push_back(static_cast<int64_t>(entity.isActive() ? 1 : 0));
        return values;
    }

    [[nodiscard]] UserEntity mapRowToEntity(const DbRow& row) const override {
        UserEntity entity;

        // row[0] = idx, row[1] = id, row[2] = created_at, row[3] = updated_at
        // row[4] = name, row[5] = email, row[6] = age, row[7] = active
        if (row.size() >= 8) {
            entity.setIdx(getInt64Value(row[0]));
            entity.setId(getStringValue(row[1]));

            auto createdAt = parseTimestamp(row[2]);
            if (createdAt.has_value()) {
                entity.setCreatedAt(createdAt.value());
            }

            auto updatedAt = parseTimestamp(row[3]);
            if (updatedAt.has_value()) {
                entity.setUpdatedAt(updatedAt.value());
            }

            entity.setName(getStringValue(row[4]));
            entity.setEmail(getStringValue(row[5]));

            if (!isNull(row[6])) {
                entity.setAge(static_cast<int>(getInt64Value(row[6])));
            }

            entity.setActive(getInt64Value(row[7]) != 0);
        }

        return entity;
    }
};

class OrderRepository : public BaseRepository<OrderEntity> {
public:
    explicit OrderRepository(DatabaseManagerPtr db) : BaseRepository<OrderEntity>(std::move(db)) {}

    [[nodiscard]] std::vector<OrderEntity> findByUserId(int64_t userId) {
        QueryBuilder qb;
        qb.where("user_id", CompareOp::Equals, userId);
        return find(qb);
    }

    [[nodiscard]] std::vector<OrderEntity> findByStatus(const std::string& status) {
        QueryBuilder qb;
        qb.where("status", CompareOp::Equals, status);
        return find(qb);
    }

    [[nodiscard]] std::vector<OrderEntity> findByAmountGreaterThan(double amount) {
        QueryBuilder qb;
        qb.where("amount", CompareOp::GreaterThan, amount);
        return find(qb);
    }

protected:
    [[nodiscard]] std::vector<std::string> getInsertColumns(const OrderEntity&) const override {
        return {"id", "user_id", "amount", "status"};
    }

    [[nodiscard]] std::vector<DbValue> getInsertValues(const OrderEntity& entity) const override {
        return {
            entity.getId(),
            entity.getUserId(),
            entity.getAmount(),
            entity.getStatus()
        };
    }

    [[nodiscard]] std::vector<std::string> getUpdateColumns(const OrderEntity&) const override {
        return {"user_id", "amount", "status"};
    }

    [[nodiscard]] std::vector<DbValue> getUpdateValues(const OrderEntity& entity) const override {
        return {
            entity.getUserId(),
            entity.getAmount(),
            entity.getStatus()
        };
    }

    [[nodiscard]] OrderEntity mapRowToEntity(const DbRow& row) const override {
        OrderEntity entity;

        // row[0] = idx, row[1] = id, row[2] = created_at, row[3] = updated_at
        // row[4] = user_id, row[5] = amount, row[6] = status
        if (row.size() >= 7) {
            entity.setIdx(getInt64Value(row[0]));
            entity.setId(getStringValue(row[1]));

            auto createdAt = parseTimestamp(row[2]);
            if (createdAt.has_value()) {
                entity.setCreatedAt(createdAt.value());
            }

            auto updatedAt = parseTimestamp(row[3]);
            if (updatedAt.has_value()) {
                entity.setUpdatedAt(updatedAt.value());
            }

            entity.setUserId(getInt64Value(row[4]));
            entity.setAmount(getDoubleValue(row[5]));
            entity.setStatus(getStringValue(row[6]));
        }

        return entity;
    }
};

} // namespace MoleculaEntity::Test
