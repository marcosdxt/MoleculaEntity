#include <gtest/gtest.h>

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <string>

using namespace MoleculaEntity;

// `execute` and `query` take ONE statement; `executeScript` takes several.
//
// The defect this guards against: `sqlite3_prepare_v2` compiles the first
// statement and reports where it stopped; with that ignored, everything after the
// first `;` was dropped and `execute` still answered `true`. A migration of three
// statements applied one — on devices in the field, in silence.

namespace {

class StatementBoundaryTest : public ::testing::Test {
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

    [[nodiscard]] bool indexExists(const std::string& name)
    {
        return !db->query("SELECT 1 FROM sqlite_master WHERE type = 'index' AND name = ?", {name})
                    .empty();
    }

    [[nodiscard]] int64_t count(const std::string& table)
    {
        const auto rows = db->query("SELECT COUNT(*) FROM " + table);
        return rows.empty() ? -1 : std::get<int64_t>(rows[0][0]);
    }
};

} // namespace

// ------------------------------------------------------------------ execute ---

TEST_F(StatementBoundaryTest, ExecuteSingleStatement)
{
    EXPECT_TRUE(db->execute("CREATE TABLE t (v TEXT)"));
    EXPECT_TRUE(db->ok());
    EXPECT_TRUE(tableExists("t"));
}

TEST_F(StatementBoundaryTest, ExecuteAcceptsTrailingSemicolonWhitespaceAndComments)
{
    EXPECT_TRUE(db->execute("CREATE TABLE a (v TEXT);")) << db->lastError();
    EXPECT_TRUE(db->execute("CREATE TABLE b (v TEXT) ;  \n\t ;;\n")) << db->lastError();
    EXPECT_TRUE(db->execute("CREATE TABLE c (v TEXT); -- trailing note\n")) << db->lastError();
    EXPECT_TRUE(db->execute("CREATE TABLE d (v TEXT); /* block; with ; inside */ ")) << db->lastError();
    EXPECT_TRUE(db->execute("CREATE TABLE e (v TEXT) -- no newline at the end")) << db->lastError();
    EXPECT_TRUE(db->execute("CREATE TABLE f (v TEXT); /* unterminated")) << db->lastError();

    for (const char* name : {"a", "b", "c", "d", "e", "f"}) {
        EXPECT_TRUE(tableExists(name)) << name;
    }
}

TEST_F(StatementBoundaryTest, ExecuteRefusesTwoStatementsAndRunsNothing)
{
    ASSERT_TRUE(db->execute("CREATE TABLE t (v TEXT)"));

    EXPECT_FALSE(db->execute("INSERT INTO t (v) VALUES ('first'); INSERT INTO t (v) VALUES ('second')"));
    EXPECT_FALSE(db->ok());
    EXPECT_NE(db->lastError().find("more than one statement"), std::string::npos) << db->lastError();
    EXPECT_NE(db->lastError().find("executeScript()"), std::string::npos) << db->lastError();

    // Refused before any step: not even the first statement ran.
    EXPECT_EQ(count("t"), 0);
}

TEST_F(StatementBoundaryTest, ExecuteRefusesSecondStatementAfterAComment)
{
    EXPECT_FALSE(db->execute("CREATE TABLE a (v TEXT); -- note\nCREATE TABLE b (v TEXT)"));
    EXPECT_FALSE(tableExists("a"));
    EXPECT_FALSE(tableExists("b"));
}

TEST_F(StatementBoundaryTest, ExecuteWithParamsRefusesTwoStatements)
{
    ASSERT_TRUE(db->execute("CREATE TABLE t (v TEXT)"));
    EXPECT_FALSE(db->execute("INSERT INTO t (v) VALUES (?); DELETE FROM t", {std::string("x")}));
    EXPECT_NE(db->lastError().find("more than one statement"), std::string::npos) << db->lastError();
    EXPECT_EQ(count("t"), 0);
}

TEST_F(StatementBoundaryTest, SemicolonInsideStringLiteralIsNotABoundary)
{
    ASSERT_TRUE(db->execute("CREATE TABLE t (v TEXT)"));
    EXPECT_TRUE(db->execute("INSERT INTO t (v) VALUES ('a;b')")) << db->lastError();
    EXPECT_TRUE(db->execute("INSERT INTO t (v) VALUES ('c -- d; /* e */');")) << db->lastError();

    const auto rows = db->query("SELECT v FROM t ORDER BY rowid");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(std::get<std::string>(rows[0][0]), "a;b");
    EXPECT_EQ(std::get<std::string>(rows[1][0]), "c -- d; /* e */");
}

TEST_F(StatementBoundaryTest, ExecuteWithNoStatementIsAnErrorWithAMessage)
{
    EXPECT_FALSE(db->execute("  -- only a comment\n"));
    EXPECT_FALSE(db->ok());
    EXPECT_NE(db->lastError().find("no statement"), std::string::npos) << db->lastError();
}

TEST_F(StatementBoundaryTest, TransactionsStillWork)
{
    ASSERT_TRUE(db->execute("CREATE TABLE t (v TEXT)"));
    ASSERT_TRUE(db->beginTransaction());
    ASSERT_TRUE(db->execute("INSERT INTO t (v) VALUES ('x')"));
    ASSERT_TRUE(db->rollback());
    EXPECT_EQ(count("t"), 0);
}

// -------------------------------------------------------------------- query ---

TEST_F(StatementBoundaryTest, QueryRefusesTwoStatements)
{
    ASSERT_TRUE(db->execute("CREATE TABLE t (v TEXT)"));
    ASSERT_TRUE(db->execute("INSERT INTO t (v) VALUES ('x')"));

    const auto rows = db->query("SELECT v FROM t; DELETE FROM t");
    EXPECT_TRUE(rows.empty());
    EXPECT_FALSE(db->ok());
    EXPECT_NE(db->lastError().find("query: the SQL has more than one statement"), std::string::npos)
        << db->lastError();
    EXPECT_EQ(count("t"), 1);
}

TEST_F(StatementBoundaryTest, QueryAcceptsTrailingSemicolon)
{
    const auto rows = db->query("SELECT 42; -- the answer");
    ASSERT_TRUE(db->ok()) << db->lastError();
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(rows[0][0]), 42);
}

// ------------------------------------------------------------ executeScript ---

TEST_F(StatementBoundaryTest, ExecuteScriptAppliesEveryStatement)
{
    EXPECT_TRUE(db->executeScript(R"(
        -- a migration the way people write them
        CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT);
        CREATE INDEX idx_t_v ON t (v);
        /* seed; with a ; in the comment */
        INSERT INTO t (v) VALUES ('a;b');
    )")) << db->lastError();
    EXPECT_TRUE(db->ok());
    EXPECT_TRUE(db->lastError().empty());

    EXPECT_TRUE(tableExists("t"));
    EXPECT_TRUE(indexExists("idx_t_v"));
    const auto rows = db->query("SELECT v FROM t");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(rows[0][0]), "a;b");
}

TEST_F(StatementBoundaryTest, ExecuteScriptStepsStatementsThatReturnRows)
{
    EXPECT_TRUE(db->executeScript("CREATE TABLE t (v TEXT); SELECT 1; INSERT INTO t (v) VALUES ('x')"))
        << db->lastError();
    EXPECT_EQ(count("t"), 1);
}

TEST_F(StatementBoundaryTest, ExecuteScriptStopsAtFirstErrorAndSaysWhere)
{
    EXPECT_FALSE(db->executeScript(
        "CREATE TABLE a (v TEXT);"
        "INSERT INTO no_such_table (v) VALUES ('x');"
        "CREATE TABLE c (v TEXT);"));
    EXPECT_FALSE(db->ok());
    EXPECT_NE(db->lastError().find("statement 2"), std::string::npos) << db->lastError();
    EXPECT_NE(db->lastError().find("no_such_table"), std::string::npos) << db->lastError();

    // No transaction of its own: statement 1 stays, statement 3 never ran.
    EXPECT_TRUE(tableExists("a"));
    EXPECT_FALSE(tableExists("c"));
}

TEST_F(StatementBoundaryTest, ExecuteScriptRuntimeErrorStops)
{
    ASSERT_TRUE(db->execute("CREATE TABLE u (v TEXT UNIQUE)"));
    EXPECT_FALSE(db->executeScript(
        "INSERT INTO u (v) VALUES ('x'); INSERT INTO u (v) VALUES ('x'); INSERT INTO u (v) VALUES ('y')"));
    EXPECT_NE(db->lastError().find("statement 2"), std::string::npos) << db->lastError();
    EXPECT_EQ(count("u"), 1);
}

TEST_F(StatementBoundaryTest, ExecuteScriptInsideCallerTransactionRollsBackEntirely)
{
    ASSERT_TRUE(db->beginTransaction());
    EXPECT_FALSE(db->executeScript("CREATE TABLE a (v TEXT); THIS IS NOT SQL;"));
    ASSERT_TRUE(db->rollback());
    EXPECT_FALSE(tableExists("a"));
}

TEST_F(StatementBoundaryTest, ExecuteScriptWithNothingInItSucceeds)
{
    EXPECT_TRUE(db->executeScript(""));
    EXPECT_TRUE(db->executeScript("  ; -- nothing\n /* at all */ "));
    EXPECT_TRUE(db->ok());
}

// --------------------------------------------------------------- migrations ---

namespace {

class Gauge : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "gauges"; }
    [[nodiscard]] int tableVersion() const override { return 1; }
    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {{"name", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt}};
    }
};

// The case from the field: one migration, three statements.
class GaugeV2 : public Gauge {
public:
    [[nodiscard]] int tableVersion() const override { return 2; }
    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {{"name", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt},
                {"unit", ColumnType::Text, true,  false, false, std::nullopt, std::nullopt, std::nullopt}};
    }
    [[nodiscard]] std::vector<Migration> migrations() const override
    {
        return {{2, "unit + index + backfill",
                 "ALTER TABLE gauges ADD COLUMN unit TEXT;"
                 "CREATE INDEX idx_gauges_unit ON gauges (unit);"
                 "UPDATE gauges SET unit = 'V' WHERE unit IS NULL;",
                 ""}};
    }
};

} // namespace

TEST_F(StatementBoundaryTest, MultiStatementMigrationAppliesEveryStatement)
{
    SchemaManager schema(db);
    ASSERT_TRUE(schema.initialize()) << schema.lastError();
    ASSERT_TRUE(schema.syncEntity<Gauge>()) << schema.lastError();
    ASSERT_TRUE(db->execute("INSERT INTO gauges (id, name) VALUES ('g1', 'supply')")) << db->lastError();

    ASSERT_TRUE(schema.syncEntity<GaugeV2>()) << schema.lastError();
    EXPECT_EQ(schema.getTableVersion("gauges"), 2);

    EXPECT_TRUE(indexExists("idx_gauges_unit"));
    const auto rows = db->query("SELECT unit FROM gauges");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(rows[0][0]), "V");
}
