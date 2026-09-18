// 01 — The basics, with no generator at all.
//
// One entity, one repository, and the whole cycle: create, read, update, delete.
// Written by hand on purpose: this is the contract the library asks for, and
// it's what the generator in example 02 fills in for you.
//
//     cmake --build build --target example-01 && ./build/examples/example-01

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <cstdio>
#include <string>

using namespace MoleculaEntity;

// ------------------------------------------------------------------ entity ---
// `idx`, `id`, `created_at` and `updated_at` come from BaseEntity and are not
// declared here — see "Base columns" in the README.
//
// Note what is NOT written: copy constructor, move constructor, assignment
// operators. BaseEntity has a virtual destructor, and as long as the derived
// class doesn't declare its own, the compiler generates all four. Declaring
// `~Task() = default;` out of habit would suppress the move constructor.
class Task : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "tasks"; }
    [[nodiscard]] int tableVersion() const override { return 1; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        // {name, type, nullable, primary key, unique, default, fk table, fk column}
        return {
            {"title", ColumnType::Text,    false, false, false, std::nullopt, std::nullopt, std::nullopt},
            {"done",  ColumnType::Boolean, false, false, false, "0",          std::nullopt, std::nullopt},
            {"due",   ColumnType::Text,    true,  false, false, std::nullopt, std::nullopt, std::nullopt},
        };
    }

    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    void setTitle(std::string v) { title_ = std::move(v); }

    [[nodiscard]] bool done() const noexcept { return done_; }
    void setDone(bool v) noexcept { done_ = v; }

    [[nodiscard]] const std::optional<std::string>& due() const noexcept { return due_; }
    void setDue(std::optional<std::string> v) { due_ = std::move(v); }

private:
    std::string title_;
    bool done_ = false;
    std::optional<std::string> due_;
};

// -------------------------------------------------------------- repository ---
// The five functions below are the bridge between the entity and the rows. This
// is the only place where column names and value order have to agree — and it is
// exactly why a generator exists (example 02): that agreement is easy to break in
// a change made three months from now.
class TaskRepository : public BaseRepository<Task> {
public:
    explicit TaskRepository(DatabaseManagerPtr db) : BaseRepository<Task>(std::move(db)) {}

    [[nodiscard]] std::vector<Task> pending()
    {
        QueryBuilder qb;
        qb.where("done", CompareOp::Equals, int64_t{0})
          .orderBy("due");
        return find(qb);
    }

protected:
    [[nodiscard]] std::vector<std::string> getInsertColumns(const Task&) const override
    {
        return {"id", "title", "done", "due"};
    }

    [[nodiscard]] std::vector<DbValue> getInsertValues(const Task& t) const override
    {
        return {t.getId(), t.title(), int64_t{t.done()},
                t.due().has_value() ? DbValue{*t.due()} : DbValue{nullptr}};
    }

    // No `id` here: the uuid is identity, not data. Changing it in an UPDATE would
    // make the row stop being the same thing to whoever references it elsewhere.
    [[nodiscard]] std::vector<std::string> getUpdateColumns(const Task&) const override
    {
        return {"title", "done", "due"};
    }

    [[nodiscard]] std::vector<DbValue> getUpdateValues(const Task& t) const override
    {
        return {t.title(), int64_t{t.done()},
                t.due().has_value() ? DbValue{*t.due()} : DbValue{nullptr}};
    }

    // The column order here is the one `SELECT *` returns, which follows the
    // CREATE TABLE: the four base columns first, then yours.
    [[nodiscard]] Task mapRowToEntity(const DbRow& row) const override
    {
        Task t;
        t.setIdx(getInt64Value(row[0]));
        t.setId(getStringValue(row[1]));
        if (auto ts = parseTimestamp(row[2])) { t.setCreatedAt(*ts); }
        if (auto ts = parseTimestamp(row[3])) { t.setUpdatedAt(*ts); }
        t.setTitle(getStringValue(row[4]));
        t.setDone(getInt64Value(row[5]) != 0);
        t.setDue(isNull(row[6]) ? std::nullopt : std::optional{getStringValue(row[6])});
        return t;
    }
};

int main()
{
    // `:memory:` so the example leaves no file behind. In production this would be
    // a path, and then the driver options start to matter — see example 05.
    auto db = SQLite3DatabaseManager::open(":memory:");
    if (!db) {
        std::fprintf(stderr, "could not open the database\n");
        return 1;
    }

    // Both return `bool`, and the return is [[nodiscard]]: a schema that didn't
    // come up is not a detail — it's everything after this running against a table
    // that doesn't exist, or that has the wrong shape.
    SchemaManager schema(db);
    if (!schema.initialize() || !schema.syncEntity<Task>()) {
        std::fprintf(stderr, "schema: %s\n", schema.lastError().c_str());
        return 1;
    }

    TaskRepository tasks(db);

    // ------------------------------------------------------------- create ---
    Task buy;
    buy.setTitle("Buy coffee");
    buy.setDue("2026-09-20");
    buy = tasks.save(buy);   // save gives back the row as it ended up

    Task review;
    review.setTitle("Review the PR");
    review = tasks.save(review);

    std::printf("created: idx=%lld and idx=%lld\n",
                static_cast<long long>(*buy.getIdx()),
                static_cast<long long>(*review.getIdx()));
    std::printf("the uuid came out of save ready: %s\n", buy.getId().c_str());

    // --------------------------------------------------------------- read ---
    for (const auto& t : tasks.pending()) {
        std::printf("  [ ] %s (due: %s)\n", t.title().c_str(),
                    t.due() ? t.due()->c_str() : "no date");
    }

    // ------------------------------------------------------------- update ---
    buy.setDone(true);
    buy = tasks.save(buy);   // it has an idx, so save becomes an UPDATE

    std::printf("pending now: %lld\n", static_cast<long long>(tasks.pending().size()));

    // ------------------------------------------------------------- delete ---
    tasks.remove(review);
    std::printf("total at the end: %lld\n", static_cast<long long>(tasks.count()));

    return 0;
}
