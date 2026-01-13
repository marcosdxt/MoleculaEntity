#pragma once

#include <string>
#include <optional>
#include <vector>

namespace MoleculaEntity {

enum class ColumnType {
    Int,
    Int64,
    Double,
    String,
    Bool,
    Blob,
    Timestamp
};

struct ForeignKey {
    std::string table;
    std::string column{"id"};
    std::string onDelete{"NO ACTION"};
    std::string onUpdate{"NO ACTION"};

    [[nodiscard]] std::string toSql(const std::string& columnName) const {
        return "FOREIGN KEY (" + columnName + ") REFERENCES " + table + "(" + column + ")"
               + " ON DELETE " + onDelete + " ON UPDATE " + onUpdate;
    }
};

struct ColumnMeta {
    std::string name;
    ColumnType type;
    bool isNullable{false};
    std::optional<std::string> defaultValue;
    std::optional<ForeignKey> fk;
    bool isUnique{false};

    // Builder pattern factory methods
    static ColumnMeta integer(const std::string& name) {
        return ColumnMeta{name, ColumnType::Int64, false, std::nullopt, std::nullopt, false};
    }

    static ColumnMeta bigint(const std::string& name) {
        return ColumnMeta{name, ColumnType::Int64, false, std::nullopt, std::nullopt, false};
    }

    static ColumnMeta text(const std::string& name) {
        return ColumnMeta{name, ColumnType::String, false, std::nullopt, std::nullopt, false};
    }

    static ColumnMeta real(const std::string& name) {
        return ColumnMeta{name, ColumnType::Double, false, std::nullopt, std::nullopt, false};
    }

    static ColumnMeta boolean(const std::string& name) {
        return ColumnMeta{name, ColumnType::Bool, false, std::nullopt, std::nullopt, false};
    }

    static ColumnMeta blob(const std::string& name) {
        return ColumnMeta{name, ColumnType::Blob, false, std::nullopt, std::nullopt, false};
    }

    static ColumnMeta timestamp(const std::string& name) {
        return ColumnMeta{name, ColumnType::Timestamp, false, std::nullopt, std::nullopt, false};
    }

    // Builder methods
    ColumnMeta& nullable() {
        isNullable = true;
        return *this;
    }

    ColumnMeta& notNull() {
        isNullable = false;
        return *this;
    }

    ColumnMeta& unique() {
        isUnique = true;
        return *this;
    }

    ColumnMeta& withDefault(const std::string& value) {
        defaultValue = value;
        return *this;
    }

    ColumnMeta& foreignKey(const std::string& table, const std::string& column = "id",
                           const std::string& onDelete = "NO ACTION",
                           const std::string& onUpdate = "NO ACTION") {
        fk = ForeignKey{table, column, onDelete, onUpdate};
        return *this;
    }

    [[nodiscard]] std::string sqlType() const {
        switch (type) {
            case ColumnType::Int:
            case ColumnType::Int64:
            case ColumnType::Bool:
                return "INTEGER";
            case ColumnType::Double:
                return "REAL";
            case ColumnType::String:
                return "TEXT";
            case ColumnType::Blob:
                return "BLOB";
            case ColumnType::Timestamp:
                return "TIMESTAMP";
        }
        return "TEXT";
    }

    [[nodiscard]] std::string toSql() const {
        std::string sql = name + " " + sqlType();

        if (!isNullable) {
            sql += " NOT NULL";
        }

        if (isUnique) {
            sql += " UNIQUE";
        }

        if (defaultValue.has_value()) {
            sql += " DEFAULT " + defaultValue.value();
        }

        return sql;
    }
};

struct IndexMeta {
    std::vector<std::string> columns;
    bool unique{false};
    std::string name;

    [[nodiscard]] std::string toSql(const std::string& tableName) const {
        std::string indexName = name.empty()
            ? "idx_" + tableName + "_" + columns[0]
            : name;

        std::string sql = unique ? "CREATE UNIQUE INDEX IF NOT EXISTS " : "CREATE INDEX IF NOT EXISTS ";
        sql += indexName + " ON " + tableName + "(";

        for (size_t i = 0; i < columns.size(); ++i) {
            if (i > 0) sql += ", ";
            sql += columns[i];
        }
        sql += ")";

        return sql;
    }
};

struct RelationMeta {
    std::string name;
    std::string entity;
    std::string fkColumn;
};

} // namespace MoleculaEntity
