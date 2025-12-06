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

class IDatabaseManager {
public:
    virtual ~IDatabaseManager() = default;

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
};

using DatabaseManagerPtr = std::shared_ptr<IDatabaseManager>;

} // namespace MoleculaEntity
