#pragma once

#include <string>
#include <cstdint>
#include <chrono>
#include <optional>
#include <vector>

#include "ColumnMeta.hpp"
#include "IDatabaseDriver.hpp"

namespace MoleculaEntity {

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

    // Getters
    [[nodiscard]] std::optional<int64_t> getIdx() const noexcept { return idx_; }
    [[nodiscard]] const std::string& getId() const noexcept { return id_; }
    [[nodiscard]] std::optional<Timestamp> getCreatedAt() const noexcept { return createdAt_; }
    [[nodiscard]] std::optional<Timestamp> getUpdatedAt() const noexcept { return updatedAt_; }

    // Setters
    void setIdx(int64_t idx) noexcept { idx_ = idx; }
    void setId(const std::string& id) { id_ = id; }
    void setId(std::string&& id) noexcept { id_ = std::move(id); }
    void setCreatedAt(Timestamp ts) noexcept { createdAt_ = ts; }
    void setUpdatedAt(Timestamp ts) noexcept { updatedAt_ = ts; }

    [[nodiscard]] bool isPersisted() const noexcept { return idx_.has_value(); }

    // Load base fields from DbRow
    void loadBaseFields(const DbRow& row, size_t offset = 0) {
        if (row.size() > offset) {
            idx_ = getInt64(row[offset]);
        }
        if (row.size() > offset + 1) {
            id_ = getString(row[offset + 1]);
        }
        if (row.size() > offset + 2 && !isNull(row[offset + 2])) {
            createdAt_ = parseTimestamp(row[offset + 2]);
        }
        if (row.size() > offset + 3 && !isNull(row[offset + 3])) {
            updatedAt_ = parseTimestamp(row[offset + 3]);
        }
    }

    // Parse timestamp from DbValue
    static std::optional<Timestamp> parseTimestamp(const DbValue& val) {
        if (isNull(val)) return std::nullopt;

        if (std::holds_alternative<std::string>(val)) {
            const auto& str = std::get<std::string>(val);
            std::tm tm = {};
            if (strptime(str.c_str(), "%Y-%m-%d %H:%M:%S", &tm) != nullptr) {
                return std::chrono::system_clock::from_time_t(std::mktime(&tm));
            }
        } else if (std::holds_alternative<int64_t>(val)) {
            return Timestamp(std::chrono::seconds(std::get<int64_t>(val)));
        }

        return std::nullopt;
    }

    // Format timestamp to string
    static std::string formatTimestamp(const Timestamp& ts) {
        auto time_t = std::chrono::system_clock::to_time_t(ts);
        std::tm tm = *std::localtime(&time_t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return buf;
    }

    // Base columns definition
    [[nodiscard]] static std::vector<ColumnMeta> baseColumns() {
        return {
            {"idx", ColumnType::Int64, false, std::nullopt, std::nullopt, false},
            {"id", ColumnType::String, false, std::nullopt, std::nullopt, true},
            {"created_at", ColumnType::Timestamp, false, "CURRENT_TIMESTAMP", std::nullopt, false},
            {"updated_at", ColumnType::Timestamp, false, "CURRENT_TIMESTAMP", std::nullopt, false}
        };
    }

protected:
    std::optional<int64_t> idx_;
    std::string id_;
    std::optional<Timestamp> createdAt_;
    std::optional<Timestamp> updatedAt_;
};

// Number of base columns (idx, id, created_at, updated_at)
constexpr size_t BASE_COLUMN_COUNT = 4;

} // namespace MoleculaEntity
