#include <gtest/gtest.h>

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <memory>
#include <string>
#include <utility>

using namespace MoleculaEntity;

// `retireEntity`: an entity left the schema, its table goes, and so does its
// history in `__schema_migrations` — in one transaction, without the application
// touching the library's bookkeeping table.
//
// The case from the field: the table was dropped and the records stayed, so the
// next time an entity with that name was synced, recording its initial version
// hit UNIQUE(table_name, version) and the database no longer opened.

namespace {

class Sensor : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "sensors"; }
    [[nodiscard]] int tableVersion() const override { return 2; }
    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {{"name", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt}};
    }
};

// A decorator that fails the one statement we tell it to — to break the retire
// halfway through and see whether the first half comes back.
class FailOn final : public IDatabaseManager {
public:
    FailOn(DatabaseManagerPtr inner, std::string needle)
        : inner_(std::move(inner)), needle_(std::move(needle)) {}

    bool execute(const std::string& sql) override { return execute(sql, {}); }
    bool execute(const std::string& sql, const std::vector<DbValue>& params) override
    {
        if (!needle_.empty() && sql.find(needle_) != std::string::npos) {
            error_ = "injected failure";
            failed_ = true;
            return false;
        }
        failed_ = false;
        return inner_->execute(sql, params);
    }
    bool executeScript(const std::string& sql) override { failed_ = false; return inner_->executeScript(sql); }
    DbResult query(const std::string& sql) override { failed_ = false; return inner_->query(sql); }
    DbResult query(const std::string& sql, const std::vector<DbValue>& params) override
    {
        failed_ = false;
        return inner_->query(sql, params);
    }
    int64_t lastInsertRowId() override { return inner_->lastInsertRowId(); }
    int affectedRows() override { return inner_->affectedRows(); }
    bool beginTransaction() override { failed_ = false; return inner_->beginTransaction(); }
    bool commit() override { failed_ = false; return inner_->commit(); }
    bool rollback() override { failed_ = false; return inner_->rollback(); }
    [[nodiscard]] bool ok() const noexcept override { return !failed_ && inner_->ok(); }
    [[nodiscard]] const std::string& lastError() const noexcept override
    {
        return failed_ ? error_ : inner_->lastError();
    }

private:
    DatabaseManagerPtr inner_;
    std::string needle_;
    std::string error_;
    bool failed_ = false;
};

class RetireEntityTest : public ::testing::Test {
protected:
    std::shared_ptr<SQLite3DatabaseManager> db;

    void SetUp() override
    {
        db = SQLite3DatabaseManager::open(":memory:");
        ASSERT_TRUE(db);
    }

    [[nodiscard]] bool tableExists(const std::string& name)
    {
        return !db->query("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?", {name})
                    .empty();
    }

    [[nodiscard]] int64_t records(const std::string& table)
    {
        const auto rows = db->query("SELECT COUNT(*) FROM __schema_migrations WHERE table_name = ?", {table});
        return rows.empty() ? -1 : std::get<int64_t>(rows[0][0]);
    }
};

} // namespace

TEST_F(RetireEntityTest, RemovesTableAndRecords)
{
    SchemaManager schema(db);
    ASSERT_TRUE(schema.initialize());
    ASSERT_TRUE(schema.syncEntity<Sensor>()) << schema.lastError();
    ASSERT_TRUE(tableExists("sensors"));
    ASSERT_EQ(records("sensors"), 1);

    EXPECT_TRUE(schema.retireEntity("sensors")) << schema.lastError();
    EXPECT_TRUE(schema.lastError().empty());
    EXPECT_FALSE(tableExists("sensors"));
    EXPECT_EQ(records("sensors"), 0);
}

TEST_F(RetireEntityTest, IsIdempotent)
{
    SchemaManager schema(db);
    ASSERT_TRUE(schema.initialize());
    ASSERT_TRUE(schema.syncEntity<Sensor>());

    EXPECT_TRUE(schema.retireEntity("sensors")) << schema.lastError();
    EXPECT_TRUE(schema.retireEntity("sensors")) << schema.lastError();
    EXPECT_TRUE(schema.retireEntity("never_existed")) << schema.lastError();
}

TEST_F(RetireEntityTest, WorksBeforeInitialize)
{
    SchemaManager schema(db);
    EXPECT_TRUE(schema.retireEntity("sensors")) << schema.lastError();
}

TEST_F(RetireEntityTest, EntityWithTheSameNameCanBeCreatedAgain)
{
    SchemaManager schema(db);
    ASSERT_TRUE(schema.initialize());
    ASSERT_TRUE(schema.syncEntity<Sensor>());

    // What the application used to do by hand: drop only the table.
    ASSERT_TRUE(db->execute("DROP TABLE sensors"));
    EXPECT_FALSE(schema.syncEntity<Sensor>()) << "the stale record should block recreation";

    ASSERT_TRUE(schema.retireEntity("sensors")) << schema.lastError();
    EXPECT_TRUE(schema.syncEntity<Sensor>()) << schema.lastError();
    EXPECT_TRUE(tableExists("sensors"));
    EXPECT_EQ(schema.getTableVersion("sensors"), 2);
}

TEST_F(RetireEntityTest, DropTableAlsoForgetsTheRecords)
{
    SchemaManager schema(db);
    ASSERT_TRUE(schema.initialize());
    ASSERT_TRUE(schema.syncEntity<Sensor>());

    ASSERT_TRUE(schema.dropTable<Sensor>()) << schema.lastError();
    EXPECT_EQ(records("sensors"), 0);
    EXPECT_TRUE(schema.syncEntity<Sensor>()) << schema.lastError();
}

TEST_F(RetireEntityTest, InvalidNamesAreRefusedBeforeAnySql)
{
    SchemaManager schema(db);
    ASSERT_TRUE(schema.initialize());
    ASSERT_TRUE(schema.syncEntity<Sensor>());

    for (const char* name : {"", "sensors; DROP TABLE x", "sensors\"", "main.sensors", "1abc",
                             "sen sors", "__schema_migrations", "__anything", "sqlite_master",
                             "SQLITE_sequence", "sensors--"}) {
        EXPECT_FALSE(schema.retireEntity(name)) << name;
        EXPECT_NE(schema.lastError().find("invalid table name"), std::string::npos) << schema.lastError();
    }

    EXPECT_TRUE(tableExists("sensors"));
    EXPECT_TRUE(tableExists("__schema_migrations"));
    EXPECT_EQ(records("sensors"), 1);
}

TEST_F(RetireEntityTest, FailureHalfwayRollsBackTheDrop)
{
    {
        SchemaManager schema(db);
        ASSERT_TRUE(schema.initialize());
        ASSERT_TRUE(schema.syncEntity<Sensor>());
        ASSERT_TRUE(db->execute("INSERT INTO sensors (id, name) VALUES ('s1', 'temp')"));
    }

    auto failing = std::make_shared<FailOn>(db, "DELETE FROM __schema_migrations");
    SchemaManager schema(failing);

    EXPECT_FALSE(schema.retireEntity("sensors"));
    EXPECT_NE(schema.lastError().find("could not forget the migrations"), std::string::npos)
        << schema.lastError();
    EXPECT_NE(schema.lastError().find("injected failure"), std::string::npos) << schema.lastError();

    // The DROP ran and was undone: table, data and record are all back.
    EXPECT_TRUE(tableExists("sensors"));
    EXPECT_EQ(db->query("SELECT name FROM sensors").size(), 1u);
    EXPECT_EQ(records("sensors"), 1);
}
