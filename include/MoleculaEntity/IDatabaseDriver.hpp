#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <variant>
#include <memory>

namespace MoleculaEntity {

using DbValue = std::variant<std::nullptr_t, int64_t, double, std::string>;
using DbRow = std::vector<DbValue>;
using DbResult = std::vector<DbRow>;

class IDatabaseDriver {
public:
    virtual ~IDatabaseDriver() = default;

    virtual bool execute(const std::string& sql) = 0;
    virtual bool execute(const std::string& sql, const std::vector<DbValue>& params) = 0;
    virtual DbResult query(const std::string& sql) = 0;
    virtual DbResult query(const std::string& sql, const std::vector<DbValue>& params) = 0;
    virtual int64_t lastInsertRowId() = 0;
    virtual int affectedRows() = 0;
    virtual bool beginTransaction() = 0;
    virtual bool commit() = 0;
    virtual bool rollback() = 0;
    virtual std::string escapeString(const std::string& str) = 0;
    virtual bool tableExists(const std::string& tableName) = 0;
};

using DatabaseDriverPtr = std::shared_ptr<IDatabaseDriver>;

// Helper functions for DbValue
inline int64_t getInt64(const DbValue& val) {
    if (std::holds_alternative<int64_t>(val)) {
        return std::get<int64_t>(val);
    }
    if (std::holds_alternative<double>(val)) {
        return static_cast<int64_t>(std::get<double>(val));
    }
    return 0;
}

inline double getDouble(const DbValue& val) {
    if (std::holds_alternative<double>(val)) {
        return std::get<double>(val);
    }
    if (std::holds_alternative<int64_t>(val)) {
        return static_cast<double>(std::get<int64_t>(val));
    }
    return 0.0;
}

inline std::string getString(const DbValue& val) {
    if (std::holds_alternative<std::string>(val)) {
        return std::get<std::string>(val);
    }
    return "";
}

inline bool isNull(const DbValue& val) {
    return std::holds_alternative<std::nullptr_t>(val);
}

inline bool getBool(const DbValue& val) {
    return getInt64(val) != 0;
}

} // namespace MoleculaEntity
