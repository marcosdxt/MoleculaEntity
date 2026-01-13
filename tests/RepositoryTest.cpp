#include <gtest/gtest.h>
#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/drivers/SQLite3Driver.hpp>
#include "TestEntities.hpp"

using namespace MoleculaEntity;
using namespace MoleculaEntity::Test;

class RepositoryTest : public ::testing::Test {
protected:
    Provider provider;

    void SetUp() override {
        auto driver = std::make_shared<SQLite3Driver>(":memory:");
        provider.setDriver(driver);
        provider.registerEntities<UserEntity, OrderEntity, OrderItemEntity>();
        provider.synchronize();
    }

    void TearDown() override {
    }

    UserEntity createTestUser(const std::string& name, const std::string& email, int age = 25, bool active = true) {
        UserEntity user;
        user.setName(name);
        user.setEmail(email);
        if (age >= 0) {
            user.setAge(age);
        }
        user.setActive(active);
        return user;
    }
};

// Save Tests

TEST_F(RepositoryTest, SaveNewEntity) {
    auto user = createTestUser("John Doe", "john@example.com");
    auto& userRepo = provider.getRepository<UserEntity>();

    auto savedUser = userRepo.save(user);

    EXPECT_TRUE(savedUser.isPersisted());
    EXPECT_TRUE(savedUser.getIdx().has_value());
    EXPECT_FALSE(savedUser.getId().empty());
    EXPECT_EQ(savedUser.getName(), "John Doe");
    EXPECT_EQ(savedUser.getEmail(), "john@example.com");
}

TEST_F(RepositoryTest, SaveGeneratesUuid) {
    auto user = createTestUser("Test User", "test@example.com");
    auto& userRepo = provider.getRepository<UserEntity>();

    EXPECT_TRUE(user.getId().empty());

    auto savedUser = userRepo.save(user);

    EXPECT_FALSE(savedUser.getId().empty());
    EXPECT_EQ(savedUser.getId().length(), 36); // UUID format
}

TEST_F(RepositoryTest, SaveWithExistingUuid) {
    auto user = createTestUser("Test User", "test@example.com");
    user.setId("custom-uuid-12345678901234567890");
    auto& userRepo = provider.getRepository<UserEntity>();

    auto savedUser = userRepo.save(user);

    EXPECT_EQ(savedUser.getId(), "custom-uuid-12345678901234567890");
}

TEST_F(RepositoryTest, SaveMultipleEntities) {
    auto& userRepo = provider.getRepository<UserEntity>();

    auto user1 = userRepo.save(createTestUser("User 1", "user1@example.com"));
    auto user2 = userRepo.save(createTestUser("User 2", "user2@example.com"));
    auto user3 = userRepo.save(createTestUser("User 3", "user3@example.com"));

    EXPECT_EQ(user1.getIdx().value(), 1);
    EXPECT_EQ(user2.getIdx().value(), 2);
    EXPECT_EQ(user3.getIdx().value(), 3);
}

TEST_F(RepositoryTest, SaveWithNullableField) {
    UserEntity user;
    user.setName("No Age User");
    user.setEmail("noage@example.com");
    // age is not set
    auto& userRepo = provider.getRepository<UserEntity>();

    auto savedUser = userRepo.save(user);

    EXPECT_FALSE(savedUser.getAge().has_value());
}

// Update Tests

TEST_F(RepositoryTest, UpdateExistingEntity) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto user = userRepo.save(createTestUser("Original Name", "original@example.com"));

    user.setName("Updated Name");
    user.setEmail("updated@example.com");

    auto updatedUser = userRepo.save(user);

    EXPECT_EQ(updatedUser.getIdx(), user.getIdx());
    EXPECT_EQ(updatedUser.getName(), "Updated Name");
    EXPECT_EQ(updatedUser.getEmail(), "updated@example.com");
}

TEST_F(RepositoryTest, UpdateThrowsForNonPersisted) {
    auto user = createTestUser("Test", "test@example.com");
    auto& userRepo = provider.getRepository<UserEntity>();

    EXPECT_THROW(userRepo.update(user), std::runtime_error);
}

// FindOne Tests

TEST_F(RepositoryTest, FindOneByIdx) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto savedUser = userRepo.save(createTestUser("Find Me", "findme@example.com"));

    auto foundUser = userRepo.findByIdx(savedUser.getIdx().value());

    EXPECT_TRUE(foundUser.has_value());
    EXPECT_EQ(foundUser->getName(), "Find Me");
    EXPECT_EQ(foundUser->getEmail(), "findme@example.com");
}

TEST_F(RepositoryTest, FindOneReturnsNulloptForNonExistent) {
    auto& userRepo = provider.getRepository<UserEntity>();

    auto foundUser = userRepo.findByIdx(999);

    EXPECT_FALSE(foundUser.has_value());
}

TEST_F(RepositoryTest, FindByUuid) {
    auto user = createTestUser("UUID User", "uuid@example.com");
    user.setId("my-unique-uuid-1234567890123456");
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(user);

    auto foundUser = userRepo.findByUuid("my-unique-uuid-1234567890123456");

    EXPECT_TRUE(foundUser.has_value());
    EXPECT_EQ(foundUser->getName(), "UUID User");
}

TEST_F(RepositoryTest, FindByUuidReturnsNulloptForNonExistent) {
    auto& userRepo = provider.getRepository<UserEntity>();

    auto foundUser = userRepo.findByUuid("non-existent-uuid");

    EXPECT_FALSE(foundUser.has_value());
}

// Find Tests

TEST_F(RepositoryTest, FindAll) {
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(createTestUser("User 1", "user1@example.com"));
    userRepo.save(createTestUser("User 2", "user2@example.com"));
    userRepo.save(createTestUser("User 3", "user3@example.com"));

    auto users = userRepo.findAll();

    EXPECT_EQ(users.size(), 3);
}

TEST_F(RepositoryTest, FindWithWhereClause) {
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(createTestUser("Active User", "active@example.com", 25, true));
    userRepo.save(createTestUser("Inactive User", "inactive@example.com", 30, false));

    QueryBuilder qb;
    qb.where("active", CompareOp::Equals, static_cast<int64_t>(1));

    auto activeUsers = userRepo.find(qb);

    EXPECT_EQ(activeUsers.size(), 1);
    EXPECT_EQ(activeUsers[0].getName(), "Active User");
}

TEST_F(RepositoryTest, FindWithMultipleConditions) {
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(createTestUser("Young Active", "young@example.com", 20, true));
    userRepo.save(createTestUser("Old Active", "old@example.com", 50, true));
    userRepo.save(createTestUser("Young Inactive", "younginactive@example.com", 22, false));

    QueryBuilder qb;
    qb.where("active", CompareOp::Equals, static_cast<int64_t>(1))
      .andWhere("age", CompareOp::LessThan, static_cast<int64_t>(30));

    auto users = userRepo.find(qb);

    EXPECT_EQ(users.size(), 1);
    EXPECT_EQ(users[0].getName(), "Young Active");
}

TEST_F(RepositoryTest, FindWithOrderBy) {
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(createTestUser("Charlie", "charlie@example.com", 30));
    userRepo.save(createTestUser("Alice", "alice@example.com", 25));
    userRepo.save(createTestUser("Bob", "bob@example.com", 35));

    QueryBuilder qb;
    qb.orderBy("name", OrderDirection::Asc);

    auto users = userRepo.find(qb);

    EXPECT_EQ(users.size(), 3);
    EXPECT_EQ(users[0].getName(), "Alice");
    EXPECT_EQ(users[1].getName(), "Bob");
    EXPECT_EQ(users[2].getName(), "Charlie");
}

TEST_F(RepositoryTest, FindWithLimit) {
    auto& userRepo = provider.getRepository<UserEntity>();
    for (int i = 0; i < 10; ++i) {
        userRepo.save(createTestUser("User " + std::to_string(i), "user" + std::to_string(i) + "@example.com", i + 20));
    }

    QueryBuilder qb;
    qb.limit(5);

    auto users = userRepo.find(qb);

    EXPECT_EQ(users.size(), 5);
}

TEST_F(RepositoryTest, FindWithLimitAndOffset) {
    auto& userRepo = provider.getRepository<UserEntity>();
    for (int i = 0; i < 10; ++i) {
        userRepo.save(createTestUser("User " + std::to_string(i), "user" + std::to_string(i) + "@example.com", i + 20));
    }

    QueryBuilder qb;
    qb.orderBy("idx", OrderDirection::Asc).limit(3).offset(3);

    auto users = userRepo.find(qb);

    EXPECT_EQ(users.size(), 3);
    EXPECT_EQ(users[0].getIdx().value(), 4);
}

TEST_F(RepositoryTest, FindByEmail) {
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(createTestUser("Test User", "specific@example.com"));
    userRepo.save(createTestUser("Other User", "other@example.com"));

    QueryBuilder qb;
    qb.where("email", CompareOp::Equals, std::string("specific@example.com"));

    auto user = userRepo.findOne(qb);

    EXPECT_TRUE(user.has_value());
    EXPECT_EQ(user->getName(), "Test User");
}

TEST_F(RepositoryTest, FindActiveUsers) {
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(createTestUser("Active 1", "active1@example.com", 25, true));
    userRepo.save(createTestUser("Active 2", "active2@example.com", 30, true));
    userRepo.save(createTestUser("Inactive", "inactive@example.com", 35, false));

    QueryBuilder qb;
    qb.where("active", CompareOp::Equals, static_cast<int64_t>(1));

    auto activeUsers = userRepo.find(qb);

    EXPECT_EQ(activeUsers.size(), 2);
}

TEST_F(RepositoryTest, FindByAgeRange) {
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(createTestUser("Too Young", "young@example.com", 15));
    userRepo.save(createTestUser("In Range 1", "range1@example.com", 25));
    userRepo.save(createTestUser("In Range 2", "range2@example.com", 30));
    userRepo.save(createTestUser("Too Old", "old@example.com", 50));

    QueryBuilder qb;
    qb.whereBetween("age", static_cast<int64_t>(20), static_cast<int64_t>(35));

    auto usersInRange = userRepo.find(qb);

    EXPECT_EQ(usersInRange.size(), 2);
}

// Remove Tests

TEST_F(RepositoryTest, RemoveEntity) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto user = userRepo.save(createTestUser("To Delete", "delete@example.com"));
    auto idx = user.getIdx().value();

    bool removed = userRepo.remove(user);

    EXPECT_TRUE(removed);
    EXPECT_FALSE(userRepo.findByIdx(idx).has_value());
}

TEST_F(RepositoryTest, RemoveByIdx) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto user = userRepo.save(createTestUser("To Delete", "delete@example.com"));
    auto idx = user.getIdx().value();

    bool removed = userRepo.removeByIdx(idx);

    EXPECT_TRUE(removed);
    EXPECT_FALSE(userRepo.findByIdx(idx).has_value());
}

TEST_F(RepositoryTest, RemoveByUuid) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto user = createTestUser("To Delete", "delete@example.com");
    user.setId("delete-uuid-123456789012345678");
    userRepo.save(user);

    bool removed = userRepo.removeByUuid("delete-uuid-123456789012345678");

    EXPECT_TRUE(removed);
    EXPECT_FALSE(userRepo.findByUuid("delete-uuid-123456789012345678").has_value());
}

TEST_F(RepositoryTest, RemoveNonPersistedReturnsFalse) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto user = createTestUser("Not Saved", "notsaved@example.com");

    bool removed = userRepo.remove(user);

    EXPECT_FALSE(removed);
}

// Count and Exists Tests

TEST_F(RepositoryTest, Count) {
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(createTestUser("User 1", "user1@example.com"));
    userRepo.save(createTestUser("User 2", "user2@example.com"));
    userRepo.save(createTestUser("User 3", "user3@example.com"));

    int64_t count = userRepo.count();

    EXPECT_EQ(count, 3);
}

TEST_F(RepositoryTest, CountWithCondition) {
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(createTestUser("Active 1", "active1@example.com", 25, true));
    userRepo.save(createTestUser("Active 2", "active2@example.com", 30, true));
    userRepo.save(createTestUser("Inactive", "inactive@example.com", 35, false));

    QueryBuilder qb;
    qb.where("active", CompareOp::Equals, static_cast<int64_t>(1));

    int64_t count = userRepo.count(qb);

    EXPECT_EQ(count, 2);
}

TEST_F(RepositoryTest, ExistsById) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto user = userRepo.save(createTestUser("Exists", "exists@example.com"));

    EXPECT_TRUE(userRepo.exists(user.getIdx().value()));
    EXPECT_FALSE(userRepo.exists(999));
}

TEST_F(RepositoryTest, ExistsByUuid) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto user = createTestUser("Exists", "exists@example.com");
    user.setId("exists-uuid-123456789012345678");
    userRepo.save(user);

    EXPECT_TRUE(userRepo.existsByUuid("exists-uuid-123456789012345678"));
    EXPECT_FALSE(userRepo.existsByUuid("non-existent-uuid"));
}

// Order Repository Tests with Foreign Key (FK references idx)

TEST_F(RepositoryTest, SaveOrderWithForeignKey) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();

    auto user = userRepo.save(createTestUser("User", "user@example.com"));

    OrderEntity order;
    order.setUserIdx(user.getIdx().value());  // FK references user's idx (int64)
    order.setAmount(99.99);
    order.setStatus("pending");

    auto savedOrder = orderRepo.save(order);

    EXPECT_TRUE(savedOrder.isPersisted());
    EXPECT_EQ(savedOrder.getUserIdx(), user.getIdx().value());
    EXPECT_EQ(savedOrder.getAmount(), 99.99);
    EXPECT_EQ(savedOrder.getStatus(), "pending");
}

TEST_F(RepositoryTest, FindOrdersByUserIdx) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();

    auto user = userRepo.save(createTestUser("User", "user@example.com"));

    OrderEntity order1;
    order1.setUserIdx(user.getIdx().value());
    order1.setAmount(50.0);
    order1.setStatus("pending");
    orderRepo.save(order1);

    OrderEntity order2;
    order2.setUserIdx(user.getIdx().value());
    order2.setAmount(75.0);
    order2.setStatus("completed");
    orderRepo.save(order2);

    QueryBuilder qb;
    qb.where("user_idx", CompareOp::Equals, user.getIdx().value());

    auto orders = orderRepo.find(qb);

    EXPECT_EQ(orders.size(), 2);
}

TEST_F(RepositoryTest, FindOrdersByStatus) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();

    auto user = userRepo.save(createTestUser("User", "user@example.com"));

    OrderEntity order1;
    order1.setUserIdx(user.getIdx().value());
    order1.setAmount(50.0);
    order1.setStatus("pending");
    orderRepo.save(order1);

    OrderEntity order2;
    order2.setUserIdx(user.getIdx().value());
    order2.setAmount(75.0);
    order2.setStatus("completed");
    orderRepo.save(order2);

    QueryBuilder qb;
    qb.where("status", CompareOp::Equals, std::string("pending"));

    auto pendingOrders = orderRepo.find(qb);
    EXPECT_EQ(pendingOrders.size(), 1);
    EXPECT_EQ(pendingOrders[0].getStatus(), "pending");
}

TEST_F(RepositoryTest, FindOrdersByAmountGreaterThan) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();

    auto user = userRepo.save(createTestUser("User", "user@example.com"));

    OrderEntity order1;
    order1.setUserIdx(user.getIdx().value());
    order1.setAmount(50.0);
    order1.setStatus("pending");
    orderRepo.save(order1);

    OrderEntity order2;
    order2.setUserIdx(user.getIdx().value());
    order2.setAmount(150.0);
    order2.setStatus("pending");
    orderRepo.save(order2);

    QueryBuilder qb;
    qb.where("amount", CompareOp::GreaterThan, 100.0);

    auto largeOrders = orderRepo.find(qb);

    EXPECT_EQ(largeOrders.size(), 1);
    EXPECT_EQ(largeOrders[0].getAmount(), 150.0);
}

// Complex Query Tests

TEST_F(RepositoryTest, ComplexQueryWithMultipleOperators) {
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(createTestUser("Alice", "alice@gmail.com", 25, true));
    userRepo.save(createTestUser("Bob", "bob@yahoo.com", 30, true));
    userRepo.save(createTestUser("Charlie", "charlie@gmail.com", 35, false));
    userRepo.save(createTestUser("Diana", "diana@gmail.com", 28, true));

    QueryBuilder qb;
    qb.where("email", CompareOp::Like, std::string("%@gmail.com"))
      .andWhere("active", CompareOp::Equals, static_cast<int64_t>(1))
      .andWhere("age", CompareOp::LessThan, static_cast<int64_t>(30))
      .orderBy("name", OrderDirection::Asc);

    auto users = userRepo.find(qb);

    EXPECT_EQ(users.size(), 2);
    EXPECT_EQ(users[0].getName(), "Alice");
    EXPECT_EQ(users[1].getName(), "Diana");
}

TEST_F(RepositoryTest, InOperator) {
    auto& userRepo = provider.getRepository<UserEntity>();
    userRepo.save(createTestUser("Alice", "alice@example.com", 25, true));
    userRepo.save(createTestUser("Bob", "bob@example.com", 30, true));
    userRepo.save(createTestUser("Charlie", "charlie@example.com", 35, true));

    std::vector<DbValue> names = {std::string("Alice"), std::string("Charlie")};

    QueryBuilder qb;
    qb.whereIn("name", names);

    auto users = userRepo.find(qb);

    EXPECT_EQ(users.size(), 2);
}

// Transaction Tests

TEST_F(RepositoryTest, TransactionCommit) {
    auto& userRepo = provider.getRepository<UserEntity>();

    EXPECT_TRUE(provider.beginTransaction());

    userRepo.save(createTestUser("Transaction User", "transaction@example.com"));

    EXPECT_TRUE(provider.commit());

    EXPECT_EQ(userRepo.count(), 1);
}

TEST_F(RepositoryTest, TransactionRollback) {
    auto& userRepo = provider.getRepository<UserEntity>();

    // First save a user before transaction
    userRepo.save(createTestUser("Before Transaction", "before@example.com"));

    EXPECT_TRUE(provider.beginTransaction());

    userRepo.save(createTestUser("Transaction User", "transaction@example.com"));

    EXPECT_TRUE(provider.rollback());

    // Should only have the first user
    EXPECT_EQ(userRepo.count(), 1);
}

// Provider Tests

TEST_F(RepositoryTest, ProviderGetRepositoryReturnsSameInstance) {
    auto& repo1 = provider.getRepository<UserEntity>();
    auto& repo2 = provider.getRepository<UserEntity>();

    EXPECT_EQ(&repo1, &repo2);
}

TEST_F(RepositoryTest, ProviderWithoutDriverThrows) {
    Provider emptyProvider;

    EXPECT_THROW(emptyProvider.getRepository<UserEntity>(), std::runtime_error);
}

TEST_F(RepositoryTest, ProviderSynchronizeWithoutDriverThrows) {
    Provider emptyProvider;

    EXPECT_THROW(emptyProvider.synchronize(), std::runtime_error);
}

// ==========================================
// Foreign Key and Cascade Tests
// ==========================================

TEST_F(RepositoryTest, CascadeDeleteRemovesChildren) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();

    // Create user with orders
    auto user = userRepo.save(createTestUser("User", "user@example.com"));

    OrderEntity order1;
    order1.setUserIdx(user.getIdx().value());
    order1.setAmount(50.0);
    order1.setStatus("pending");
    orderRepo.save(order1);

    OrderEntity order2;
    order2.setUserIdx(user.getIdx().value());
    order2.setAmount(100.0);
    order2.setStatus("completed");
    orderRepo.save(order2);

    // Verify orders exist
    EXPECT_EQ(orderRepo.count(), 2);

    // Delete user - CASCADE should remove orders
    userRepo.remove(user);

    // Verify user and orders are gone
    EXPECT_EQ(userRepo.count(), 0);
    EXPECT_EQ(orderRepo.count(), 0);
}

TEST_F(RepositoryTest, ThreeLevelHierarchyCascadeDelete) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();
    auto& itemRepo = provider.getRepository<OrderItemEntity>();

    // Create User -> Order -> OrderItems hierarchy
    auto user = userRepo.save(createTestUser("Customer", "customer@example.com"));

    OrderEntity order;
    order.setUserIdx(user.getIdx().value());
    order.setAmount(250.0);
    order.setStatus("pending");
    auto savedOrder = orderRepo.save(order);

    // Add items to order
    OrderItemEntity item1;
    item1.setOrderIdx(savedOrder.getIdx().value());
    item1.setProductName("Product A");
    item1.setQuantity(2);
    item1.setUnitPrice(50.0);
    itemRepo.save(item1);

    OrderItemEntity item2;
    item2.setOrderIdx(savedOrder.getIdx().value());
    item2.setProductName("Product B");
    item2.setQuantity(3);
    item2.setUnitPrice(50.0);
    itemRepo.save(item2);

    // Verify hierarchy exists
    EXPECT_EQ(userRepo.count(), 1);
    EXPECT_EQ(orderRepo.count(), 1);
    EXPECT_EQ(itemRepo.count(), 2);

    // Delete user - CASCADE should remove entire hierarchy
    userRepo.remove(user);

    // Verify everything is gone
    EXPECT_EQ(userRepo.count(), 0);
    EXPECT_EQ(orderRepo.count(), 0);
    EXPECT_EQ(itemRepo.count(), 0);
}

TEST_F(RepositoryTest, DeleteOrderRemovesOnlyItsItems) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();
    auto& itemRepo = provider.getRepository<OrderItemEntity>();

    auto user = userRepo.save(createTestUser("Customer", "customer@example.com"));

    // Create two orders
    OrderEntity order1;
    order1.setUserIdx(user.getIdx().value());
    order1.setAmount(100.0);
    order1.setStatus("pending");
    auto savedOrder1 = orderRepo.save(order1);

    OrderEntity order2;
    order2.setUserIdx(user.getIdx().value());
    order2.setAmount(200.0);
    order2.setStatus("pending");
    auto savedOrder2 = orderRepo.save(order2);

    // Add items to each order
    OrderItemEntity item1;
    item1.setOrderIdx(savedOrder1.getIdx().value());
    item1.setProductName("Order1 Product");
    item1.setQuantity(1);
    item1.setUnitPrice(100.0);
    itemRepo.save(item1);

    OrderItemEntity item2;
    item2.setOrderIdx(savedOrder2.getIdx().value());
    item2.setProductName("Order2 Product");
    item2.setQuantity(1);
    item2.setUnitPrice(200.0);
    itemRepo.save(item2);

    EXPECT_EQ(itemRepo.count(), 2);

    // Delete only order1 - should only remove item1
    orderRepo.remove(savedOrder1);

    EXPECT_EQ(orderRepo.count(), 1);
    EXPECT_EQ(itemRepo.count(), 1);

    // Verify the remaining item belongs to order2
    auto remainingItems = itemRepo.findAll();
    EXPECT_EQ(remainingItems[0].getOrderIdx(), savedOrder2.getIdx().value());
    EXPECT_EQ(remainingItems[0].getProductName(), "Order2 Product");
}

TEST_F(RepositoryTest, FindChildrenByParentIdx) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();

    // Create multiple users
    auto user1 = userRepo.save(createTestUser("User1", "user1@example.com"));
    auto user2 = userRepo.save(createTestUser("User2", "user2@example.com"));

    // Create orders for user1
    OrderEntity order1;
    order1.setUserIdx(user1.getIdx().value());
    order1.setAmount(50.0);
    order1.setStatus("pending");
    orderRepo.save(order1);

    OrderEntity order2;
    order2.setUserIdx(user1.getIdx().value());
    order2.setAmount(100.0);
    order2.setStatus("completed");
    orderRepo.save(order2);

    // Create order for user2
    OrderEntity order3;
    order3.setUserIdx(user2.getIdx().value());
    order3.setAmount(200.0);
    order3.setStatus("pending");
    orderRepo.save(order3);

    // Find orders for user1
    QueryBuilder qb1;
    qb1.where("user_idx", CompareOp::Equals, user1.getIdx().value());
    auto user1Orders = orderRepo.find(qb1);
    EXPECT_EQ(user1Orders.size(), 2);

    // Find orders for user2
    QueryBuilder qb2;
    qb2.where("user_idx", CompareOp::Equals, user2.getIdx().value());
    auto user2Orders = orderRepo.find(qb2);
    EXPECT_EQ(user2Orders.size(), 1);
    EXPECT_EQ(user2Orders[0].getAmount(), 200.0);
}

TEST_F(RepositoryTest, ChildCountByParent) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();

    auto user = userRepo.save(createTestUser("User", "user@example.com"));

    // Create 5 orders
    for (int i = 0; i < 5; ++i) {
        OrderEntity order;
        order.setUserIdx(user.getIdx().value());
        order.setAmount(static_cast<double>(i * 10));
        order.setStatus("pending");
        orderRepo.save(order);
    }

    // Count orders by user
    QueryBuilder qb;
    qb.where("user_idx", CompareOp::Equals, user.getIdx().value());
    auto count = orderRepo.count(qb);

    EXPECT_EQ(count, 5);
}

TEST_F(RepositoryTest, OrderItemWithCorrectOrderIdx) {
    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();
    auto& itemRepo = provider.getRepository<OrderItemEntity>();

    auto user = userRepo.save(createTestUser("User", "user@example.com"));

    OrderEntity order;
    order.setUserIdx(user.getIdx().value());
    order.setAmount(300.0);
    order.setStatus("pending");
    auto savedOrder = orderRepo.save(order);

    // Create items with different quantities
    for (int i = 1; i <= 3; ++i) {
        OrderItemEntity item;
        item.setOrderIdx(savedOrder.getIdx().value());
        item.setProductName("Product " + std::to_string(i));
        item.setQuantity(i);
        item.setUnitPrice(100.0);
        itemRepo.save(item);
    }

    // Find items for this order
    QueryBuilder qb;
    qb.where("order_idx", CompareOp::Equals, savedOrder.getIdx().value())
      .orderBy("quantity", OrderDirection::Asc);

    auto items = itemRepo.find(qb);

    EXPECT_EQ(items.size(), 3);
    EXPECT_EQ(items[0].getQuantity(), 1);
    EXPECT_EQ(items[1].getQuantity(), 2);
    EXPECT_EQ(items[2].getQuantity(), 3);
}
