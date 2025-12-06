#pragma once

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <string>

namespace MoleculaEntity::Test {

class UserEntity : public BaseEntity {
public:
    UserEntity() = default;
    ~UserEntity() override = default;

    UserEntity(const UserEntity& other)
        : BaseEntity(other)
        , name_(other.name_)
        , email_(other.email_)
        , age_(other.age_)
        , active_(other.active_) {}

    UserEntity(UserEntity&& other) noexcept
        : BaseEntity(std::move(other))
        , name_(std::move(other.name_))
        , email_(std::move(other.email_))
        , age_(other.age_)
        , active_(other.active_) {}

    UserEntity& operator=(const UserEntity& other) {
        if (this != &other) {
            BaseEntity::operator=(other);
            name_ = other.name_;
            email_ = other.email_;
            age_ = other.age_;
            active_ = other.active_;
        }
        return *this;
    }

    UserEntity& operator=(UserEntity&& other) noexcept {
        if (this != &other) {
            BaseEntity::operator=(std::move(other));
            name_ = std::move(other.name_);
            email_ = std::move(other.email_);
            age_ = other.age_;
            active_ = other.active_;
        }
        return *this;
    }

    [[nodiscard]] std::string tableName() const override { return "users"; }
    [[nodiscard]] int tableVersion() const override { return 1; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override {
        return {
            {"name", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt},
            {"email", ColumnType::Text, false, false, true, std::nullopt, std::nullopt, std::nullopt},
            {"age", ColumnType::Integer, true, false, false, std::nullopt, std::nullopt, std::nullopt},
            {"active", ColumnType::Boolean, false, false, false, "1", std::nullopt, std::nullopt}
        };
    }

    [[nodiscard]] const std::string& getName() const noexcept { return name_; }
    [[nodiscard]] const std::string& getEmail() const noexcept { return email_; }
    [[nodiscard]] std::optional<int> getAge() const noexcept { return age_; }
    [[nodiscard]] bool isActive() const noexcept { return active_; }

    void setName(const std::string& name) { name_ = name; }
    void setName(std::string&& name) noexcept { name_ = std::move(name); }
    void setEmail(const std::string& email) { email_ = email; }
    void setEmail(std::string&& email) noexcept { email_ = std::move(email); }
    void setAge(std::optional<int> age) noexcept { age_ = age; }
    void setActive(bool active) noexcept { active_ = active; }

private:
    std::string name_;
    std::string email_;
    std::optional<int> age_;
    bool active_ = true;
};

class OrderEntity : public BaseEntity {
public:
    OrderEntity() = default;
    ~OrderEntity() override = default;

    OrderEntity(const OrderEntity& other)
        : BaseEntity(other)
        , userId_(other.userId_)
        , amount_(other.amount_)
        , status_(other.status_) {}

    OrderEntity(OrderEntity&& other) noexcept
        : BaseEntity(std::move(other))
        , userId_(other.userId_)
        , amount_(other.amount_)
        , status_(std::move(other.status_)) {}

    OrderEntity& operator=(const OrderEntity& other) {
        if (this != &other) {
            BaseEntity::operator=(other);
            userId_ = other.userId_;
            amount_ = other.amount_;
            status_ = other.status_;
        }
        return *this;
    }

    OrderEntity& operator=(OrderEntity&& other) noexcept {
        if (this != &other) {
            BaseEntity::operator=(std::move(other));
            userId_ = other.userId_;
            amount_ = other.amount_;
            status_ = std::move(other.status_);
        }
        return *this;
    }

    [[nodiscard]] std::string tableName() const override { return "orders"; }
    [[nodiscard]] int tableVersion() const override { return 1; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override {
        return {
            {"user_id", ColumnType::BigInt, false, false, false, std::nullopt, "users", "idx"},
            {"amount", ColumnType::Real, false, false, false, std::nullopt, std::nullopt, std::nullopt},
            {"status", ColumnType::Text, false, false, false, "'pending'", std::nullopt, std::nullopt}
        };
    }

    [[nodiscard]] int64_t getUserId() const noexcept { return userId_; }
    [[nodiscard]] double getAmount() const noexcept { return amount_; }
    [[nodiscard]] const std::string& getStatus() const noexcept { return status_; }

    void setUserId(int64_t userId) noexcept { userId_ = userId; }
    void setAmount(double amount) noexcept { amount_ = amount; }
    void setStatus(const std::string& status) { status_ = status; }
    void setStatus(std::string&& status) noexcept { status_ = std::move(status); }

private:
    int64_t userId_ = 0;
    double amount_ = 0.0;
    std::string status_ = "pending";
};

} // namespace MoleculaEntity::Test
