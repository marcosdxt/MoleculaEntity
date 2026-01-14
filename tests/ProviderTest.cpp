#include <gtest/gtest.h>
#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/drivers/SQLite3Driver.hpp>
#include "TestEntities.hpp"

using namespace MoleculaEntity;
using namespace MoleculaEntity::Test;

class ProviderTest : public ::testing::Test {
protected:
    Provider provider;
    std::shared_ptr<SQLite3Driver> driver;

    void SetUp() override {
        driver = std::make_shared<SQLite3Driver>(":memory:");
    }
};

// Driver Tests

TEST_F(ProviderTest, SetDriver) {
    provider.setDriver(driver);

    EXPECT_EQ(provider.getDriver(), driver);
}

TEST_F(ProviderTest, GetDriverReturnsNullBeforeSet) {
    EXPECT_EQ(provider.getDriver(), nullptr);
}

// Entity Registration Tests

TEST_F(ProviderTest, RegisterSingleEntity) {
    provider.setDriver(driver);
    provider.registerEntity<UserEntity>();
    provider.synchronize();

    // Should be able to get repository
    auto& repo = provider.getRepository<UserEntity>();
    EXPECT_EQ(repo.count(), 0);
}

TEST_F(ProviderTest, RegisterMultipleEntities) {
    provider.setDriver(driver);
    provider.registerEntities<UserEntity, OrderEntity>();
    provider.synchronize();

    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();

    EXPECT_EQ(userRepo.count(), 0);
    EXPECT_EQ(orderRepo.count(), 0);
}

// Synchronization Tests

TEST_F(ProviderTest, SynchronizeCreatesTable) {
    provider.setDriver(driver);
    provider.registerEntity<UserEntity>();
    provider.synchronize();

    // Check table exists
    EXPECT_TRUE(driver->tableExists("users"));
}

TEST_F(ProviderTest, SynchronizeCreatesMultipleTables) {
    provider.setDriver(driver);
    provider.registerEntities<UserEntity, OrderEntity, OrderItemEntity>();
    provider.synchronize();

    EXPECT_TRUE(driver->tableExists("users"));
    EXPECT_TRUE(driver->tableExists("orders"));
    EXPECT_TRUE(driver->tableExists("order_items"));
}

TEST_F(ProviderTest, SynchronizeThrowsWithoutDriver) {
    EXPECT_THROW(provider.synchronize(), std::runtime_error);
}

TEST_F(ProviderTest, SynchronizeEnablesForeignKeys) {
    provider.setDriver(driver);
    provider.registerEntities<UserEntity, OrderEntity>();
    provider.synchronize();

    // Insert user and order to verify FK is working
    auto& userRepo = provider.getRepository<UserEntity>();
    auto& orderRepo = provider.getRepository<OrderEntity>();

    UserEntity user;
    user.setName("Test User");
    user.setEmail("test@example.com");
    auto savedUser = userRepo.save(user);

    OrderEntity order;
    order.setUserIdx(savedUser.getIdx().value());
    order.setAmount(100.0);
    order.setStatus("pending");
    auto savedOrder = orderRepo.save(order);

    EXPECT_TRUE(savedOrder.isPersisted());
}

// Repository Caching Tests

TEST_F(ProviderTest, GetRepositoryReturnsSameInstance) {
    provider.setDriver(driver);
    provider.registerEntity<UserEntity>();
    provider.synchronize();

    auto& repo1 = provider.getRepository<UserEntity>();
    auto& repo2 = provider.getRepository<UserEntity>();

    EXPECT_EQ(&repo1, &repo2);
}

TEST_F(ProviderTest, SetDriverClearsRepositoryCache) {
    provider.setDriver(driver);
    provider.registerEntity<UserEntity>();
    provider.synchronize();

    auto& repo1 = provider.getRepository<UserEntity>();

    // Set a new driver
    auto newDriver = std::make_shared<SQLite3Driver>(":memory:");
    provider.setDriver(newDriver);
    provider.synchronize();

    auto& repo2 = provider.getRepository<UserEntity>();

    // Should be a different repository instance
    EXPECT_NE(&repo1, &repo2);
}

// Transaction Tests

TEST_F(ProviderTest, BeginTransactionReturnsTrueWithDriver) {
    provider.setDriver(driver);

    EXPECT_TRUE(provider.beginTransaction());
}

TEST_F(ProviderTest, BeginTransactionReturnsFalseWithoutDriver) {
    EXPECT_FALSE(provider.beginTransaction());
}

TEST_F(ProviderTest, CommitReturnsTrueWithDriver) {
    provider.setDriver(driver);
    provider.beginTransaction();

    EXPECT_TRUE(provider.commit());
}

TEST_F(ProviderTest, CommitReturnsFalseWithoutDriver) {
    EXPECT_FALSE(provider.commit());
}

TEST_F(ProviderTest, RollbackReturnsTrueWithDriver) {
    provider.setDriver(driver);
    provider.beginTransaction();

    EXPECT_TRUE(provider.rollback());
}

TEST_F(ProviderTest, RollbackReturnsFalseWithoutDriver) {
    EXPECT_FALSE(provider.rollback());
}

TEST_F(ProviderTest, TransactionActuallyRollsBack) {
    provider.setDriver(driver);
    provider.registerEntity<UserEntity>();
    provider.synchronize();

    auto& repo = provider.getRepository<UserEntity>();

    // Insert one user before transaction
    UserEntity user1;
    user1.setName("User 1");
    user1.setEmail("user1@example.com");
    repo.save(user1);

    EXPECT_EQ(repo.count(), 1);

    // Start transaction and insert another user
    provider.beginTransaction();

    UserEntity user2;
    user2.setName("User 2");
    user2.setEmail("user2@example.com");
    repo.save(user2);

    EXPECT_EQ(repo.count(), 2);

    // Rollback
    provider.rollback();

    // Should only have the first user
    EXPECT_EQ(repo.count(), 1);
}

// Error Handling Tests

TEST_F(ProviderTest, GetRepositoryThrowsWithoutDriver) {
    EXPECT_THROW(provider.getRepository<UserEntity>(), std::runtime_error);
}

// Table Structure Tests

TEST_F(ProviderTest, TableHasCorrectBaseColumns) {
    provider.setDriver(driver);
    provider.registerEntity<UserEntity>();
    provider.synchronize();

    // Query table info
    auto result = driver->query("PRAGMA table_info(users)");

    // Should have at least idx, id, created_at, updated_at + entity columns
    EXPECT_GE(result.size(), 8); // 4 base + 4 entity columns

    // Check column names
    std::set<std::string> columnNames;
    for (const auto& row : result) {
        columnNames.insert(getString(row[1])); // column name is at index 1
    }

    EXPECT_TRUE(columnNames.count("idx") > 0);
    EXPECT_TRUE(columnNames.count("id") > 0);
    EXPECT_TRUE(columnNames.count("created_at") > 0);
    EXPECT_TRUE(columnNames.count("updated_at") > 0);
    EXPECT_TRUE(columnNames.count("name") > 0);
    EXPECT_TRUE(columnNames.count("email") > 0);
    EXPECT_TRUE(columnNames.count("age") > 0);
    EXPECT_TRUE(columnNames.count("active") > 0);
}

TEST_F(ProviderTest, TableHasIdxAsPrimaryKey) {
    provider.setDriver(driver);
    provider.registerEntity<UserEntity>();
    provider.synchronize();

    auto result = driver->query("PRAGMA table_info(users)");

    for (const auto& row : result) {
        std::string colName = getString(row[1]);
        int pk = static_cast<int>(getInt64(row[5])); // pk is at index 5

        if (colName == "idx") {
            EXPECT_EQ(pk, 1); // idx should be primary key
        }
    }
}

TEST_F(ProviderTest, TableHasIndexOnId) {
    provider.setDriver(driver);
    provider.registerEntity<UserEntity>();
    provider.synchronize();

    auto result = driver->query("SELECT name FROM sqlite_master WHERE type='index' AND tbl_name='users'");

    std::set<std::string> indexNames;
    for (const auto& row : result) {
        indexNames.insert(getString(row[0]));
    }

    EXPECT_TRUE(indexNames.count("idx_users_id") > 0);
}

TEST_F(ProviderTest, ForeignKeyIndexIsCreated) {
    provider.setDriver(driver);
    provider.registerEntities<UserEntity, OrderEntity>();
    provider.synchronize();

    auto result = driver->query("SELECT name FROM sqlite_master WHERE type='index' AND tbl_name='orders'");

    std::set<std::string> indexNames;
    for (const auto& row : result) {
        indexNames.insert(getString(row[0]));
    }

    EXPECT_TRUE(indexNames.count("idx_orders_user_idx") > 0);
}

// UpdatedAt Trigger Tests

TEST_F(ProviderTest, UpdatedAtTriggerIsCreated) {
    provider.setDriver(driver);
    provider.registerEntity<UserEntity>();
    provider.synchronize();

    auto result = driver->query("SELECT name FROM sqlite_master WHERE type='trigger' AND tbl_name='users'");

    EXPECT_GE(result.size(), 1);

    std::string triggerName = getString(result[0][0]);
    EXPECT_EQ(triggerName, "users_updated_at_trigger");
}
