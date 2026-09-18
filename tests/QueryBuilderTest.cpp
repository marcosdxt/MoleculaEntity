#include <gtest/gtest.h>
#include <MoleculaEntity/QueryBuilder.hpp>

using namespace MoleculaEntity;

class QueryBuilderTest : public ::testing::Test {
protected:
    QueryBuilder qb;

    void SetUp() override {
        qb.clear();
    }
};

TEST_F(QueryBuilderTest, EmptyQueryBuilder) {
    EXPECT_EQ(qb.buildWhereClause(), "");
    EXPECT_EQ(qb.buildOrderClause(), "");
    EXPECT_EQ(qb.buildLimitClause(), "");
    EXPECT_TRUE(qb.getParams().empty());
}

TEST_F(QueryBuilderTest, WhereEquals) {
    qb.where("name", CompareOp::Equals, std::string("John"));

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"name\" = ?");
    EXPECT_EQ(qb.getParams().size(), 1);
    EXPECT_EQ(std::get<std::string>(qb.getParams()[0]), "John");
}

TEST_F(QueryBuilderTest, WhereNotEquals) {
    qb.where("status", CompareOp::NotEquals, std::string("deleted"));

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"status\" != ?");
}

TEST_F(QueryBuilderTest, WhereGreaterThan) {
    qb.where("age", CompareOp::GreaterThan, static_cast<int64_t>(18));

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"age\" > ?");
    EXPECT_EQ(std::get<int64_t>(qb.getParams()[0]), 18);
}

TEST_F(QueryBuilderTest, WhereGreaterThanOrEquals) {
    qb.where("age", CompareOp::GreaterThanOrEquals, static_cast<int64_t>(21));

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"age\" >= ?");
}

TEST_F(QueryBuilderTest, WhereLessThan) {
    qb.where("price", CompareOp::LessThan, 100.0);

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"price\" < ?");
    EXPECT_EQ(std::get<double>(qb.getParams()[0]), 100.0);
}

TEST_F(QueryBuilderTest, WhereLessThanOrEquals) {
    qb.where("quantity", CompareOp::LessThanOrEquals, static_cast<int64_t>(10));

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"quantity\" <= ?");
}

TEST_F(QueryBuilderTest, WhereLike) {
    qb.where("email", CompareOp::Like, std::string("%@gmail.com"));

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"email\" LIKE ?");
}

TEST_F(QueryBuilderTest, WhereNotLike) {
    qb.where("name", CompareOp::NotLike, std::string("Test%"));

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"name\" NOT LIKE ?");
}

TEST_F(QueryBuilderTest, WhereIsNull) {
    qb.whereNull("deleted_at");

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"deleted_at\" IS NULL");
    EXPECT_TRUE(qb.getParams().empty());
}

TEST_F(QueryBuilderTest, WhereIsNotNull) {
    qb.whereNotNull("verified_at");

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"verified_at\" IS NOT NULL");
    EXPECT_TRUE(qb.getParams().empty());
}

TEST_F(QueryBuilderTest, WhereBetween) {
    qb.whereBetween("age", static_cast<int64_t>(18), static_cast<int64_t>(65));

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"age\" BETWEEN ? AND ?");
    EXPECT_EQ(qb.getParams().size(), 2);
    EXPECT_EQ(std::get<int64_t>(qb.getParams()[0]), 18);
    EXPECT_EQ(std::get<int64_t>(qb.getParams()[1]), 65);
}

TEST_F(QueryBuilderTest, WhereIn) {
    std::vector<DbValue> values = {std::string("pending"), std::string("approved"), std::string("processing")};
    qb.whereIn("status", values);

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"status\" IN (?, ?, ?)");
    EXPECT_EQ(qb.getParams().size(), 3);
}

TEST_F(QueryBuilderTest, WhereNotIn) {
    std::vector<DbValue> values = {static_cast<int64_t>(1), static_cast<int64_t>(2), static_cast<int64_t>(3)};
    qb.whereNotIn("category_id", values);

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"category_id\" NOT IN (?, ?, ?)");
}

TEST_F(QueryBuilderTest, MultipleAndConditions) {
    qb.where("active", CompareOp::Equals, static_cast<int64_t>(1))
      .andWhere("age", CompareOp::GreaterThan, static_cast<int64_t>(18))
      .andWhere("country", CompareOp::Equals, std::string("BR"));

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"active\" = ? AND \"age\" > ? AND \"country\" = ?");
    EXPECT_EQ(qb.getParams().size(), 3);
}

TEST_F(QueryBuilderTest, OrConditions) {
    qb.where("status", CompareOp::Equals, std::string("pending"))
      .orWhere("status", CompareOp::Equals, std::string("approved"));

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"status\" = ? OR \"status\" = ?");
}

TEST_F(QueryBuilderTest, MixedAndOrConditions) {
    qb.where("active", CompareOp::Equals, static_cast<int64_t>(1))
      .andWhere("age", CompareOp::GreaterThan, static_cast<int64_t>(18))
      .orWhere("is_admin", CompareOp::Equals, static_cast<int64_t>(1));

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"active\" = ? AND \"age\" > ? OR \"is_admin\" = ?");
}

TEST_F(QueryBuilderTest, OrderByAsc) {
    qb.orderBy("name", OrderDirection::Asc);

    EXPECT_EQ(qb.buildOrderClause(), " ORDER BY \"name\" ASC");
}

TEST_F(QueryBuilderTest, OrderByDesc) {
    qb.orderBy("created_at", OrderDirection::Desc);

    EXPECT_EQ(qb.buildOrderClause(), " ORDER BY \"created_at\" DESC");
}

TEST_F(QueryBuilderTest, MultipleOrderBy) {
    qb.orderBy("last_name", OrderDirection::Asc)
      .orderBy("first_name", OrderDirection::Asc)
      .orderBy("created_at", OrderDirection::Desc);

    EXPECT_EQ(qb.buildOrderClause(), " ORDER BY \"last_name\" ASC, \"first_name\" ASC, \"created_at\" DESC");
}

TEST_F(QueryBuilderTest, Limit) {
    qb.limit(10);

    EXPECT_EQ(qb.buildLimitClause(), " LIMIT 10");
}

TEST_F(QueryBuilderTest, Offset) {
    qb.offset(20);

    // It used to be " OFFSET 20", which SQLite refuses: `OFFSET` only exists
    // attached to a `LIMIT`. The test was guarding the defect instead of catching
    // it.
    EXPECT_EQ(qb.buildLimitClause(), " LIMIT -1 OFFSET 20");
}

TEST_F(QueryBuilderTest, LimitAndOffset) {
    qb.limit(10).offset(20);

    EXPECT_EQ(qb.buildLimitClause(), " LIMIT 10 OFFSET 20");
}

TEST_F(QueryBuilderTest, FullClause) {
    qb.where("active", CompareOp::Equals, static_cast<int64_t>(1))
      .andWhere("age", CompareOp::GreaterThan, static_cast<int64_t>(18))
      .orderBy("name", OrderDirection::Asc)
      .limit(10)
      .offset(0);

    std::string expected = " WHERE \"active\" = ? AND \"age\" > ? ORDER BY \"name\" ASC LIMIT 10 OFFSET 0";
    EXPECT_EQ(qb.buildFullClause(), expected);
}

TEST_F(QueryBuilderTest, Clear) {
    qb.where("name", CompareOp::Equals, std::string("Test"))
      .orderBy("id", OrderDirection::Asc)
      .limit(5);

    qb.clear();

    EXPECT_EQ(qb.buildWhereClause(), "");
    EXPECT_EQ(qb.buildOrderClause(), "");
    EXPECT_EQ(qb.buildLimitClause(), "");
    EXPECT_TRUE(qb.getParams().empty());
}

TEST_F(QueryBuilderTest, GetConditions) {
    qb.where("name", CompareOp::Equals, std::string("Test"))
      .andWhere("active", CompareOp::Equals, static_cast<int64_t>(1));

    auto conditions = qb.getConditions();
    EXPECT_EQ(conditions.size(), 2);
    EXPECT_EQ(conditions[0].column, "name");
    EXPECT_EQ(conditions[1].column, "active");
}

TEST_F(QueryBuilderTest, GetOrderBy) {
    qb.orderBy("name", OrderDirection::Asc)
      .orderBy("created_at", OrderDirection::Desc);

    auto orders = qb.getOrderBy();
    EXPECT_EQ(orders.size(), 2);
    EXPECT_EQ(orders[0].column, "name");
    EXPECT_EQ(orders[0].direction, OrderDirection::Asc);
    EXPECT_EQ(orders[1].column, "created_at");
    EXPECT_EQ(orders[1].direction, OrderDirection::Desc);
}

TEST_F(QueryBuilderTest, GetLimitAndOffset) {
    qb.limit(25).offset(50);

    EXPECT_EQ(qb.getLimit(), 25);
    EXPECT_EQ(qb.getOffset(), 50);
}

TEST_F(QueryBuilderTest, DefaultLimitAndOffset) {
    EXPECT_EQ(qb.getLimit(), -1);
    EXPECT_EQ(qb.getOffset(), -1);
}

TEST_F(QueryBuilderTest, CopyConstruction) {
    qb.where("name", CompareOp::Equals, std::string("Test"))
      .orderBy("id", OrderDirection::Asc)
      .limit(10);

    QueryBuilder copy(qb);

    EXPECT_EQ(copy.buildFullClause(), qb.buildFullClause());
    EXPECT_EQ(copy.getParams().size(), qb.getParams().size());
}

TEST_F(QueryBuilderTest, MoveConstruction) {
    qb.where("name", CompareOp::Equals, std::string("Test"))
      .orderBy("id", OrderDirection::Asc);

    std::string originalClause = qb.buildFullClause();
    QueryBuilder moved(std::move(qb));

    EXPECT_EQ(moved.buildFullClause(), originalClause);
}

TEST_F(QueryBuilderTest, DoubleValue) {
    qb.where("price", CompareOp::LessThanOrEquals, 99.99);

    EXPECT_EQ(qb.buildWhereClause(), " WHERE \"price\" <= ?");
    EXPECT_EQ(std::get<double>(qb.getParams()[0]), 99.99);
}
