// 04 — Migrations: the database that already exists in the field.
//
// Creating a table in an empty database is easy. The case that hurts is the other
// one: version 1 of your program is installed, it has data in it, and version 2
// needs a new column without losing any of it.
//
// This example simulates exactly that, on a real file (not `:memory:`, or there
// would be no "database that already exists"): it opens, writes, closes, and
// reopens with the entity one version further along.

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace MoleculaEntity;

namespace {

// ----------------------------------------------------------- version 1 of it ---
class NoteV1 : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notes"; }
    [[nodiscard]] int tableVersion() const override { return 1; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {
            {"text", ColumnType::Text, false, false, false, std::nullopt, std::nullopt, std::nullopt},
        };
    }
};

// ------------------------------------ version 2, one release later ------------
// Two things change together, and both are mandatory:
//
//   1. `columns()` now describes the table AS IT SHOULD BE today — that's what
//      matters for anyone installing from scratch.
//   2. `migrations()` says how to get from 1 to 2 — that's what matters for
//      anyone who already has the old table with data in it.
//
// Forget (1) and a fresh install is born without the column; forget (2) and you
// break everyone upgrading. `version` ties the two together.
class NoteV2 : public BaseEntity {
public:
    [[nodiscard]] std::string tableName() const override { return "notes"; }
    [[nodiscard]] int tableVersion() const override { return 2; }

    [[nodiscard]] std::vector<ColumnDefinition> columns() const override
    {
        return {
            {"text",     ColumnType::Text,    false, false, false, std::nullopt, std::nullopt, std::nullopt},
            {"archived", ColumnType::Boolean, false, false, false, "0",          std::nullopt, std::nullopt},
        };
    }

    [[nodiscard]] std::vector<Migration> migrations() const override
    {
        return {
            {2, "archived column",
             "ALTER TABLE notes ADD COLUMN archived INTEGER NOT NULL DEFAULT 0",
             "-- older SQLite has no DROP COLUMN; going back would mean rebuilding the table"},
        };
    }
};

void showColumns(const DatabaseManagerPtr& db)
{
    const auto info = db->query("PRAGMA table_info(notes)");
    std::printf("  columns:");
    for (const auto& row : info) {
        std::printf(" %s", std::get<std::string>(row[1]).c_str());
    }
    std::printf("\n");
}

void showVersions(const DatabaseManagerPtr& db)
{
    const auto rows = db->query(
        "SELECT table_name, version, description FROM __schema_migrations ORDER BY version");
    for (const auto& r : rows) {
        std::printf("  applied: %s v%lld — %s\n",
                    std::get<std::string>(r[0]).c_str(),
                    static_cast<long long>(std::get<int64_t>(r[1])),
                    std::get<std::string>(r[2]).c_str());
    }
}

}  // namespace

int main()
{
    const std::string path = "example-04-migrations.db";
    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());

    // ------------------------------------------- release 1, in the field ---
    {
        auto db = SQLite3DatabaseManager::open(path);
        if (!db) { return 1; }

        SchemaManager schema(db);
        if (!schema.initialize() || !schema.syncEntity<NoteV1>()) {
            std::fprintf(stderr, "schema v1: %s\n", schema.lastError().c_str());
            return 1;
        }

        db->execute("INSERT INTO notes (id, text) VALUES (?, ?)",
                    {std::string{"note-1"}, std::string{"buy bread"}});
        db->execute("INSERT INTO notes (id, text) VALUES (?, ?)",
                    {std::string{"note-2"}, std::string{"call the notary"}});

        std::printf("release 1 installed\n");
        showColumns(db);
    }

    // ------------------------------------ release 2, upgrading in the field ---
    {
        auto db = SQLite3DatabaseManager::open(path);
        if (!db) { return 1; }

        SchemaManager schema(db);
        if (!schema.initialize()) { return 1; }

        // The same call as always. It creates the table when it doesn't exist and
        // applies the missing migrations when it does — all in one transaction.
        //
        // And the return value MATTERS: if the ALTER TABLE fails, nothing is
        // applied and nothing is recorded, so the next startup tries again.
        // Ignoring this `bool` was the defect an earlier version had — the
        // database would claim to be at a version it wasn't, and never retry.
        if (!schema.syncEntity<NoteV2>()) {
            std::fprintf(stderr, "migration: %s\n", schema.lastError().c_str());
            return 1;
        }

        std::printf("release 2 applied\n");
        showColumns(db);
        showVersions(db);

        const auto rows = db->query("SELECT text, archived FROM notes ORDER BY idx");
        std::printf("  the old data is still there:\n");
        for (const auto& r : rows) {
            std::printf("    %s (archived=%lld)\n",
                        std::get<std::string>(r[0]).c_str(),
                        static_cast<long long>(std::get<int64_t>(r[1])));
        }
    }

    // --------------------------- running it again must do nothing at all ---
    // Idempotence matters: the program starts many times, and sync runs every time.
    {
        auto db = SQLite3DatabaseManager::open(path);
        SchemaManager schema(db);
        if (!schema.initialize() || !schema.syncEntity<NoteV2>()) { return 1; }

        const auto applied = db->query("SELECT COUNT(*) FROM __schema_migrations");
        const auto total = std::get<int64_t>(applied[0][0]);
        std::printf("third startup: %lld migration%s recorded — nothing duplicated\n",
                    static_cast<long long>(total), total == 1 ? "" : "s");
    }

    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
    return 0;
}
