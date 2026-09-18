#include <gtest/gtest.h>

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <string>

using namespace MoleculaEntity;

namespace {

class NoteV1 : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notes"; }
    [[nodiscard]] int tableVersion() const override { return 1; }
    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {{"text", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt}};
    }
};

// Migration 2 is invalid on purpose. What's being asked of it isn't to fail — it's
// to fail WITHOUT leaving a trace: nothing applied, nothing recorded, and the next
// startup trying again.
class NoteWithBrokenMigration : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notes"; }
    [[nodiscard]] int tableVersion() const override { return 2; }
    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {{"text",   ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt},
                {"author", ColumnType::Text, true,  false, false, std::nullopt, std::nullopt, std::nullopt}};
    }
    [[nodiscard]] std::vector<Migration> migrations() const override
    {
        return {{2, "author", "THIS IS NOT VALID SQL", ""}};
    }
};

// Two steps: the first works, the second doesn't. This is the case that separates
// "checked the return value" from "actually used a transaction" — without a
// rollback, the column from step one stays in the database.
class NoteWithBrokenSecondStep : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notes"; }
    [[nodiscard]] int tableVersion() const override { return 3; }
    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {{"text", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt}};
    }
    [[nodiscard]] std::vector<Migration> migrations() const override
    {
        return {{2, "good column", "ALTER TABLE notes ADD COLUMN author TEXT", ""},
                {3, "bad column",  "ALTER TABLE notes ADD COLUMN author TEXT", ""}};  // repeated: error
    }
};

// A declared version with no migration that reaches it. Not an exotic case: it's
// what happens when someone bumps `version` and forgets to write `migrations()`.
class NoteWithVersionButNoMigration : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notes"; }
    [[nodiscard]] int tableVersion() const override { return 3; }
    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {{"text", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt}};
    }
    // migrations() deliberately empty.
};

class MigrationFailureTest : public ::testing::Test {
protected:
    std::shared_ptr<SQLite3DatabaseManager> db;
    std::unique_ptr<SchemaManager> schema;

    void SetUp() override
    {
        db = SQLite3DatabaseManager::open(":memory:");
        ASSERT_TRUE(db);
        schema = std::make_unique<SchemaManager>(db);
        ASSERT_TRUE(schema->initialize());
        ASSERT_TRUE(schema->syncEntity<NoteV1>());

        ASSERT_TRUE(db->execute("INSERT INTO notes (id, text) VALUES (?, ?)",
                                {std::string{"n1"}, std::string{"the first note"}}));
    }

    [[nodiscard]] int recordedVersion()
    {
        const auto r = db->query("SELECT MAX(version) FROM __schema_migrations WHERE table_name = ?",
                                 {std::string{"notes"}});
        if (r.empty() || std::holds_alternative<std::nullptr_t>(r[0][0])) { return 0; }
        return static_cast<int>(std::get<int64_t>(r[0][0]));
    }

    [[nodiscard]] bool hasColumn(const std::string& name)
    {
        for (const auto& row : db->query("PRAGMA table_info(notes)")) {
            if (std::get<std::string>(row[1]) == name) { return true; }
        }
        return false;
    }
};

}  // namespace

// The defect: the driver reports failure by returning `false`, and runMigrations
// only handled exceptions. The return value was ignored, so the version was
// RECORDED and the transaction COMMITTED even with the migration failing — and the
// next startup saw version 2 in the database and never tried again. The column
// would never arrive, in silence.
TEST_F(MigrationFailureTest, FailedMigrationIsNotRecorded)
{
    EXPECT_FALSE(schema->syncEntity<NoteWithBrokenMigration>());

    EXPECT_EQ(recordedVersion(), 1) << "the version of a failed migration must not be recorded";
    EXPECT_FALSE(hasColumn("author"));
}

TEST_F(MigrationFailureTest, FailureInTheSecondStepRollsBackTheFirst)
{
    EXPECT_FALSE(schema->syncEntity<NoteWithBrokenSecondStep>());

    // Step 2 created the column and step 3 failed: without a real transaction,
    // "author" would stay in the database with the version recorded as 2.
    EXPECT_EQ(recordedVersion(), 1);
    EXPECT_FALSE(hasColumn("author")) << "the step that succeeded had to come back too";
}

TEST_F(MigrationFailureTest, DataSurvivesAFailedMigration)
{
    EXPECT_FALSE(schema->syncEntity<NoteWithBrokenMigration>());

    const auto rows = db->query("SELECT text FROM notes");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(rows[0][0]), "the first note");
}

// A consequence of the one above: since nothing was recorded, the next startup
// tries again — and with the migration fixed, it applies.
TEST_F(MigrationFailureTest, NextBootRetriesAndSucceeds)
{
    EXPECT_FALSE(schema->syncEntity<NoteWithBrokenMigration>());

    struct FixedNote : NoteWithBrokenMigration {
        [[nodiscard]] std::vector<Migration> migrations() const override
        {
            return {{2, "author", "ALTER TABLE notes ADD COLUMN author TEXT", ""}};
        }
    };

    EXPECT_TRUE(schema->syncEntity<FixedNote>());
    EXPECT_EQ(recordedVersion(), 2);
    EXPECT_TRUE(hasColumn("author"));
}

// This used to pass as success and do nothing: the loop found no migration in
// range, the commit happened, and the table stayed in the old shape with the
// recorded version frozen. Every startup repeated the doing-nothing, quietly.
TEST_F(MigrationFailureTest, DeclaredVersionWithoutMigrationIsAnError)
{
    EXPECT_FALSE(schema->syncEntity<NoteWithVersionButNoMigration>());

    EXPECT_EQ(recordedVersion(), 1);
    EXPECT_NE(schema->lastError().find("version 3"), std::string::npos) << schema->lastError();
    EXPECT_NE(schema->lastError().find("migrations()"), std::string::npos) << schema->lastError();
}

// And the message is useful to someone: it says WHICH migration failed, not just
// that something did. Schema diagnostics usually arrive as a field log line, with
// no debugger attached.
TEST_F(MigrationFailureTest, ErrorMessageNamesTheFailedMigration)
{
    EXPECT_FALSE(schema->syncEntity<NoteWithBrokenMigration>());

    EXPECT_NE(schema->lastError().find("migration 2"), std::string::npos) << schema->lastError();
    EXPECT_NE(schema->lastError().find("author"), std::string::npos) << schema->lastError();
}
