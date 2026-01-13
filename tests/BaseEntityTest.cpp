#include <gtest/gtest.h>
#include "TestEntities.hpp"

using namespace MoleculaEntity;
using namespace MoleculaEntity::Test;

class BaseEntityTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(BaseEntityTest, DefaultConstruction) {
    UserEntity user;

    EXPECT_FALSE(user.isPersisted());
    EXPECT_FALSE(user.getIdx().has_value());
    EXPECT_TRUE(user.getId().empty());
    EXPECT_FALSE(user.getCreatedAt().has_value());
    EXPECT_FALSE(user.getUpdatedAt().has_value());
}

TEST_F(BaseEntityTest, SettersAndGetters) {
    UserEntity user;

    user.setIdx(42);
    user.setId("test-uuid-123");
    user.setName("John Doe");
    user.setEmail("john@example.com");
    user.setAge(30);
    user.setActive(true);

    EXPECT_TRUE(user.isPersisted());
    EXPECT_EQ(user.getIdx().value(), 42);
    EXPECT_EQ(user.getId(), "test-uuid-123");
    EXPECT_EQ(user.getName(), "John Doe");
    EXPECT_EQ(user.getEmail(), "john@example.com");
    EXPECT_EQ(user.getAge().value(), 30);
    EXPECT_TRUE(user.isActive());
}

TEST_F(BaseEntityTest, CopyConstruction) {
    UserEntity original;
    original.setIdx(1);
    original.setId("uuid-1");
    original.setName("Original");
    original.setEmail("original@test.com");

    UserEntity copy(original);

    EXPECT_EQ(copy.getIdx(), original.getIdx());
    EXPECT_EQ(copy.getId(), original.getId());
    EXPECT_EQ(copy.getName(), original.getName());
    EXPECT_EQ(copy.getEmail(), original.getEmail());
}

TEST_F(BaseEntityTest, MoveConstruction) {
    UserEntity original;
    original.setIdx(1);
    original.setId("uuid-1");
    original.setName("Original");
    original.setEmail("original@test.com");

    UserEntity moved(std::move(original));

    EXPECT_EQ(moved.getIdx().value(), 1);
    EXPECT_EQ(moved.getId(), "uuid-1");
    EXPECT_EQ(moved.getName(), "Original");
    EXPECT_EQ(moved.getEmail(), "original@test.com");
}

TEST_F(BaseEntityTest, CopyAssignment) {
    UserEntity original;
    original.setIdx(1);
    original.setId("uuid-1");
    original.setName("Original");

    UserEntity copy;
    copy = original;

    EXPECT_EQ(copy.getIdx(), original.getIdx());
    EXPECT_EQ(copy.getId(), original.getId());
    EXPECT_EQ(copy.getName(), original.getName());
}

TEST_F(BaseEntityTest, MoveAssignment) {
    UserEntity original;
    original.setIdx(1);
    original.setId("uuid-1");
    original.setName("Original");

    UserEntity moved;
    moved = std::move(original);

    EXPECT_EQ(moved.getIdx().value(), 1);
    EXPECT_EQ(moved.getId(), "uuid-1");
    EXPECT_EQ(moved.getName(), "Original");
}

TEST_F(BaseEntityTest, EqualityOperator) {
    UserEntity user1;
    user1.setIdx(1);
    user1.setId("uuid-1");

    UserEntity user2;
    user2.setIdx(1);
    user2.setId("uuid-1");

    UserEntity user3;
    user3.setIdx(2);
    user3.setId("uuid-2");

    EXPECT_TRUE(user1 == user2);
    EXPECT_FALSE(user1 == user3);
}

TEST_F(BaseEntityTest, InequalityOperator) {
    UserEntity user1;
    user1.setIdx(1);
    user1.setId("uuid-1");

    UserEntity user2;
    user2.setIdx(2);
    user2.setId("uuid-2");

    EXPECT_TRUE(user1 != user2);
}

TEST_F(BaseEntityTest, TableName) {
    EXPECT_STREQ(UserEntity::tableName, "users");
    EXPECT_STREQ(OrderEntity::tableName, "orders");
    EXPECT_STREQ(OrderItemEntity::tableName, "order_items");
}

TEST_F(BaseEntityTest, Columns) {
    auto columns = UserEntity::columns();

    EXPECT_EQ(columns.size(), 4);
    EXPECT_EQ(columns[0].name, "name");
    EXPECT_EQ(columns[1].name, "email");
    EXPECT_EQ(columns[2].name, "age");
    EXPECT_EQ(columns[3].name, "active");
}

TEST_F(BaseEntityTest, BaseColumns) {
    auto baseColumns = BaseEntity::baseColumns();

    EXPECT_EQ(baseColumns.size(), 4);
    EXPECT_EQ(baseColumns[0].name, "idx");
    EXPECT_EQ(baseColumns[1].name, "id");
    EXPECT_EQ(baseColumns[2].name, "created_at");
    EXPECT_EQ(baseColumns[3].name, "updated_at");
}

TEST_F(BaseEntityTest, ColumnsWithForeignKey) {
    auto columns = OrderEntity::columns();

    EXPECT_EQ(columns.size(), 3);
    EXPECT_EQ(columns[0].name, "user_id");
    EXPECT_TRUE(columns[0].fk.has_value());
    EXPECT_EQ(columns[0].fk->table, "users");
    EXPECT_EQ(columns[0].fk->column, "id");
    EXPECT_EQ(columns[0].fk->onDelete, "CASCADE");
    EXPECT_EQ(columns[0].fk->onUpdate, "CASCADE");
}

TEST_F(BaseEntityTest, OrderItemForeignKey) {
    auto columns = OrderItemEntity::columns();

    EXPECT_EQ(columns.size(), 4);
    EXPECT_EQ(columns[0].name, "order_id");
    EXPECT_TRUE(columns[0].fk.has_value());
    EXPECT_EQ(columns[0].fk->table, "orders");
}

TEST_F(BaseEntityTest, Relations) {
    auto userRelations = UserEntity::relations();
    EXPECT_EQ(userRelations.size(), 1);
    EXPECT_EQ(userRelations[0].name, "orders");
    EXPECT_EQ(userRelations[0].entity, "OrderEntity");
    EXPECT_EQ(userRelations[0].fkColumn, "user_id");

    auto orderRelations = OrderEntity::relations();
    EXPECT_EQ(orderRelations.size(), 1);
    EXPECT_EQ(orderRelations[0].name, "items");
    EXPECT_EQ(orderRelations[0].entity, "OrderItemEntity");
    EXPECT_EQ(orderRelations[0].fkColumn, "order_id");

    auto itemRelations = OrderItemEntity::relations();
    EXPECT_TRUE(itemRelations.empty());
}

TEST_F(BaseEntityTest, OptionalAge) {
    UserEntity user;

    EXPECT_FALSE(user.getAge().has_value());

    user.setAge(25);
    EXPECT_TRUE(user.getAge().has_value());
    EXPECT_EQ(user.getAge().value(), 25);

    user.setAge(std::nullopt);
    EXPECT_FALSE(user.getAge().has_value());
}

TEST_F(BaseEntityTest, Timestamps) {
    UserEntity user;
    auto now = std::chrono::system_clock::now();

    user.setCreatedAt(now);
    user.setUpdatedAt(now);

    EXPECT_TRUE(user.getCreatedAt().has_value());
    EXPECT_TRUE(user.getUpdatedAt().has_value());
    EXPECT_EQ(user.getCreatedAt().value(), now);
    EXPECT_EQ(user.getUpdatedAt().value(), now);
}

TEST_F(BaseEntityTest, ToValues) {
    UserEntity user;
    user.setName("John");
    user.setEmail("john@test.com");
    user.setAge(30);
    user.setActive(true);

    auto values = user.toValues();
    EXPECT_EQ(values.size(), 4);
    EXPECT_EQ(getString(values[0]), "John");
    EXPECT_EQ(getString(values[1]), "john@test.com");
    EXPECT_EQ(getInt64(values[2]), 30);
    EXPECT_EQ(getInt64(values[3]), 1);
}

TEST_F(BaseEntityTest, ToValuesWithNullAge) {
    UserEntity user;
    user.setName("John");
    user.setEmail("john@test.com");
    user.setAge(std::nullopt);
    user.setActive(false);

    auto values = user.toValues();
    EXPECT_EQ(values.size(), 4);
    EXPECT_TRUE(isNull(values[2]));  // age is null
    EXPECT_EQ(getInt64(values[3]), 0);  // active is false
}

TEST_F(BaseEntityTest, FromRow) {
    // Simulate a database row: idx, id, created_at, updated_at, name, email, age, active
    DbRow row = {
        int64_t(1),
        std::string("test-uuid"),
        std::string("2024-01-01 12:00:00"),
        std::string("2024-01-02 12:00:00"),
        std::string("John Doe"),
        std::string("john@example.com"),
        int64_t(30),
        int64_t(1)
    };

    UserEntity user = UserEntity::fromRow(row);

    EXPECT_EQ(user.getIdx().value(), 1);
    EXPECT_EQ(user.getId(), "test-uuid");
    EXPECT_EQ(user.getName(), "John Doe");
    EXPECT_EQ(user.getEmail(), "john@example.com");
    EXPECT_EQ(user.getAge().value(), 30);
    EXPECT_TRUE(user.isActive());
}

TEST_F(BaseEntityTest, FromRowWithNullAge) {
    DbRow row = {
        int64_t(2),
        std::string("uuid-null-age"),
        std::string("2024-01-01 12:00:00"),
        std::string("2024-01-02 12:00:00"),
        std::string("Jane Doe"),
        std::string("jane@example.com"),
        nullptr,  // null age
        int64_t(0)  // inactive
    };

    UserEntity user = UserEntity::fromRow(row);

    EXPECT_EQ(user.getIdx().value(), 2);
    EXPECT_EQ(user.getName(), "Jane Doe");
    EXPECT_FALSE(user.getAge().has_value());
    EXPECT_FALSE(user.isActive());
}

TEST_F(BaseEntityTest, ColumnMetaBuilder) {
    // Test builder pattern for ColumnMeta
    auto col = ColumnMeta::text("test_col")
        .nullable()
        .withDefault("'default_value'")
        .foreignKey("other_table", "other_id", "SET NULL", "CASCADE");

    EXPECT_EQ(col.name, "test_col");
    EXPECT_EQ(col.type, ColumnType::String);
    EXPECT_TRUE(col.isNullable);
    EXPECT_EQ(col.defaultValue.value(), "'default_value'");
    EXPECT_TRUE(col.fk.has_value());
    EXPECT_EQ(col.fk->table, "other_table");
    EXPECT_EQ(col.fk->column, "other_id");
    EXPECT_EQ(col.fk->onDelete, "SET NULL");
    EXPECT_EQ(col.fk->onUpdate, "CASCADE");
}

TEST_F(BaseEntityTest, ColumnMetaToSql) {
    auto col = ColumnMeta::text("email").notNull().unique();
    std::string sql = col.toSql();

    EXPECT_TRUE(sql.find("email") != std::string::npos);
    EXPECT_TRUE(sql.find("TEXT") != std::string::npos);
    EXPECT_TRUE(sql.find("NOT NULL") != std::string::npos);
    EXPECT_TRUE(sql.find("UNIQUE") != std::string::npos);
}

TEST_F(BaseEntityTest, ForeignKeyToSql) {
    auto col = OrderEntity::columns()[0];  // user_id

    EXPECT_TRUE(col.fk.has_value());
    std::string fkSql = col.fk->toSql("user_id");

    EXPECT_TRUE(fkSql.find("FOREIGN KEY") != std::string::npos);
    EXPECT_TRUE(fkSql.find("user_id") != std::string::npos);
    EXPECT_TRUE(fkSql.find("users") != std::string::npos);
    EXPECT_TRUE(fkSql.find("ON DELETE CASCADE") != std::string::npos);
    EXPECT_TRUE(fkSql.find("ON UPDATE CASCADE") != std::string::npos);
}
