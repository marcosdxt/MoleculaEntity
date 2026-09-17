#include <gtest/gtest.h>

#include <algorithm>
#include <MoleculaEntity/SchemaManager.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>
#include "TestEntities.hpp"

using namespace MoleculaEntity;
using namespace MoleculaEntity::Test;

class SchemaManagerTest : public ::testing::Test {
protected:
    std::shared_ptr<SQLite3DatabaseManager> db;
    std::unique_ptr<SchemaManager> schemaManager;

    void SetUp() override {
        db = SQLite3DatabaseManager::open(":memory:");
        ASSERT_TRUE(db) << "não abriu o banco em memória";
        schemaManager = std::make_unique<SchemaManager>(db);
        schemaManager->initialize();
    }

    void TearDown() override {
        schemaManager.reset();
        db.reset();
    }
};

TEST_F(SchemaManagerTest, InitializeCreatesMigrationsTable) {
    EXPECT_TRUE(schemaManager->tableExists("__schema_migrations"));
}

TEST_F(SchemaManagerTest, SyncEntityCreatesTable) {
    EXPECT_FALSE(schemaManager->tableExists("users"));

    schemaManager->syncEntity<UserEntity>();

    EXPECT_TRUE(schemaManager->tableExists("users"));
}

TEST_F(SchemaManagerTest, SyncEntityCreatesTableWithCorrectVersion) {
    schemaManager->syncEntity<UserEntity>();

    int version = schemaManager->getTableVersion("users");
    EXPECT_EQ(version, 1);
}

TEST_F(SchemaManagerTest, SyncEntityDoesNotRecreatExistingTable) {
    schemaManager->syncEntity<UserEntity>();

    // Insert test data
    db->execute("INSERT INTO users (id, name, email, active) VALUES ('uuid-1', 'Test', 'test@test.com', 1)");

    // Sync again - should not drop table
    schemaManager->syncEntity<UserEntity>();

    // Data should still exist
    auto result = db->query("SELECT COUNT(*) FROM users");
    EXPECT_EQ(std::get<int64_t>(result[0][0]), 1);
}

TEST_F(SchemaManagerTest, TableExistsReturnsFalseForNonExistent) {
    EXPECT_FALSE(schemaManager->tableExists("non_existent_table"));
}

TEST_F(SchemaManagerTest, TableExistsReturnsTrueForExisting) {
    schemaManager->syncEntity<UserEntity>();
    EXPECT_TRUE(schemaManager->tableExists("users"));
}

TEST_F(SchemaManagerTest, GetTableVersionReturnsZeroForNonExistent) {
    int version = schemaManager->getTableVersion("non_existent_table");
    EXPECT_EQ(version, 0);
}

TEST_F(SchemaManagerTest, DropTable) {
    schemaManager->syncEntity<UserEntity>();
    EXPECT_TRUE(schemaManager->tableExists("users"));

    schemaManager->dropTable<UserEntity>();
    EXPECT_FALSE(schemaManager->tableExists("users"));
}

TEST_F(SchemaManagerTest, SyncMultipleEntities) {
    schemaManager->syncEntity<UserEntity>();
    schemaManager->syncEntity<OrderEntity>();

    EXPECT_TRUE(schemaManager->tableExists("users"));
    EXPECT_TRUE(schemaManager->tableExists("orders"));
}

TEST_F(SchemaManagerTest, TableHasBaseColumns) {
    schemaManager->syncEntity<UserEntity>();

    // Check that we can query base columns
    auto result = db->query("PRAGMA table_info(users)");

    std::vector<std::string> columnNames;
    for (const auto& row : result) {
        columnNames.push_back(std::get<std::string>(row[1]));
    }

    EXPECT_TRUE(std::find(columnNames.begin(), columnNames.end(), "idx") != columnNames.end());
    EXPECT_TRUE(std::find(columnNames.begin(), columnNames.end(), "id") != columnNames.end());
    EXPECT_TRUE(std::find(columnNames.begin(), columnNames.end(), "created_at") != columnNames.end());
    EXPECT_TRUE(std::find(columnNames.begin(), columnNames.end(), "updated_at") != columnNames.end());
}

TEST_F(SchemaManagerTest, TableHasEntityColumns) {
    schemaManager->syncEntity<UserEntity>();

    auto result = db->query("PRAGMA table_info(users)");

    std::vector<std::string> columnNames;
    for (const auto& row : result) {
        columnNames.push_back(std::get<std::string>(row[1]));
    }

    EXPECT_TRUE(std::find(columnNames.begin(), columnNames.end(), "name") != columnNames.end());
    EXPECT_TRUE(std::find(columnNames.begin(), columnNames.end(), "email") != columnNames.end());
    EXPECT_TRUE(std::find(columnNames.begin(), columnNames.end(), "age") != columnNames.end());
    EXPECT_TRUE(std::find(columnNames.begin(), columnNames.end(), "active") != columnNames.end());
}

TEST_F(SchemaManagerTest, OrderTableHasForeignKey) {
    schemaManager->syncEntity<UserEntity>();
    schemaManager->syncEntity<OrderEntity>();

    // Insert user first
    db->execute("INSERT INTO users (id, name, email, active) VALUES ('uuid-1', 'Test', 'test@test.com', 1)");

    // This should work - valid foreign key
    EXPECT_NO_THROW({
        db->execute("INSERT INTO orders (id, user_id, amount, status) VALUES ('order-1', 1, 100.0, 'pending')");
    });
}

TEST_F(SchemaManagerTest, UpdatedAtTriggerExists) {
    schemaManager->syncEntity<UserEntity>();

    // Check trigger exists
    auto result = db->query("SELECT name FROM sqlite_master WHERE type='trigger' AND name='users_updated_at_trigger'");
    EXPECT_EQ(result.size(), 1);
}

TEST_F(SchemaManagerTest, IdIndexExists) {
    schemaManager->syncEntity<UserEntity>();

    // Check index exists
    auto result = db->query("SELECT name FROM sqlite_master WHERE type='index' AND name='idx_users_id'");
    EXPECT_EQ(result.size(), 1);
}

class MigrationEntity : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "migration_test"; }
    [[nodiscard]] int tableVersion() const override { return 2; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override {
        return {
            {"field1", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt},
            {"field2", ColumnType::Integer, true, false, false, std::nullopt, std::nullopt, std::nullopt}
        };
    }

    [[nodiscard]] std::vector<Migration> migrations() const override {
        return {
            {2, "Add field2 column", "ALTER TABLE migration_test ADD COLUMN field2 INTEGER", ""}
        };
    }
};

class MigrationEntityV1 : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "migration_test"; }
    [[nodiscard]] int tableVersion() const override { return 1; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override {
        return {
            {"field1", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt}
        };
    }
};

TEST_F(SchemaManagerTest, MigrationApplied) {
    // Create table at version 1
    schemaManager->syncEntity<MigrationEntityV1>();
    EXPECT_EQ(schemaManager->getTableVersion("migration_test"), 1);

    // Migrate to version 2
    schemaManager->syncEntity<MigrationEntity>();
    EXPECT_EQ(schemaManager->getTableVersion("migration_test"), 2);

    // Check new column exists
    auto result = db->query("PRAGMA table_info(migration_test)");
    std::vector<std::string> columnNames;
    for (const auto& row : result) {
        columnNames.push_back(std::get<std::string>(row[1]));
    }
    EXPECT_TRUE(std::find(columnNames.begin(), columnNames.end(), "field2") != columnNames.end());
}
