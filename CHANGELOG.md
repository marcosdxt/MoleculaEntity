# Changelog

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versions follow [SemVer](https://semver.org/) — in `0.x`, the API can change
between minor versions.

## [0.2.0] — 2026-09-29

### Changed — behaviour change, read before upgrading

- **`execute()` and `query()` refuse SQL with more than one statement.** They
  return `false` / an empty result with `ok() == false` and `lastError()` =
  `execute: the SQL has more than one statement; use executeScript()` (or
  `query: ...`), and **nothing runs** — the refusal happens before the first
  step. A trailing `;`, whitespace and `--` / `/* */` comments are still accepted.
- **`IDatabaseManager` gained a pure virtual `executeScript()`.** A custom driver
  or mock has to implement it (a decorator forwards it, as in example 06).

### Fixed

- **Everything after the first statement was dropped in silence.**
  `sqlite3_prepare_v2` was called with a null tail, so SQLite compiled the first
  statement and the rest of the text was discarded — and `execute` returned
  `true`. A migration with several statements applied only the first one and was
  recorded as applied; on devices in the field the schema stayed behind, and no
  later startup would retry it.
- **Migrations with several statements now apply all of them.** `SchemaManager`
  runs each migration's `up` through `executeScript()`, inside the migration
  transaction: all statements are applied, or none.
- **`dropTable<T>()` left the table's records in `__schema_migrations`**, so the
  next `syncEntity<T>()` failed on `UNIQUE(table_name, version)`. It now does
  what `retireEntity()` does.
- **SQL with no statement at all** (only whitespace or comments) made `execute`
  return `false` with no error set; it now says `the SQL has no statement`.

### Added

- **`bool executeScript(const std::string& sql)`** on `IDatabaseManager` and in
  `SQLite3DatabaseManager`: runs every statement in order, the way `sqlite3_exec`
  does (SQLite does the splitting, so a `;` inside a string literal or comment is
  not a boundary), no parameters, stops at the first failure with `lastError()`
  naming the statement. It opens no transaction of its own.
- **`bool SchemaManager::retireEntity(const std::string& tableName)`**: retires an
  entity that left the schema — `DROP TABLE IF EXISTS` plus deleting its records
  from `__schema_migrations`, in one transaction. Idempotent; the name must be a
  plain identifier (`[A-Za-z_][A-Za-z0-9_]*`, not `sqlite_*` / `__*`) and is
  checked before any SQL runs. Applications no longer need to touch the
  library's bookkeeping table — and shouldn't: dropping only the table left
  records that made `syncAll()` fail when an entity with that name came back.

### Migrating from 0.1.0

`execute` now refuses several statements; use `executeScript`. Anywhere you
passed more than one statement to `execute()` — it only ever ran the first —
switch to `executeScript()`, and check whether the statements that were being
dropped need to be applied now. Migrations declared in the schema need no change:
they already go through `executeScript()`. Custom `IDatabaseManager`
implementations must add `executeScript()`. Code that dropped a table and
deleted from `__schema_migrations` by hand should call `retireEntity()` instead.

## [0.1.0] — 2026-09-17

The first publishable version. The code already existed; what this release does is
make it usable by someone who didn't write it — and fix defects that only show up
away from the bench of the person who did.

### Fixed

- **Timestamps came back shifted by the machine's timezone.** SQLite's
  `CURRENT_TIMESTAMP` writes UTC; the read used `std::mktime`, which interprets
  local time. On a host at UTC−3, every `created_at`/`updated_at` travelled three
  hours into the past. The conversion is now UTC on both ends and never consults
  the system timezone (`MoleculaEntity/Time.hpp`), and the suite runs in a non-UTC
  timezone on CI — before, the defect was invisible precisely because CI runs in
  UTC.
- **A failed migration was recorded as applied, and the transaction committed.**
  `runMigrations` only handled exceptions, and the driver reports failure by
  returning `false` — so the `execute` return was ignored, the version went into
  `__schema_migrations`, and the `commit` happened. The database went on claiming
  a schema it didn't have, and the next startup, seeing the new version, never
  tried again: the column never arrived, in silence. Every return value is now
  checked, a failure rolls back the whole transaction (including earlier steps of
  the same run) and nothing is recorded.
- **A declared version with no migration reaching it** passed as success while
  doing nothing. It's now an error, naming the gap.
- **`sqlite3_bind_*` return values were ignored.** To SQLite, a `?` nobody bound
  is `NULL`: a statement with one value missing ran, and ran wrong — an `UPDATE`
  would become "clear the column". The number of values is now checked against the
  number of `?` before executing.
- **`offset()` without `limit()` produced invalid SQL.** `OFFSET` on its own is a
  syntax error in SQLite; it now emits `LIMIT -1 OFFSET n`.
- **A data race in the uuid generator.** The `std::mt19937_64` engine was `static`
  and unguarded: two threads generating an id at the same time was undefined
  behaviour, and in practice could repeat an id — which is a unique column. It's
  now `thread_local`, confirmed under ThreadSanitizer.
- **Table and column names went into the SQL raw.** Values were always
  parameterized, but identifiers were concatenated: a name coming from
  configuration or from the UI escaped into the SQL. They are now quoted and
  escaped (`MoleculaEntity/Identifier.hpp`), which as a bonus makes a column named
  after a reserved word (`order`, `group`) work.
- **A fresh clone didn't build.** `GeneratorTest` included headers the generator
  produces and `.gitignore` ignores, and nothing invoked the generator. CMake now
  runs it during the build.
- **Generated code tripped `-Wunused-parameter`** in the projects that included it.

### Added

- **`SQLite3DatabaseManager` as part of the library.** It was a test fixture, in
  the `::Test` namespace: anyone using the library had to write their own driver.
  It's now the `MoleculaEntity::SQLite3` target, and it ships with configurable
  WAL, `busy_timeout`, `synchronous`, `foreign_keys` and `strictIdentifiers` — with
  the defaults of a service that writes to a real disk, not those of a test.
- **An error model without exceptions** in the driver: `execute` returns `false`,
  and `ok()` and `lastError()` say what happened. `ok()` exists to separate "the
  query failed" from "the query found nothing", which used to be the same empty
  vector.
- `MoleculaEntity::time_utils::parseUtc` / `formatUtc`.
- `LICENSE` (MIT) — without it, nobody could legally use this.
- CI: GCC and Clang, ASan/UBSan/TSan, the suite in `America/Sao_Paulo` and
  `Asia/Tokyo`, and a job that installs the library and consumes it from outside
  through `find_package`.
- `CMakePackageConfigHelpers`: `find_package(MoleculaEntity)` now works.
- 23 new tests, all tied to the defects above — including a suite dedicated to
  failed migrations (`MigrationFailureTest`). Total: 141.
- **`examples/`**: six examples, from hand-written CRUD to a custom
  `IDatabaseManager` that measures every SQL statement. They are executed by
  `ctest` (total: 148) — an example that doesn't compile is worse than no example.
- **`assets/`**: the logo (mark and lockup) and the architecture view as SVG, both
  with a dark-theme variant.

### Changed — breaking

- `SchemaManager::initialize()`, `syncEntity<T>()` and `dropTable<T>()` return
  `[[nodiscard]] bool` instead of `void`; `tableExists()` returns
  `std::optional<bool>` and `getTableVersion()` returns `std::optional<int>` —
  empty means "I couldn't ask the database", which used to be indistinguishable
  from "it doesn't exist" and from "version zero". There is a `lastError()`.
- The generated `DatabaseBootstrap`: `syncAll()` returns `bool` and gained a
  `lastError()`; the constructor no longer calls `initialize()` — a constructor is
  no place for an operation that can fail when you can't throw.
- `IDatabaseManager` gained `ok()` and `lastError()` (pure virtual) and **lost
  `escapeString()`**. Anyone implementing the interface needs to add the first two;
  `escapeString` went because nothing called it and what it offered was the path to
  assembling SQL by concatenating text — the opposite of what the library does.
- `SQLite3DatabaseManager` is opened through `open()`, which returns null instead
  of throwing, and moved from `MoleculaEntity::Test` to `MoleculaEntity`.
- The assembled SQL now carries quoted identifiers. Tests comparing exact strings
  need updating.
- Project version: was `1.0.0` in CMake, now `0.1.0`. `1.0` is a stability
  promise, and that is made after use.
- CMake targets: `MoleculaEntity::MoleculaEntity` (headers only) and
  `MoleculaEntity::SQLite3` (with the driver). The test option became
  `MOLECULA_BUILD_TESTS`, and examples are built by `MOLECULA_BUILD_EXAMPLES`.
