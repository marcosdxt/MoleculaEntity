#include <gtest/gtest.h>

#include <MoleculaEntity/Identifier.hpp>
#include <MoleculaEntity/QueryBuilder.hpp>

using namespace MoleculaEntity;

TEST(Identifier, QuotesPlainName)
{
    EXPECT_EQ(quoteIdentifier("name"), "\"name\"");
}

TEST(Identifier, DoublesEmbeddedQuotes)
{
    // This is what stops anyone closing the quotes and carrying on writing SQL.
    EXPECT_EQ(quoteIdentifier("a\"b"), "\"a\"\"b\"");
}

TEST(Identifier, QuotesEachPartOfQualifiedName)
{
    // "order"."total", not "order.total" — which would be a single column with a
    // dot in its name.
    EXPECT_EQ(quoteIdentifier("order.total"), "\"order\".\"total\"");
}

TEST(Identifier, ReservedWordSurvives)
{
    // `order` unquoted is a syntax error; quoted, it is a column like any other.
    EXPECT_EQ(quoteIdentifier("order"), "\"order\"");
}

// The defect the quoting exists to close: a column name arriving from outside
// (an ordering chosen in the UI, a filter from configuration) went into the SQL
// raw, and the library had an injection path nobody looked at because "the values
// are parameterized".
TEST(Identifier, ColumnNameCannotEscapeIntoSql)
{
    QueryBuilder qb;
    qb.where("name = 'x' OR 1=1 --", CompareOp::Equals, DbValue{std::string("y")});

    // The whole text becomes ONE quoted identifier — including the `--`, which
    // outside the quotes would comment out the rest of the query. What proves this
    // isn't merely cosmetic is
    // RepositoryTest.InjectedColumnNameIsRejectedByTheDatabase: there the database
    // refuses the column instead of returning the whole table.
    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"name = 'x' OR 1=1 --\" = ?");
}
