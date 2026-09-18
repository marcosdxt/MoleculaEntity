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

    /// False after a statement that failed, until the next statement.
    ///
    /// It exists because `query()` returns rows: a query that fails and a query
    /// that found nothing are both an empty vector, and without this there is no
    /// way to tell "nothing there" from "it didn't run".
    [[nodiscard]] virtual bool ok() const noexcept = 0;

    /// The database's message for the last failure, empty when there wasn't one.
    [[nodiscard]] virtual const std::string& lastError() const noexcept = 0;
};

// NOTE: there used to be an `escapeString` here. It's gone, and the removal is
// deliberate: nothing in the library called it, and what it offered was the path
// to assembling SQL by concatenating text — exactly what this interface's `?`
// and `quoteIdentifier` exist to make unnecessary. A public library's interface
// shouldn't carry the method that exists to use it wrong.

using DatabaseManagerPtr = std::shared_ptr<IDatabaseManager>;

} // namespace MoleculaEntity
