[![Build Status](https://travis-ci.org/vinzenz/vsqlite--.png?branch=master)](https://travis-ci.org/vinzenz/vsqlite--)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/1976/badge.svg)](https://scan.coverity.com/projects/1976)
[![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/vinzenz/vsqlite--?utm_source=oss&utm_medium=github&utm_campaign=vinzenz%2Fvsqlite--&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)](https://coderabbit.ai)

VSQLite++ - A welldesigned and portable SQLite3 Wrapper for C++
(C) 2006-2014 by virtuosic bytes  - vinzenz.feenstra@gmail.com

Author: Vinzenz Feenstra

License: BSD-3

# Website: http://vsqlite.virtuosic-bytes.com

[![Join the chat at https://gitter.im/vinzenz/vsqlite--](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/vinzenz/vsqlite--?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

# Operating Systems
- Linux
- Windows
- Mac OS X

# Dependencies
- A C++20-capable compiler (GCC 13+, Clang 14+, MSVC 19.30+)
- libsqlite3


Additional notices:

- Please let me know if you want to suggest features
- Contributions are welcome
- Proposals for Design Improvement are welcome

## Building

```bash
cmake -S . -B build -DBUILD_SHARED_LIBS=ON -DVSQLITE_BUILD_EXAMPLES=ON
cmake --build build -j$(nproc)
cmake --build build --target vsqlitepp_example
ctest --test-dir build --output-on-failure
cmake --install build --prefix /usr/local
```

Use `-DVSQLITE_BUILD_EXAMPLES=OFF` on headless build farms and set `-DCMAKE_INSTALL_PREFIX` (or a toolchain file) to match your packaging target. The install step publishes headers, the `vsqlitepp` shared/static library, and the generated `vsqlite::vsqlitepp` package config so downstream projects can `find_package(vsqlitepp CONFIG REQUIRED)`.

## FetchContent and CPM.cmake

`vsqlite++` can be embedded directly in another CMake project because it exports the `vsqlite::vsqlitepp` target and keeps include paths relative to the build tree. When included as a subproject, examples, tests, and install rules default to `OFF`.

With CMake `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
  vsqlitepp
  GIT_REPOSITORY https://github.com/vinzenz/vsqlite--
  GIT_TAG v${VSQLITEPP_VERSION} # or a commit hash
)
FetchContent_MakeAvailable(vsqlitepp)

target_link_libraries(my_app PRIVATE vsqlite::vsqlitepp)
```

With CPM.cmake:

```cmake
CPMAddPackage(
  NAME vsqlitepp
  GITHUB_REPOSITORY vinzenz/vsqlite--
  GIT_TAG v${VSQLITEPP_VERSION} # or a commit hash
)

target_link_libraries(my_app PRIVATE vsqlite::vsqlitepp)
```

By default, `vsqlite++` uses `find_package(SQLite3 REQUIRED)`. If you want SQLite to be fetched and built with the wrapper, set `VSQLITE_BUNDLED_SQLITE=ON` before adding the dependency.

## vcpkg Overlay Port

This repository includes a vcpkg overlay port under `packaging/vcpkg`. Until `vsqlitepp` is available from the curated registry, install it with:

```bash
vcpkg install vsqlitepp --overlay-ports=packaging/vcpkg
```

Then consume it from CMake through the vcpkg toolchain:

```cmake
find_package(vsqlitepp CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE vsqlite::vsqlitepp)
```

## Release Packages

Release tags matching `v*` build and attach native Linux packages to the GitHub release. Each package format is built inside the distribution it targets:

- DEB packages via CPack's DEB generator on Ubuntu 24.04 (`amd64`).
- RPM packages via CPack's RPM generator inside a Fedora 43 container (`x86_64`).
- Arch Linux pacman packages from `packaging/arch/PKGBUILD` in an `archlinux:base-devel` container (`x86_64`).

The RPM is no longer produced on Ubuntu: building it with Fedora's own `rpmbuild` yields target-native install paths (`/usr/lib64`) and dependency names. Its release is dist-tagged (for example `vsqlitepp-0.4.2-1.fc43.x86_64.rpm`) and declares `sqlite-libs` as the runtime dependency, the Fedora package that owns `libsqlite3.so.0`. The automatically generated soname requirements cover the technical dependency; the explicit name documents the intended runtime package.

All packages install under `/usr` and combine runtime and development files in a single package: the `libvsqlitepp` shared library, the public headers, the `vsqlite::vsqlitepp` CMake package config, and documentation. There is no separate runtime/dev split.

Native packages always link against the distribution's external SQLite package (`libsqlite3-0` on Debian and Ubuntu, `sqlite-libs` on Fedora, `sqlite` on Arch) and never ship SQLite themselves. `VSQLITE_BUNDLED_SQLITE=ON` is a source-build option only (plain CMake, FetchContent, CPM.cmake, vcpkg): it installs a private static SQLite archive and headers next to VSQLite++ so such consumers do not depend on a system SQLite.

Before anything is published:

- The DEB artifact is installed with `apt` in a clean `ubuntu:24.04` container so its dependency metadata is resolved, its installed file and dependency lists are printed, a consumer is compiled against the installed development files, the build tree is removed, and the consumer runs against the installed shared library.
- The publish job uploads the artifacts, re-lists the release assets, and fails when an expected asset is missing or has a size of zero.

## Threading & Pooling

Configure SQLite's global threading mode before opening connections:

```cpp
#include <sqlite/threading.hpp>

sqlite::configure_threading(sqlite::threading_mode::serialized);
```

Connections are still not shareable across threads. To fan out work, create a `sqlite::connection_pool` and lease connections when needed:

```cpp
#include <sqlite/connection_pool.hpp>

sqlite::connection_pool pool(
    4,
    sqlite::connection_pool::make_factory("my.db")
);

auto lease = pool.acquire();
sqlite::command cmd(*lease, "INSERT INTO log(msg) VALUES (?);");
cmd % std::string("hello from worker");
cmd.step_once();
```

`connection_pool::lease::shared()` returns an aliasing `std::shared_ptr<sqlite::connection>` for
cases where a connection has to cross an API boundary. If that alias outlives the pool, the
connection is closed normally instead of being returned to a destroyed pool.

## User-Defined SQL Functions

Register portable SQL functions directly from C++ lambdas via `sqlite::create_function` (from `#include <sqlite/function.hpp>`). Arguments map to lambda parameters (including `std::optional<T>` for nullable inputs) while return values are written back automatically:

```cpp
#include <sqlite/function.hpp>

sqlite::create_function(conn, "repeat_text",
    [](std::string_view text, int times) -> std::string {
        auto copies = times < 0 ? 0 : times;
        std::string out;
        out.reserve(text.size() * static_cast<std::size_t>(copies));
        for(int i = 0; i < copies; ++i) {
            out.append(text);
        }
        return out;
    },
    {.deterministic = true}
);
sqlite::query q(conn, "SELECT repeat_text('hi', 3);");
```

The helper enforces type-safe conversions for integers, floating point values, `std::string_view`, `std::span<const std::byte>`/`unsigned char` blobs, and their `std::optional` counterparts. When needed, opt into SQLite flags such as deterministic/direct-only/innocuous through `function_options`. Exceptions thrown inside the callable are surfaced as SQLite errors at query time.

## Type-Safe Binding & Row Materialization

`sqlite::command` now offers `bind_value(idx, value)` and templated `bind/ operator%` overloads that accept `std::optional<T>`, `std::chrono::time_point`, enums, and other PODs without manual conversions. On the read side, `sqlite::result::get<T>` and `get_tuple<Ts...>` deserialize rows directly into strongly typed values (including tuples for structured bindings):

```cpp
#include <sqlite/command.hpp>
#include <sqlite/result.hpp>

sqlite::command insert(conn, "INSERT INTO events(id, happened, note) VALUES (?, ?, ?);");
insert % 1
       % std::chrono::system_clock::now()
       % std::optional<std::string>("hello");
insert.step_once();

sqlite::query q(conn, "SELECT id, happened, note FROM events;");
auto row = q.get_result();
row->next_row();
auto [id, stamp, note] = row->get_tuple<
    std::int64_t,
    std::chrono::system_clock::time_point,
    std::optional<std::string>
>();
```

Bindings use microseconds for chrono values, unwrap `std::optional` automatically (binding `NULL` when empty), and tuple helpers validate column counts to keep mismatches from slipping through at runtime.

Use `command::clear()` when reusing a command for a new set of parameters; it resets the statement
and clears all previous bindings. Use `reset_statement()` only when you intentionally want to keep
existing bindings for another execution.

`query::get_result()` returns a cursor that keeps the prepared statement alive even if the `query`
object is destroyed first. The owning `sqlite::connection` must still outlive active commands and
results. `result::get_column_decltype()` mirrors SQLite and returns an empty string for computed
expressions or other columns where SQLite reports no declared type.

## Optional SQLite Capabilities & Error Reporting

Sessions, snapshots, and serialization are optional SQLite features: whether they exist depends on the SQLite build VSQLite++ links against (bundled SQLite enables all three; system SQLite varies). Query them up front with `sqlite::connection::capabilities()`:

```cpp
#include <sqlite/capabilities.hpp>

sqlite::connection_capabilities caps = conn.capabilities();
if (caps.serialization) {
    auto image = sqlite::serialize(conn);
}
```

The values mirror the detection performed when the wrapper was built and never contradict the `sessions_supported()`, `snapshots_supported()`, and `serialization_supported()` helpers: API groups the build verified report true and resolve their symbols through direct references (no executable export flags needed for static builds); unverifiable groups report whether the runtime lookup in the loaded SQLite module finds them.

Errors reported by these wrappers come in two kinds:

- **Absent build capability** — the linked SQLite build lacks the API group. Operations throw a `sqlite::database_exception` whose message contains "not available in this build", names the capability, and names the SQLite build flags that enable it: `SQLITE_ENABLE_SESSION` plus `SQLITE_ENABLE_PREUPDATE_HOOK` for sessions, `SQLITE_ENABLE_SNAPSHOT` for snapshots, and building without `SQLITE_OMIT_DESERIALIZE` for serialization.
- **Connection-state / operation errors** — the capability exists, but SQLite rejects the operation (for example, capturing a snapshot outside a WAL read transaction, applying a conflicting changeset, or deserializing into an unknown schema). These throw `sqlite::database_exception_code` carrying the SQLite result code.

## Prepared Statements with Explicit Execution State

`connection::prepare()` returns a move-only `sqlite::prepared_statement` whose `execute()` runs a
bound argument set to completion and whose `rows()` returns a cursor over result rows. The
statement tracks whether it is `prepared`, `executing`, `complete`, or `failed`, rejects a second
execution while one of its cursors is live, and snapshots affected-row counts at completion:

```cpp
#include <sqlite/prepared_statement.hpp>

auto insert = db.prepare("INSERT INTO events(message) VALUES (?)");
auto outcome = insert.execute("started"); // affected_rows + last_insert_rowid

auto select = db.prepare("SELECT id, message FROM events WHERE id > ?");
for (auto row : select.rows(last_seen)) {
    auto id = row.get<std::int64_t>(0);
}
```

Every `execute(args...)`/`rows(args...)` call supplies the complete argument set (earlier
bindings are cleared first, and incomplete sets throw before anything runs); manual `bind()`
plus the zero-argument overloads cover the advanced mode. Statements that return rows —
`SELECT` or DML with `RETURNING` — go through `rows()`; `execute()` rejects them with a clear
error. The full behavior specification, the error matrix, and a migration table from
`command`/`query`/`execute` live in [docs/prepared-statement.md](docs/prepared-statement.md).

## Snapshots, WAL & WAL2

The wrapper exposes WAL helpers and snapshot utilities in `#include <sqlite/snapshot.hpp>`. Switch a database into WAL or WAL2 (when supported by your SQLite build) using `sqlite::enable_wal(conn, /*prefer_wal2=*/true);` – the helper automatically falls back to classic WAL if WAL2 is unavailable. Once running in WAL, capture consistent read views via the transaction/savepoint adapters:

```cpp
#include <sqlite/snapshot.hpp>

sqlite::enable_wal(reader, true);
sqlite::transaction tx(reader, sqlite::transaction_type::deferred);
auto snap = tx.take_snapshot();
tx.commit();

sqlite::transaction replay(reader);
replay.open_snapshot(snap); // reads the historical view
```

`sqlite::snapshots_supported()` reports whether the linked SQLite library exposes `sqlite3_snapshot_*` APIs (they require `SQLITE_ENABLE_SNAPSHOT`). Savepoints gain identical helpers so you can scope replayed snapshots to subtransactions.

## Session & Changesets

When SQLite is built with `SQLITE_ENABLE_SESSION`, `#include <sqlite/session.hpp>` unlocks RAII wrappers for `sqlite3_session` so you can capture and ship changes without raw C glue:

```cpp
sqlite::session tracker(conn);
tracker.attach_all();
sqlite::execute(conn, "UPDATE docs SET body = 'patched' WHERE id = 1;", true);
auto diff = tracker.patchset(); // std::vector<unsigned char>

sqlite::apply_patchset(replica_conn, diff);
```

Patchsets/changesets arrive as `std::vector<unsigned char>` buffers and helpers exist to apply them with a single call. Use this to fan out live syncing, create lightweight undo/redo stacks, or persist incremental diffs between tests.

## Serialization Helpers

Need to persist an in-memory database or hydrate a fixture from bytes? With `#include <sqlite/serialization.hpp>` you can call `sqlite::serialize(conn)` to obtain a `std::vector<unsigned char>` snapshot and `sqlite::deserialize(conn, image)` to restore it later (requires a SQLite build without `SQLITE_OMIT_DESERIALIZE`). This keeps golden images in memory-friendly buffers and lets tests fast-forward between prebuilt schemas without temporary files.

`sqlite::serialize` takes typed options instead of raw SQLite flags and always returns a vector that owns its bytes:

```cpp
// Read the connection's own contiguous in-memory image (SQLITE_SERIALIZE_NOCOPY)
// instead of letting SQLite allocate a fresh buffer; the result is still a copy.
auto image = sqlite::serialize(conn, "main", {.use_connection_image = true});
```

The deprecated raw-flag overload remains as an adapter that maps `SQLITE_SERIALIZE_NOCOPY` to `serialize_options::use_connection_image`.

## JSON & FTS Utilities

`#include <sqlite/json_fts.hpp>` ships opt-in helpers for two popular SQLite extensions:

- `sqlite::json::path()` builds JSON paths fluently, quoting and JSON-escaping key segments so keys like `a"b` or `a\b` round-trip, and `json::contains_expression()`/`json::extract_expression()` format ready-to-use SQL fragments that embed the path as a safely escaped SQL string literal.
- `sqlite::json::register_contains_function()` registers a deterministic `json_contains_value(doc, path, value)` UDF (implemented in terms of JSON1) so application code can reuse the same predicate everywhere.
- `sqlite::fts::match_expression()` stitches together safe `MATCH` clauses, while `sqlite::fts::register_rank_function()` exposes a ready-to-use ranking helper for FTS5 tables (skips automatically when FTS5 is unavailable).

## Statement Cache

High-traffic workloads often bounce through the same SQL repeatedly. Enable the built-in LRU cache to reuse prepared statements automatically:

```cpp
conn.configure_statement_cache({.capacity = 64, .enabled = true});
// subsequent sqlite::command/sqlite::query objects will reuse cached sqlite3_stmt*
```

Cached statements are reset and cleared of bound values when they return to the cache, so an idle statement never keeps a read lock open, holds on to previous parameter values, or reports active through `sqlite3_stmt_busy()`. Statements that cannot be retained (cache disabled, duplicate SQL text, a failed `sqlite3_reset`, or failed cache bookkeeping) are finalized instead of stored.

Returning a statement is a `noexcept` operation, because it runs while the last owner of a statement is destroyed. A `sqlite3_reset` error reported for the statement's last evaluation is therefore never thrown out of destruction; it is delivered to an error hook instead:

```cpp
conn.set_statement_cache_error_hook([](std::string const &message) {
    std::cerr << "statement cache: " << message << '\n';
});
```

The hook receives the reset error text, is invoked without the cache lock held, and must not throw (an exception escaping it is ignored). Without a hook, reset failures are written to `std::cerr` in debug builds and ignored otherwise.

Reconfiguring the cache with `configure_statement_cache()` (or closing the connection) finalizes idle entries immediately. A statement that is checked out while the cache is reconfigured may re-enter the new configuration when it is returned; if the new configuration has caching disabled, it is finalized on return. VSQLite++ deliberately keeps no generation tracking for statements prepared under an older configuration: the cache keys entries by SQL text only, capacity and enabled state do not change what a prepared statement means, and nothing consumes such a distinction, while schema invalidation is already handled by SQLite's `sqlite3_prepare_v2` automatic recompile behavior.
