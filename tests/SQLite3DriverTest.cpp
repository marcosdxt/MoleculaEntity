#include <gtest/gtest.h>
#include <MoleculaEntity/drivers/SQLite3Driver.hpp>

using namespace MoleculaEntity;

class SQLite3DriverTest : public ::testing::Test {
protected:
    std::shared_ptr<SQLite3Driver> driver;

    void SetUp() override {
        driver = std::make_shared<SQLite3Driver>(":memory:");
    }
};

// Connection Tests

TEST_F(SQLite3DriverTest, ConnectsToInMemoryDatabase) {
    EXPECT_NE(driver, nullptr);
}

// Execute Tests

TEST_F(SQLite3DriverTest, ExecuteCreateTable) {
    bool result = driver->execute(
        "CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)"
    );

    EXPECT_TRUE(result);
    EXPECT_TRUE(driver->tableExists("test"));
}

TEST_F(SQLite3DriverTest, ExecuteWithParameters) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");

    bool result = driver->execute(
        "INSERT INTO test (name) VALUES (?)",
        {std::string("John")}
    );

    EXPECT_TRUE(result);
}

TEST_F(SQLite3DriverTest, ExecuteMultipleParameters) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT, age INTEGER, score REAL)");

    bool result = driver->execute(
        "INSERT INTO test (name, age, score) VALUES (?, ?, ?)",
        {std::string("John"), int64_t(30), 95.5}
    );

    EXPECT_TRUE(result);
}

TEST_F(SQLite3DriverTest, ExecuteWithNullParameter) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");

    bool result = driver->execute(
        "INSERT INTO test (name) VALUES (?)",
        {nullptr}
    );

    EXPECT_TRUE(result);
}

// Query Tests

TEST_F(SQLite3DriverTest, QueryReturnsResults) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");
    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("Alice")});
    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("Bob")});

    auto result = driver->query("SELECT * FROM test");

    EXPECT_EQ(result.size(), 2);
}

TEST_F(SQLite3DriverTest, QueryWithParameters) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)");
    driver->execute("INSERT INTO test (name, age) VALUES (?, ?)", {std::string("Alice"), int64_t(25)});
    driver->execute("INSERT INTO test (name, age) VALUES (?, ?)", {std::string("Bob"), int64_t(30)});

    auto result = driver->query(
        "SELECT * FROM test WHERE age > ?",
        {int64_t(27)}
    );

    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(getString(result[0][1]), "Bob");
}

TEST_F(SQLite3DriverTest, QueryReturnsCorrectTypes) {
    driver->execute("CREATE TABLE test (id INTEGER, name TEXT, score REAL)");
    driver->execute("INSERT INTO test VALUES (?, ?, ?)", {int64_t(1), std::string("Test"), 95.5});

    auto result = driver->query("SELECT * FROM test");

    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(getInt64(result[0][0]), 1);
    EXPECT_EQ(getString(result[0][1]), "Test");
    EXPECT_DOUBLE_EQ(getDouble(result[0][2]), 95.5);
}

TEST_F(SQLite3DriverTest, QueryReturnsNull) {
    driver->execute("CREATE TABLE test (id INTEGER, name TEXT)");
    driver->execute("INSERT INTO test VALUES (?, ?)", {int64_t(1), nullptr});

    auto result = driver->query("SELECT * FROM test");

    EXPECT_EQ(result.size(), 1);
    EXPECT_TRUE(isNull(result[0][1]));
}

TEST_F(SQLite3DriverTest, QueryReturnsEmptyForNoMatches) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY)");

    auto result = driver->query("SELECT * FROM test WHERE id = 999");

    EXPECT_TRUE(result.empty());
}

// Table Exists Tests

TEST_F(SQLite3DriverTest, TableExistsReturnsFalseForNonExistent) {
    EXPECT_FALSE(driver->tableExists("nonexistent"));
}

TEST_F(SQLite3DriverTest, TableExistsReturnsTrueAfterCreate) {
    driver->execute("CREATE TABLE mytest (id INTEGER)");

    EXPECT_TRUE(driver->tableExists("mytest"));
}

// Last Insert Row ID Tests

TEST_F(SQLite3DriverTest, LastInsertRowIdReturnsCorrectValue) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT)");

    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("First")});
    EXPECT_EQ(driver->lastInsertRowId(), 1);

    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("Second")});
    EXPECT_EQ(driver->lastInsertRowId(), 2);

    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("Third")});
    EXPECT_EQ(driver->lastInsertRowId(), 3);
}

// Affected Rows Tests

TEST_F(SQLite3DriverTest, AffectedRowsAfterInsert) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");
    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("Test")});

    EXPECT_EQ(driver->affectedRows(), 1);
}

TEST_F(SQLite3DriverTest, AffectedRowsAfterUpdate) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");
    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("Test1")});
    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("Test2")});

    driver->execute("UPDATE test SET name = ?", {std::string("Updated")});

    EXPECT_EQ(driver->affectedRows(), 2);
}

TEST_F(SQLite3DriverTest, AffectedRowsAfterDelete) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");
    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("Test1")});
    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("Test2")});

    driver->execute("DELETE FROM test WHERE id = 1");

    EXPECT_EQ(driver->affectedRows(), 1);
}

TEST_F(SQLite3DriverTest, AffectedRowsZeroForNoMatch) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");
    driver->execute("DELETE FROM test WHERE id = 999");

    EXPECT_EQ(driver->affectedRows(), 0);
}

// Transaction Tests

TEST_F(SQLite3DriverTest, BeginTransactionReturnsTrue) {
    EXPECT_TRUE(driver->beginTransaction());
}

TEST_F(SQLite3DriverTest, CommitReturnsTrue) {
    driver->beginTransaction();
    EXPECT_TRUE(driver->commit());
}

TEST_F(SQLite3DriverTest, RollbackReturnsTrue) {
    driver->beginTransaction();
    EXPECT_TRUE(driver->rollback());
}

TEST_F(SQLite3DriverTest, TransactionRollbackUndoesChanges) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");
    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("Before")});

    driver->beginTransaction();
    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("During")});

    auto countDuring = driver->query("SELECT COUNT(*) FROM test");
    EXPECT_EQ(getInt64(countDuring[0][0]), 2);

    driver->rollback();

    auto countAfter = driver->query("SELECT COUNT(*) FROM test");
    EXPECT_EQ(getInt64(countAfter[0][0]), 1);
}

TEST_F(SQLite3DriverTest, TransactionCommitPersistsChanges) {
    driver->execute("CREATE TABLE test (id INTEGER PRIMARY KEY, name TEXT)");

    driver->beginTransaction();
    driver->execute("INSERT INTO test (name) VALUES (?)", {std::string("Committed")});
    driver->commit();

    auto count = driver->query("SELECT COUNT(*) FROM test");
    EXPECT_EQ(getInt64(count[0][0]), 1);
}

// Type Handling Tests

TEST_F(SQLite3DriverTest, HandlesBlobAsString) {
    driver->execute("CREATE TABLE test (id INTEGER, data BLOB)");
    driver->execute("INSERT INTO test VALUES (?, ?)", {int64_t(1), std::string("binary data")});

    auto result = driver->query("SELECT * FROM test");
    EXPECT_EQ(result.size(), 1);
}

TEST_F(SQLite3DriverTest, HandlesLargeInteger) {
    driver->execute("CREATE TABLE test (id INTEGER, big_num INTEGER)");
    int64_t largeNum = 9223372036854775807LL; // Max int64
    driver->execute("INSERT INTO test VALUES (?, ?)", {int64_t(1), largeNum});

    auto result = driver->query("SELECT * FROM test");
    EXPECT_EQ(getInt64(result[0][1]), largeNum);
}

TEST_F(SQLite3DriverTest, HandlesSpecialCharactersInString) {
    driver->execute("CREATE TABLE test (id INTEGER, text TEXT)");
    std::string specialText = "Hello 'World' with \"quotes\" and \n newlines";
    driver->execute("INSERT INTO test VALUES (?, ?)", {int64_t(1), specialText});

    auto result = driver->query("SELECT * FROM test");
    EXPECT_EQ(getString(result[0][1]), specialText);
}

TEST_F(SQLite3DriverTest, HandlesEmptyString) {
    driver->execute("CREATE TABLE test (id INTEGER, text TEXT)");
    driver->execute("INSERT INTO test VALUES (?, ?)", {int64_t(1), std::string("")});

    auto result = driver->query("SELECT * FROM test");
    EXPECT_EQ(getString(result[0][1]), "");
}

// PRAGMA Tests

TEST_F(SQLite3DriverTest, CanSetPragma) {
    EXPECT_TRUE(driver->execute("PRAGMA foreign_keys = ON"));

    auto result = driver->query("PRAGMA foreign_keys");
    EXPECT_EQ(getInt64(result[0][0]), 1);
}

// Foreign Key Tests

TEST_F(SQLite3DriverTest, ForeignKeyConstraintWorks) {
    driver->execute("PRAGMA foreign_keys = ON");
    driver->execute("CREATE TABLE parent (id TEXT PRIMARY KEY)");
    driver->execute("CREATE TABLE child (id INTEGER PRIMARY KEY, parent_id TEXT, FOREIGN KEY (parent_id) REFERENCES parent(id))");

    driver->execute("INSERT INTO parent (id) VALUES (?)", {std::string("p1")});
    driver->execute("INSERT INTO child (parent_id) VALUES (?)", {std::string("p1")});

    // This should work
    EXPECT_EQ(driver->affectedRows(), 1);
}
