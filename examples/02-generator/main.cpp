// 02 — The same domain as example 01, through the generator.
//
// Example 01 has ~130 lines of hand-written entity and repository. Here the
// equivalent is the 20 lines of `schema.toml` next door, and the C++ below only
// uses what came out of them.
//
// What the generator emitted (into `generated/`, inside the build directory):
//
//     Entities.hpp           includes everything else — the only one you include
//     DatabaseBootstrap.hpp  syncAll(): creates and migrates every table
//     TaskEntity.hpp         the entity, with typed getters and setters
//     TaskRepository.hpp     the repository, with the methods from the schema
//
// Generated code doesn't belong in version control: it derives from the schema
// the way a .o derives from a .cpp. Committing both guarantees that one day
// they'll disagree.

#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include "Entities.hpp"

#include <cstdio>

int main()
{
    auto db = MoleculaEntity::SQLite3DatabaseManager::open(":memory:");
    if (!db) {
        std::fprintf(stderr, "could not open the database\n");
        return 1;
    }

    // One call instead of initialize() plus a syncEntity<T>() per entity. With ten
    // tables in the schema, it's still one call.
    Agenda::DatabaseBootstrap bootstrap(db);
    if (!bootstrap.syncAll()) {
        std::fprintf(stderr, "schema: %s\n", bootstrap.lastError().c_str());
        return 1;
    }

    Agenda::TaskRepository tasks(db);

    Agenda::TaskEntity buy;
    buy.setTitle("Buy coffee");
    buy.setDue("2026-09-20");
    buy.setPriority(1);
    buy = tasks.save(buy);

    Agenda::TaskEntity review;
    review.setTitle("Review the PR");
    review.setPriority(2);
    review = tasks.save(review);

    Agenda::TaskEntity archive;
    archive.setTitle("Archive notes");
    archive.setPriority(5);
    archive.setDone(true);
    archive = tasks.save(archive);

    // The three methods below came from the [entities.Task.repository] section of
    // the schema — they were never written in C++ anywhere.
    if (auto found = tasks.findByTitle("Buy coffee")) {
        std::printf("found: %s (priority %d)\n",
                    found->getTitle().c_str(), found->getPriority());
    }

    std::printf("pending: %lld\n", static_cast<long long>(tasks.pending().size()));

    std::printf("priority 1..2:\n");
    for (const auto& t : tasks.byPriority(1, 2)) {
        std::printf("  %s\n", t.getTitle().c_str());
    }

    // The generic CRUD is still there: the generator ADDS methods to
    // BaseRepository, it doesn't replace what it already does.
    std::printf("total: %lld\n", static_cast<long long>(tasks.count()));

    return 0;
}
