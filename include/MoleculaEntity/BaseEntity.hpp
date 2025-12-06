#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <optional>
#include <vector>
#include <memory>

namespace MoleculaEntity {

enum class ColumnType {
    Integer,
    BigInt,
    Real,
    Text,
    Blob,
    Boolean,
    Timestamp
};

struct ColumnDefinition {
    std::string name;
    ColumnType type;
    bool nullable = true;
    bool primaryKey = false;
    bool unique = false;
    std::optional<std::string> defaultValue;
    std::optional<std::string> foreignKeyTable;
    std::optional<std::string> foreignKeyColumn;
};

struct Migration {
    int version;
    std::string description;
    std::string upSql;
    std::string downSql;
};

class BaseEntity {
public:
    using Timestamp = std::chrono::system_clock::time_point;

    BaseEntity() = default;
    virtual ~BaseEntity() = default;

    BaseEntity(const BaseEntity& other)
        : idx_(other.idx_)
        , id_(other.id_)
        , createdAt_(other.createdAt_)
        , updatedAt_(other.updatedAt_) {}

    BaseEntity(BaseEntity&& other) noexcept
        : idx_(other.idx_)
        , id_(std::move(other.id_))
        , createdAt_(other.createdAt_)
        , updatedAt_(other.updatedAt_) {}

    BaseEntity& operator=(const BaseEntity& other) {
        if (this != &other) {
            idx_ = other.idx_;
            id_ = other.id_;
            createdAt_ = other.createdAt_;
            updatedAt_ = other.updatedAt_;
        }
        return *this;
    }

    BaseEntity& operator=(BaseEntity&& other) noexcept {
        if (this != &other) {
            idx_ = other.idx_;
            id_ = std::move(other.id_);
            createdAt_ = other.createdAt_;
            updatedAt_ = other.updatedAt_;
        }
        return *this;
    }

    bool operator==(const BaseEntity& other) const {
        return idx_ == other.idx_ && id_ == other.id_;
    }

    bool operator!=(const BaseEntity& other) const {
        return !(*this == other);
    }

    [[nodiscard]] std::optional<int64_t> getIdx() const noexcept { return idx_; }
    [[nodiscard]] const std::string& getId() const noexcept { return id_; }
    [[nodiscard]] std::optional<Timestamp> getCreatedAt() const noexcept { return createdAt_; }
    [[nodiscard]] std::optional<Timestamp> getUpdatedAt() const noexcept { return updatedAt_; }

    void setIdx(int64_t idx) noexcept { idx_ = idx; }
    void setId(const std::string& id) { id_ = id; }
    void setId(std::string&& id) noexcept { id_ = std::move(id); }
    void setCreatedAt(Timestamp ts) noexcept { createdAt_ = ts; }
    void setUpdatedAt(Timestamp ts) noexcept { updatedAt_ = ts; }

    [[nodiscard]] bool isPersisted() const noexcept { return idx_.has_value(); }

    [[nodiscard]] virtual std::string tableName() const = 0;
    [[nodiscard]] virtual int tableVersion() const = 0;
    [[nodiscard]] virtual std::vector<ColumnDefinition> columns() const = 0;
    [[nodiscard]] virtual std::vector<Migration> migrations() const { return {}; }

    [[nodiscard]] static std::vector<ColumnDefinition> baseColumns() {
        return {
            {"idx", ColumnType::BigInt, false, true, false, std::nullopt, std::nullopt, std::nullopt},
            {"id", ColumnType::Text, false, false, true, std::nullopt, std::nullopt, std::nullopt},
            {"created_at", ColumnType::Timestamp, false, false, false, "CURRENT_TIMESTAMP", std::nullopt, std::nullopt},
            {"updated_at", ColumnType::Timestamp, false, false, false, "CURRENT_TIMESTAMP", std::nullopt, std::nullopt}
        };
    }

protected:
    std::optional<int64_t> idx_;
    std::string id_;
    std::optional<Timestamp> createdAt_;
    std::optional<Timestamp> updatedAt_;
};

} // namespace MoleculaEntity
