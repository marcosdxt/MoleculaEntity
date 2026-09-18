# Examples

Six programs, from the simplest to what you'll probably need in production. They
all really run — **they are part of the suite**, and `ctest` executes every one of
them.

```sh
cmake -S . -B build
cmake --build build --target examples
ctest --test-dir build -R example --output-on-failure   # run them all

./build/examples/example-01                             # or one at a time
```

| | Example | What it shows |
|---|---|---|
| 01 | [`01-basics`](01-basics/main.cpp) | An entity and a repository **written by hand**, and the full CRUD. This is the contract the library asks for |
| 02 | [`02-generator`](02-generator/) | The same domain as 01, generated from a `schema.toml`. ~130 lines of C++ become 20 of TOML |
| 03 | [`03-queries`](03-queries/) | The whole `QueryBuilder` — equality, `IN`, `LIKE`, `BETWEEN`, null, ordering, pagination, counting — **and where it ends** |
| 04 | [`04-migrations`](04-migrations/main.cpp) | The database already in the field: version 1 installed with data, version 2 adding a column without losing any of it |
| 05 | [`05-errors-and-transactions`](05-errors-and-transactions/main.cpp) | Errors without exceptions, `ok()` vs. an empty vector, a transaction with rollback, and the driver options that matter on disk |
| 06 | [`06-custom-driver`](06-custom-driver/main.cpp) | Implementing `IDatabaseManager`: a decorator that measures every SQL statement. This is how you find out why a screen is slow |

## Where to start

**Never used it:** 01 and then 02, in that order. 01 exists so you can see what
the generator is filling in — without that, 02 looks like magic, and magic is hard
to debug when it breaks.

**Already using it, want better filters:** 03.

**About to put this in a service:** 05 first — that's where the options that
decide whether the database survives a power cut live — then 04.

**Investigating slowness:** 06. Its report shows, for instance, that every
`save()` does one extra `SELECT` to return the row as it ended up:

```
   50 x     645 us  INSERT INTO "products" ("id", "name", "price") VALUES (?, ?, ?)
   50 x     566 us  SELECT * FROM "products" WHERE "idx" = ?
```

## Details that hold for all of them

- **`:memory:` in most.** Only 04 writes a file, because "upgrading a database
  that already exists" makes no sense in memory. It deletes what it created on the
  way out.
- **No example assembles SQL by concatenating text.** Values travel as `?` and
  identifiers travel quoted — including in the raw queries in 03 and 05.
- **Generated code is not in version control.** Examples 02 and 03 ship a
  `schema.toml`; the matching C++ is born in the build directory, during
  compilation.
