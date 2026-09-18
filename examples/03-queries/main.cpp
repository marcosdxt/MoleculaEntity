// 03 — Queries: the whole QueryBuilder, and where it ends.
//
// The repository brings `find`, `count` and `exists`; the QueryBuilder is what
// describes the filter. Values always travel as `?` and identifiers always travel
// quoted — you never assemble a SQL string here.

#include <MoleculaEntity/SQLite3DatabaseManager.hpp>

#include "Entities.hpp"

#include <cstdio>

using namespace MoleculaEntity;

namespace {

void show(const char* heading, const std::vector<Bookstore::BookEntity>& books)
{
    std::printf("%s (%zu)\n", heading, books.size());
    for (const auto& b : books) {
        std::printf("  %-28s %-12s $ %6.2f  stock %d\n",
                    b.getTitle().c_str(), b.getCategory().c_str(),
                    b.getPrice(), b.getStock());
    }
}

}  // namespace

int main()
{
    auto db = SQLite3DatabaseManager::open(":memory:");
    if (!db) { return 1; }

    Bookstore::DatabaseBootstrap bootstrap(db);
    if (!bootstrap.syncAll()) {
        std::fprintf(stderr, "schema: %s\n", bootstrap.lastError().c_str());
        return 1;
    }

    Bookstore::BookRepository books(db);

    const struct { const char* title; const char* author; const char* cat; double price; int stock; }
    shelf[] = {
        {"Moby-Dick",            "Herman Melville",  "classic", 32.90,  4},
        {"Don Quixote",          "Miguel Cervantes", "classic", 79.90,  1},
        {"Heart of Darkness",    "Joseph Conrad",    "classic", 41.00,  0},
        {"Clean Architecture",   "Robert Martin",    "tech",   145.00,  7},
        {"Domain-Driven Design", "Eric Evans",       "tech",   210.50,  2},
        {"Refactoring",          "Martin Fowler",    "tech",   189.90,  0},
        {"The Hobbit",           "J. R. R. Tolkien", "fantasy", 54.90, 12},
    };

    for (const auto& item : shelf) {
        Bookstore::BookEntity b;
        b.setTitle(item.title);
        b.setAuthor(item.author);
        b.setCategory(item.cat);
        b.setPrice(item.price);
        b.setStock(item.stock);
        if (item.stock == 0) { b.setSoldOutAt("2026-09-01"); }
        books.save(b);
    }

    // ------------------------------------------------ equality and ranges ---
    {
        QueryBuilder qb;
        qb.where("category", CompareOp::Equals, std::string{"tech"})
          .andWhere("price", CompareOp::LessThan, 200.0)
          .orderBy("price", OrderDirection::Desc);
        show("tech under 200, most expensive first", books.find(qb));
    }

    // -------------------------------------------------------- IN and LIKE ---
    {
        QueryBuilder qb;
        qb.whereIn("category", {std::string{"classic"}, std::string{"fantasy"}})
          .orderBy("title");
        show("classics and fantasy", books.find(qb));
    }
    {
        QueryBuilder qb;
        // The wildcard belongs to SQL, not to the builder: you write it in the value.
        qb.where("author", CompareOp::Like, std::string{"%Martin%"});
        show("author matching \"Martin\"", books.find(qb));
    }

    // ---------------------------------------------------- BETWEEN and NULL ---
    {
        QueryBuilder qb;
        qb.whereBetween("price", 30.0, 60.0).orderBy("price");
        show("between $30 and $60", books.find(qb));
    }
    {
        QueryBuilder qb;
        qb.whereNotNull("sold_out_at");
        show("sold out (column filled in)", books.find(qb));
    }

    // ------------------------------------------------------------ AND/OR ---
    {
        QueryBuilder qb;
        // CAREFUL: there are no parentheses. This is `(category = ? AND stock > ?)
        // OR price > ?`, following SQL precedence. To group it differently, write
        // the SQL and use db->query().
        qb.where("category", CompareOp::Equals, std::string{"classic"})
          .andWhere("stock", CompareOp::GreaterThan, int64_t{0})
          .orWhere("price", CompareOp::GreaterThan, 200.0);
        show("classics in stock, OR anything above 200", books.find(qb));
    }

    // --------------------------------------------------------- pagination ---
    for (int page = 0; page < 3; ++page) {
        QueryBuilder qb;
        qb.orderBy("title").limit(3).offset(page * 3);
        const auto p = books.find(qb);
        std::printf("page %d: %zu items", page + 1, p.size());
        if (!p.empty()) { std::printf(" (starting at \"%s\")", p.front().getTitle().c_str()); }
        std::printf("\n");
    }

    // ------------------------------------------------- counting and exists ---
    {
        QueryBuilder outOfStock;
        outOfStock.where("stock", CompareOp::Equals, int64_t{0});
        std::printf("out of stock: %lld\n", static_cast<long long>(books.count(outOfStock)));

        QueryBuilder expensive;
        expensive.where("price", CompareOp::GreaterThan, 500.0);
        std::printf("anything above 500? %s\n", books.exists(expensive) ? "yes" : "no");
    }

    // -------------------------------------------------------- where it ends ---
    // There is no JOIN in the builder. When the query outgrows what it describes,
    // drop to SQL — the database interface is still reachable, and the values are
    // still parameterized.
    const auto byCategory = db->query(
        "SELECT category, COUNT(*), AVG(price) FROM books "
        "WHERE stock > ? GROUP BY category ORDER BY category", {int64_t{0}});

    if (!db->ok()) {
        std::fprintf(stderr, "query failed: %s\n", db->lastError().c_str());
        return 1;
    }

    std::printf("summary by category (only what's in stock):\n");
    for (const auto& row : byCategory) {
        std::printf("  %-10s %lld titles, average $ %.2f\n",
                    std::get<std::string>(row[0]).c_str(),
                    static_cast<long long>(std::get<int64_t>(row[1])),
                    std::get<double>(row[2]));
    }

    return 0;
}
