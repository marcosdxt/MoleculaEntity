#pragma once

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <string>
#include <vector>
#include <optional>

namespace MoleculaEntity::Test {

// User entity (parent)
class UserEntity : public BaseEntity {
public:
    static constexpr const char* tableName = "users";

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

    // Static columns definition
    static std::vector<ColumnMeta> columns() {
        return {
            ColumnMeta::text("name").notNull(),
            ColumnMeta::text("email").notNull().unique(),
            ColumnMeta::integer("age").nullable(),
            ColumnMeta::integer("active").notNull().withDefault("1")
        };
    }

    // Static relations definition
    static std::vector<RelationMeta> relations() {
        return {
            {"orders", "OrderEntity", "user_id"}
        };
    }

    // Convert entity to values for INSERT/UPDATE
    [[nodiscard]] std::vector<DbValue> toValues() const {
        std::vector<DbValue> values;
        values.push_back(name_);
        values.push_back(email_);
        if (age_.has_value()) {
            values.push_back(static_cast<int64_t>(age_.value()));
        } else {
            values.push_back(nullptr);
        }
        values.push_back(static_cast<int64_t>(active_ ? 1 : 0));
        return values;
    }

    // Create entity from database row
    static UserEntity fromRow(const DbRow& row) {
        UserEntity entity;
        entity.loadBaseFields(row);

        size_t offset = BASE_COLUMN_COUNT;
        auto cols = columns();

        // name
        if (offset < row.size()) {
            entity.name_ = getString(row[offset]);
        }
        offset++;

        // email
        if (offset < row.size()) {
            entity.email_ = getString(row[offset]);
        }
        offset++;

        // age (nullable)
        if (offset < row.size() && !std::holds_alternative<std::nullptr_t>(row[offset])) {
            entity.age_ = static_cast<int>(getInt64(row[offset]));
        }
        offset++;

        // active
        if (offset < row.size()) {
            entity.active_ = getInt64(row[offset]) != 0;
        }

        return entity;
    }

    // Getters
    [[nodiscard]] const std::string& getName() const noexcept { return name_; }
    [[nodiscard]] const std::string& getEmail() const noexcept { return email_; }
    [[nodiscard]] std::optional<int> getAge() const noexcept { return age_; }
    [[nodiscard]] bool isActive() const noexcept { return active_; }

    // Setters
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

// Order entity (child of User)
class OrderEntity : public BaseEntity {
public:
    static constexpr const char* tableName = "orders";

    OrderEntity() = default;
    ~OrderEntity() override = default;

    OrderEntity(const OrderEntity& other)
        : BaseEntity(other)
        , userId_(other.userId_)
        , amount_(other.amount_)
        , status_(other.status_) {}

    OrderEntity(OrderEntity&& other) noexcept
        : BaseEntity(std::move(other))
        , userId_(std::move(other.userId_))
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
            userId_ = std::move(other.userId_);
            amount_ = other.amount_;
            status_ = std::move(other.status_);
        }
        return *this;
    }

    // Static columns definition with FK
    static std::vector<ColumnMeta> columns() {
        return {
            ColumnMeta::text("user_id").notNull().foreignKey("users", "id", "CASCADE", "CASCADE"),
            ColumnMeta::real("amount").notNull().withDefault("0.0"),
            ColumnMeta::text("status").notNull().withDefault("'pending'")
        };
    }

    // Static relations definition
    static std::vector<RelationMeta> relations() {
        return {
            {"items", "OrderItemEntity", "order_id"}
        };
    }

    // Convert entity to values for INSERT/UPDATE
    [[nodiscard]] std::vector<DbValue> toValues() const {
        std::vector<DbValue> values;
        values.push_back(userId_);
        values.push_back(amount_);
        values.push_back(status_);
        return values;
    }

    // Create entity from database row
    static OrderEntity fromRow(const DbRow& row) {
        OrderEntity entity;
        entity.loadBaseFields(row);

        size_t offset = BASE_COLUMN_COUNT;

        // user_id
        if (offset < row.size()) {
            entity.userId_ = getString(row[offset]);
        }
        offset++;

        // amount
        if (offset < row.size()) {
            entity.amount_ = getDouble(row[offset]);
        }
        offset++;

        // status
        if (offset < row.size()) {
            entity.status_ = getString(row[offset]);
        }

        return entity;
    }

    // Getters
    [[nodiscard]] const std::string& getUserId() const noexcept { return userId_; }
    [[nodiscard]] double getAmount() const noexcept { return amount_; }
    [[nodiscard]] const std::string& getStatus() const noexcept { return status_; }

    // Setters
    void setUserId(const std::string& userId) { userId_ = userId; }
    void setUserId(std::string&& userId) noexcept { userId_ = std::move(userId); }
    void setAmount(double amount) noexcept { amount_ = amount; }
    void setStatus(const std::string& status) { status_ = status; }
    void setStatus(std::string&& status) noexcept { status_ = std::move(status); }

private:
    std::string userId_;
    double amount_ = 0.0;
    std::string status_ = "pending";
};

// OrderItem entity (child of Order)
class OrderItemEntity : public BaseEntity {
public:
    static constexpr const char* tableName = "order_items";

    OrderItemEntity() = default;
    ~OrderItemEntity() override = default;

    // Static columns definition with FK
    static std::vector<ColumnMeta> columns() {
        return {
            ColumnMeta::text("order_id").notNull().foreignKey("orders", "id", "CASCADE", "CASCADE"),
            ColumnMeta::text("product_name").notNull(),
            ColumnMeta::integer("quantity").notNull().withDefault("1"),
            ColumnMeta::real("unit_price").notNull()
        };
    }

    // Static relations definition
    static std::vector<RelationMeta> relations() {
        return {};
    }

    // Convert entity to values for INSERT/UPDATE
    [[nodiscard]] std::vector<DbValue> toValues() const {
        std::vector<DbValue> values;
        values.push_back(orderId_);
        values.push_back(productName_);
        values.push_back(static_cast<int64_t>(quantity_));
        values.push_back(unitPrice_);
        return values;
    }

    // Create entity from database row
    static OrderItemEntity fromRow(const DbRow& row) {
        OrderItemEntity entity;
        entity.loadBaseFields(row);

        size_t offset = BASE_COLUMN_COUNT;

        // order_id
        if (offset < row.size()) {
            entity.orderId_ = getString(row[offset]);
        }
        offset++;

        // product_name
        if (offset < row.size()) {
            entity.productName_ = getString(row[offset]);
        }
        offset++;

        // quantity
        if (offset < row.size()) {
            entity.quantity_ = static_cast<int>(getInt64(row[offset]));
        }
        offset++;

        // unit_price
        if (offset < row.size()) {
            entity.unitPrice_ = getDouble(row[offset]);
        }

        return entity;
    }

    // Getters
    [[nodiscard]] const std::string& getOrderId() const noexcept { return orderId_; }
    [[nodiscard]] const std::string& getProductName() const noexcept { return productName_; }
    [[nodiscard]] int getQuantity() const noexcept { return quantity_; }
    [[nodiscard]] double getUnitPrice() const noexcept { return unitPrice_; }

    // Setters
    void setOrderId(const std::string& orderId) { orderId_ = orderId; }
    void setProductName(const std::string& name) { productName_ = name; }
    void setQuantity(int quantity) noexcept { quantity_ = quantity; }
    void setUnitPrice(double price) noexcept { unitPrice_ = price; }

private:
    std::string orderId_;
    std::string productName_;
    int quantity_ = 1;
    double unitPrice_ = 0.0;
};

} // namespace MoleculaEntity::Test
