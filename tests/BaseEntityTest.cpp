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
    UserEntity user;
    OrderEntity order;

    EXPECT_EQ(user.tableName(), "users");
    EXPECT_EQ(order.tableName(), "orders");
}

TEST_F(BaseEntityTest, TableVersion) {
    UserEntity user;
    OrderEntity order;

    EXPECT_EQ(user.tableVersion(), 1);
    EXPECT_EQ(order.tableVersion(), 1);
}

TEST_F(BaseEntityTest, Columns) {
    UserEntity user;
    auto columns = user.columns();

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
    OrderEntity order;
    auto columns = order.columns();

    EXPECT_EQ(columns.size(), 3);
    EXPECT_EQ(columns[0].name, "user_id");
    EXPECT_TRUE(columns[0].foreignKeyTable.has_value());
    EXPECT_EQ(columns[0].foreignKeyTable.value(), "users");
    EXPECT_EQ(columns[0].foreignKeyColumn.value(), "idx");
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
