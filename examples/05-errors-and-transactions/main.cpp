// 05 — Errors without exceptions, transactions, and the options that matter on disk.
//
// This is the example for anyone about to run this inside a service: what happens
// when the database refuses, how to undo a whole block, and what the driver's
// defaults protect you from.

#include <MoleculaEntity/MoleculaEntity.hpp>
#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace MoleculaEntity;

int main()
{
    const std::string path = "example-05.db";
    std::remove(path.c_str());

    // -------------------------------------------------------- the options ---
    // The defaults are already those of a service that writes to a real disk.
    // They're spelled out here to show what each one avoids:
    SQLite3DatabaseManager::Options options;
    options.walJournal = true;                                 // readers don't block the writer
    options.busyTimeout = std::chrono::seconds(5);             // SQLite's default is ZERO
    options.synchronous = SQLite3DatabaseManager::Options::Synchronous::Full;
    options.foreignKeys = true;                                // SQLite's default is off
    options.strictIdentifiers = true;                          // "text" doesn't silently become a string

    std::string error;
    auto db = SQLite3DatabaseManager::open(path, options, &error);
    if (!db) {
        // Opening returns null instead of throwing. In a service this becomes a log
        // line and a decision — not an unwound stack.
        std::fprintf(stderr, "could not open: %s\n", error.c_str());
        return 1;
    }
    std::printf("opened with WAL and a %lld ms busy timeout\n",
                static_cast<long long>(options.busyTimeout.count()));

    db->execute("CREATE TABLE accounts (idx INTEGER PRIMARY KEY, owner TEXT UNIQUE, balance INTEGER NOT NULL)");
    db->execute("INSERT INTO accounts (owner, balance) VALUES (?, ?)", {std::string{"ana"},   int64_t{100}});
    db->execute("INSERT INTO accounts (owner, balance) VALUES (?, ?)", {std::string{"bruno"}, int64_t{ 50}});

    // ------------------------------------------ an error is not an exception ---
    if (!db->execute("INSERT INTO accounts (owner, balance) VALUES (?, ?)",
                     {std::string{"ana"}, int64_t{999}})) {
        std::printf("refused, as expected: %s\n", db->lastError().c_str());
    }

    // -------------------------------------------- empty != something broke ---
    // Both queries below return zero rows. Only ok() separates "nothing there"
    // from "it didn't run" — that's what it's for.
    const auto noResult = db->query("SELECT * FROM accounts WHERE owner = ?", {std::string{"nobody"}});
    std::printf("valid query, no results: %zu rows, ok=%s\n",
                noResult.size(), db->ok() ? "yes" : "no");

    const auto broken = db->query("SELECT * FROM table_that_does_not_exist");
    std::printf("invalid query:           %zu rows, ok=%s (%s)\n",
                broken.size(), db->ok() ? "yes" : "no", db->lastError().c_str());

    // ---------------------------------------------------------- transaction ---
    // A transfer: either both ends change, or neither does. Without a transaction,
    // a failure in the middle leaves money that vanished.
    const auto transfer = [&db](const char* from, const char* to, int64_t amount) {
        db->beginTransaction();

        const auto balance = db->query("SELECT balance FROM accounts WHERE owner = ?", {std::string{from}});
        if (balance.empty() || std::get<int64_t>(balance[0][0]) < amount) {
            db->rollback();
            std::printf("transfer of %lld refused: not enough balance in %s\n",
                        static_cast<long long>(amount), from);
            return false;
        }

        const bool ok =
            db->execute("UPDATE accounts SET balance = balance - ? WHERE owner = ?", {amount, std::string{from}}) &&
            db->execute("UPDATE accounts SET balance = balance + ? WHERE owner = ?", {amount, std::string{to}});

        if (!ok) {
            db->rollback();
            std::printf("transfer undone: %s\n", db->lastError().c_str());
            return false;
        }

        db->commit();
        std::printf("transferred %lld from %s to %s\n",
                    static_cast<long long>(amount), from, to);
        return true;
    };

    transfer("ana", "bruno", 30);
    transfer("bruno", "ana", 500);   // refused, and the rollback puts everything back

    for (const auto& r : db->query("SELECT owner, balance FROM accounts ORDER BY owner")) {
        std::printf("  %s: %lld\n", std::get<std::string>(r[0]).c_str(),
                    static_cast<long long>(std::get<int64_t>(r[1])));
    }

    // -------------------------------------------------------- foreign keys ---
    // On by default. Without it, SQLite ACCEPTS the orphan row in silence, and the
    // defect only shows up months later, on a read.
    db->execute("CREATE TABLE entries (idx INTEGER PRIMARY KEY, account_idx INTEGER NOT NULL, "
                "amount INTEGER NOT NULL, FOREIGN KEY (account_idx) REFERENCES accounts(idx))");

    if (!db->execute("INSERT INTO entries (account_idx, amount) VALUES (?, ?)",
                     {int64_t{9999}, int64_t{10}})) {
        std::printf("orphan refused by the foreign key: %s\n", db->lastError().c_str());
    }

    // ----------------------------------------------- parameters are counted ---
    // To SQLite, a `?` nobody bound is NULL: a statement with one value missing
    // would run, and run WRONG — this UPDATE would quietly clear the column.
    if (!db->execute("UPDATE accounts SET balance = ? WHERE owner = ?", {int64_t{0}})) {
        std::printf("statement with the wrong parameter count refused: %s\n",
                    db->lastError().c_str());
    }

    std::remove(path.c_str());
    std::remove((path + "-wal").c_str());
    std::remove((path + "-shm").c_str());
    return 0;
}
