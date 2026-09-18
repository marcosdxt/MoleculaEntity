// 06 — Writing an IDatabaseManager: tracing every SQL statement that goes out.
//
// The library doesn't talk to SQLite: it talks to the interface. Replacing or
// wrapping whatever sits behind it is the extension point — and the most useful
// example of that isn't "another database", it's a **decorator**: same driver,
// with measurement.
//
// What comes out of this answers "why is that screen slow" without guessing: how
// many queries, which ones, how long each took.
//
// (If you do implement another database, the contract is the same: these eleven
// methods, values as `?`, and errors without exceptions.)

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace MoleculaEntity;

namespace {

class TracingDriver final : public IDatabaseManager {
public:
    explicit TracingDriver(DatabaseManagerPtr inner, bool print = true)
        : inner_(std::move(inner)), print_(print) {}

    bool execute(const std::string& sql) override
    {
        return measure(sql, [&] { return inner_->execute(sql); });
    }

    bool execute(const std::string& sql, const std::vector<DbValue>& params) override
    {
        return measure(sql, [&] { return inner_->execute(sql, params); });
    }

    DbResult query(const std::string& sql) override
    {
        DbResult r;
        measure(sql, [&] { r = inner_->query(sql); return inner_->ok(); });
        return r;
    }

    DbResult query(const std::string& sql, const std::vector<DbValue>& params) override
    {
        DbResult r;
        measure(sql, [&] { r = inner_->query(sql, params); return inner_->ok(); });
        return r;
    }

    // The rest is pass-through. A decorator that "improves" what doesn't need
    // improving becomes a second driver to maintain.
    int64_t lastInsertRowId() override { return inner_->lastInsertRowId(); }
    int affectedRows() override { return inner_->affectedRows(); }
    bool beginTransaction() override { return inner_->beginTransaction(); }
    bool commit() override { return inner_->commit(); }
    bool rollback() override { return inner_->rollback(); }

    [[nodiscard]] bool ok() const noexcept override { return inner_->ok(); }
    [[nodiscard]] const std::string& lastError() const noexcept override { return inner_->lastError(); }

    struct Measurement {
        int calls = 0;
        std::chrono::microseconds total{0};
    };

    [[nodiscard]] const std::map<std::string, Measurement>& measurements() const noexcept
    {
        return measurements_;
    }

private:
    template <typename Action>
    bool measure(const std::string& sql, Action&& action)
    {
        const auto start = std::chrono::steady_clock::now();
        const bool ok = action();
        const auto took = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start);

        // The key is the SQL, not the call site: the same 200 queries with
        // different values have to add up to a single line, or the report is
        // noise. This only works because values travel as `?` and never inside
        // the text.
        auto& m = measurements_[summarize(sql)];
        ++m.calls;
        m.total += took;

        if (print_) {
            std::printf("    [sql %6lld us] %s%s\n", static_cast<long long>(took.count()),
                        summarize(sql).c_str(), ok ? "" : "  <- FAILED");
        }
        return ok;
    }

    [[nodiscard]] static std::string summarize(const std::string& sql)
    {
        std::string one_line;
        one_line.reserve(sql.size());
        bool space = false;
        for (const char c : sql) {
            if (c == '\n' || c == '\t' || c == ' ') {
                space = true;
                continue;
            }
            if (space && !one_line.empty()) { one_line += ' '; }
            space = false;
            one_line += c;
        }
        return one_line.size() > 72 ? one_line.substr(0, 69) + "..." : one_line;
    }

    DatabaseManagerPtr inner_;
    bool print_;
    std::map<std::string, Measurement> measurements_;
};

class Product : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "products"; }
    [[nodiscard]] int tableVersion() const override { return 1; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {
            {"name",  ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt},
            {"price", ColumnType::Real, false, false, false, std::nullopt, std::nullopt, std::nullopt},
        };
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void setName(std::string v) { name_ = std::move(v); }
    [[nodiscard]] double price() const noexcept { return price_; }
    void setPrice(double v) noexcept { price_ = v; }

private:
    std::string name_;
    double price_ = 0.0;
};

class ProductRepository : public BaseRepository<Product> {
public:
    explicit ProductRepository(DatabaseManagerPtr db) : BaseRepository<Product>(std::move(db)) {}

protected:
    [[nodiscard]] std::vector<std::string> getInsertColumns(const Product&) const override
    {
        return {"id", "name", "price"};
    }
    [[nodiscard]] std::vector<DbValue> getInsertValues(const Product& p) const override
    {
        return {p.getId(), p.name(), p.price()};
    }
    [[nodiscard]] std::vector<std::string> getUpdateColumns(const Product&) const override
    {
        return {"name", "price"};
    }
    [[nodiscard]] std::vector<DbValue> getUpdateValues(const Product& p) const override
    {
        return {p.name(), p.price()};
    }
    [[nodiscard]] Product mapRowToEntity(const DbRow& row) const override
    {
        Product p;
        p.setIdx(getInt64Value(row[0]));
        p.setId(getStringValue(row[1]));
        p.setName(getStringValue(row[4]));
        p.setPrice(getDoubleValue(row[5]));
        return p;
    }
};

}  // namespace

int main()
{
    auto sqlite = SQLite3DatabaseManager::open(":memory:");
    if (!sqlite) { return 1; }

    // The decorator takes the driver's place, and nothing else changes:
    // SchemaManager and the repositories take an IDatabaseManager and never ask
    // which one it is.
    auto tracer = std::make_shared<TracingDriver>(sqlite, /*print=*/false);
    DatabaseManagerPtr db = tracer;

    SchemaManager schema(db);
    if (!schema.initialize() || !schema.syncEntity<Product>()) {
        std::fprintf(stderr, "schema: %s\n", schema.lastError().c_str());
        return 1;
    }

    ProductRepository products(db);

    std::printf("writing 50 products...\n");
    for (int i = 0; i < 50; ++i) {
        Product p;
        p.setName("product " + std::to_string(i));
        p.setPrice(9.90 + i);
        products.save(p);
    }

    QueryBuilder expensive;
    expensive.where("price", CompareOp::GreaterThan, 50.0).orderBy("price", OrderDirection::Desc).limit(5);
    const auto top = products.find(expensive);
    std::printf("five most expensive, starting with %s\n", top.front().name().c_str());

    // -------------------------------------------------------- the report ---
    std::printf("\nSQL by shape, most expensive first:\n");

    std::vector<std::pair<std::string, TracingDriver::Measurement>> rows(
        tracer->measurements().begin(), tracer->measurements().end());
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.second.total > b.second.total; });

    for (const auto& [sql, m] : rows) {
        std::printf("  %5d x %7lld us  %s\n", m.calls,
                    static_cast<long long>(m.total.count()), sql.c_str());
    }

    // `save()` does an INSERT and then reads the row back to return it as it ended
    // up — which is why the SELECT shows up 50 times here. That's the kind of
    // thing you only see by measuring, and it's exactly what this decorator is for.
    return 0;
}
