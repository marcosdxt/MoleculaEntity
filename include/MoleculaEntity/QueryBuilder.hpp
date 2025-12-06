#pragma once

#include "IDatabaseManager.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <memory>
#include <functional>

namespace MoleculaEntity {

enum class CompareOp {
    Equals,
    NotEquals,
    GreaterThan,
    GreaterThanOrEquals,
    LessThan,
    LessThanOrEquals,
    Like,
    NotLike,
    In,
    NotIn,
    IsNull,
    IsNotNull,
    Between
};

enum class LogicalOp {
    And,
    Or
};

enum class OrderDirection {
    Asc,
    Desc
};

struct WhereCondition {
    std::string column;
    CompareOp op;
    std::vector<DbValue> values;
    LogicalOp logicalOp = LogicalOp::And;
};

struct OrderBy {
    std::string column;
    OrderDirection direction;
};

class QueryBuilder {
public:
    QueryBuilder() = default;
    ~QueryBuilder() = default;

    QueryBuilder(const QueryBuilder&) = default;
    QueryBuilder(QueryBuilder&&) noexcept = default;
    QueryBuilder& operator=(const QueryBuilder&) = default;
    QueryBuilder& operator=(QueryBuilder&&) noexcept = default;

    QueryBuilder& where(const std::string& column, CompareOp op, const DbValue& value) {
        conditions_.push_back({column, op, {value}, LogicalOp::And});
        return *this;
    }

    QueryBuilder& where(const std::string& column, CompareOp op, const std::vector<DbValue>& values) {
        conditions_.push_back({column, op, values, LogicalOp::And});
        return *this;
    }

    QueryBuilder& andWhere(const std::string& column, CompareOp op, const DbValue& value) {
        conditions_.push_back({column, op, {value}, LogicalOp::And});
        return *this;
    }

    QueryBuilder& orWhere(const std::string& column, CompareOp op, const DbValue& value) {
        conditions_.push_back({column, op, {value}, LogicalOp::Or});
        return *this;
    }

    QueryBuilder& whereNull(const std::string& column) {
        conditions_.push_back({column, CompareOp::IsNull, {}, LogicalOp::And});
        return *this;
    }

    QueryBuilder& whereNotNull(const std::string& column) {
        conditions_.push_back({column, CompareOp::IsNotNull, {}, LogicalOp::And});
        return *this;
    }

    QueryBuilder& whereBetween(const std::string& column, const DbValue& from, const DbValue& to) {
        conditions_.push_back({column, CompareOp::Between, {from, to}, LogicalOp::And});
        return *this;
    }

    QueryBuilder& whereIn(const std::string& column, const std::vector<DbValue>& values) {
        conditions_.push_back({column, CompareOp::In, values, LogicalOp::And});
        return *this;
    }

    QueryBuilder& whereNotIn(const std::string& column, const std::vector<DbValue>& values) {
        conditions_.push_back({column, CompareOp::NotIn, values, LogicalOp::And});
        return *this;
    }

    QueryBuilder& orderBy(const std::string& column, OrderDirection direction = OrderDirection::Asc) {
        orderBy_.push_back({column, direction});
        return *this;
    }

    QueryBuilder& limit(int count) {
        limit_ = count;
        return *this;
    }

    QueryBuilder& offset(int count) {
        offset_ = count;
        return *this;
    }

    QueryBuilder& clear() {
        conditions_.clear();
        orderBy_.clear();
        limit_ = -1;
        offset_ = -1;
        return *this;
    }

    [[nodiscard]] std::string buildWhereClause() const {
        if (conditions_.empty()) {
            return "";
        }

        std::ostringstream ss;
        ss << " WHERE ";

        for (size_t i = 0; i < conditions_.size(); ++i) {
            const auto& cond = conditions_[i];

            if (i > 0) {
                ss << (cond.logicalOp == LogicalOp::And ? " AND " : " OR ");
            }

            ss << cond.column << " " << opToString(cond.op);

            if (cond.op == CompareOp::IsNull || cond.op == CompareOp::IsNotNull) {
                // No value needed
            } else if (cond.op == CompareOp::In || cond.op == CompareOp::NotIn) {
                ss << " (";
                for (size_t j = 0; j < cond.values.size(); ++j) {
                    if (j > 0) ss << ", ";
                    ss << "?";
                }
                ss << ")";
            } else if (cond.op == CompareOp::Between) {
                ss << " ? AND ?";
            } else {
                ss << " ?";
            }
        }

        return ss.str();
    }

    [[nodiscard]] std::string buildOrderClause() const {
        if (orderBy_.empty()) {
            return "";
        }

        std::ostringstream ss;
        ss << " ORDER BY ";

        for (size_t i = 0; i < orderBy_.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << orderBy_[i].column;
            ss << (orderBy_[i].direction == OrderDirection::Asc ? " ASC" : " DESC");
        }

        return ss.str();
    }

    [[nodiscard]] std::string buildLimitClause() const {
        std::ostringstream ss;

        if (limit_ >= 0) {
            ss << " LIMIT " << limit_;
        }

        if (offset_ >= 0) {
            ss << " OFFSET " << offset_;
        }

        return ss.str();
    }

    [[nodiscard]] std::string buildFullClause() const {
        return buildWhereClause() + buildOrderClause() + buildLimitClause();
    }

    [[nodiscard]] std::vector<DbValue> getParams() const {
        std::vector<DbValue> params;

        for (const auto& cond : conditions_) {
            if (cond.op != CompareOp::IsNull && cond.op != CompareOp::IsNotNull) {
                for (const auto& val : cond.values) {
                    params.push_back(val);
                }
            }
        }

        return params;
    }

    [[nodiscard]] const std::vector<WhereCondition>& getConditions() const noexcept {
        return conditions_;
    }

    [[nodiscard]] const std::vector<OrderBy>& getOrderBy() const noexcept {
        return orderBy_;
    }

    [[nodiscard]] int getLimit() const noexcept { return limit_; }
    [[nodiscard]] int getOffset() const noexcept { return offset_; }

private:
    [[nodiscard]] static std::string opToString(CompareOp op) {
        switch (op) {
            case CompareOp::Equals: return "=";
            case CompareOp::NotEquals: return "!=";
            case CompareOp::GreaterThan: return ">";
            case CompareOp::GreaterThanOrEquals: return ">=";
            case CompareOp::LessThan: return "<";
            case CompareOp::LessThanOrEquals: return "<=";
            case CompareOp::Like: return "LIKE";
            case CompareOp::NotLike: return "NOT LIKE";
            case CompareOp::In: return "IN";
            case CompareOp::NotIn: return "NOT IN";
            case CompareOp::IsNull: return "IS NULL";
            case CompareOp::IsNotNull: return "IS NOT NULL";
            case CompareOp::Between: return "BETWEEN";
            default: return "=";
        }
    }

    std::vector<WhereCondition> conditions_;
    std::vector<OrderBy> orderBy_;
    int limit_ = -1;
    int offset_ = -1;
};

} // namespace MoleculaEntity
