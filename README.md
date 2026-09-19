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

Release tags matching `v*` build and attach native Linux packages to the GitHub release:

- Debian packages via CPack's DEB generator.
- RPM packages via CPack's RPM generator.
- Arch Linux pacman packages from `packaging/arch/PKGBUILD`.

The generated packages install the CMake package config, headers, documentation, and shared library under `/usr` and link against the distribution SQLite package.

## Opening Databases: File, Memory, and URI

`sqlite::connection` offers explicit factories so the database location category is known before
validation runs, instead of being guessed from one string:

```cpp
#include <sqlite/connection.hpp>

auto file      = sqlite::connection::open_file("app.db", {.mode = sqlite::open_mode::open_existing});
auto temporary = sqlite::connection::open_memory();
auto named     = sqlite::connection::open_memory("events"); // file:events?mode=memory&cache=shared
auto uri       = sqlite::connection::open_uri("file:data/app.sqlite?mode=rw");
```

`sqlite::open_options` bundles the request: `mode` (an `sqlite::open_mode`, default
`open_or_create`) and `nofollow` (default `false`).

### What each entry point validates

- `open_file(path, options)` treats the name as a literal filename, never as a URI. Names starting
  with `file:` and the special name `:memory:` are rejected with a pointer to the fitting factory.
  The filesystem-adapter checks apply: the immediate parent directory must be a real directory, and
  an existing target must be a regular file and no symlink.
- `open_memory()` / `open_memory(name, options)` never checks, creates, or deletes anything on
  disk. The name is a pure logical key (reserved characters are percent-encoded automatically) and
  may look like a path under a directory that does not exist. Only `open_or_create` is accepted.
- `open_uri(uri, options)` passes `SQLITE_OPEN_URI` semantics to SQLite, which owns the URI
  interpretation: percent decoding, query parameters, and `vfs=` VFS selection. The wrapper rejects
  only what it can decide correctly: embedded NUL bytes, empty URIs, non-`file:` schemes,
  `open_mode::always_create`, and contradictory modes (below). No filesystem checks run here, so
  locations served by custom SQLite VFS implementations are not rejected by `std::filesystem`
  assumptions.

Every entry point — including the string constructors and `attach()` — rejects database names
containing embedded NUL bytes, which would otherwise be silently truncated.

### Destructive creation is file-only

`open_mode::always_create` deletes an existing database before recreating it and is accepted by
`open_file()` alone; `open_memory()` and `open_uri()` reject it before opening anything. Recreating
removes the main database file only: sidecar files (`-journal`, `-wal`, `-shm`) are left behind, and
databases held open by other connections keep running on the unlinked inode (POSIX semantics).

### URI modes versus wrapper options

When a URI carries an explicit `mode=` parameter and the wrapper options would map onto different
SQLite open flags, the call is rejected before anything is opened. The mapping is `mode=ro` with
`open_mode::open_readonly`, `mode=rw` with `open_mode::open_existing`, and `mode=create` or
`mode=memory` with `open_mode::open_or_create`. Unknown mode values (for example `mode=bogus`) are
left to SQLite, which rejects them while opening. Without a `mode=` parameter the wrapper options
govern on their own.

### Symlink policy scope

Symlink handling spans two mechanisms with different reach:

- Pre-open checks (via the filesystem adapter, using `symlink_status` semantics): the immediate
  parent directory and the database file entry itself are inspected, and symlinks are rejected.
  Symlinked components deeper inside the parent path are followed by the operating system and are
  not detected.
- `SQLITE_OPEN_NOFOLLOW` at open time: SQLite's VFS refuses to open the database file itself when
  it is a symlink. Enable it build-wide with `VSQLITE_ALLOW_FOLLOW_SYMLINKS=OFF` or per call with
  `open_options::nofollow` (which only strengthens the build-wide setting). It covers only the
  final path component.

A path can change between the pre-open check and the actual open (TOCTOU); the wrapper does not
claim to close that window — `SQLITE_OPEN_NOFOLLOW` narrows it for the final component only.

### Validation versus VFS boundary

The `filesystem_adapter` is a validation hook that runs before `open_file()` and the string
constructors open a disk-backed name; it does not replace the filesystem SQLite talks to. SQLite's
own VFS performs the real I/O. Locations that only exist for a custom VFS therefore belong to
`open_uri()`, where no `std::filesystem` checks would reject them.

### Migration table for the string constructors

| Legacy constructor | Recommended factory |
| --- | --- |
| `connection("app.db")` | `connection::open_file("app.db")` |
| `connection(":memory:")` | `connection::open_memory()` |
| `connection("file:events?mode=memory&cache=shared")` | `connection::open_memory("events")` |
| `connection("file:…")` (any other URI) | `connection::open_uri("file:…")` |
| `connection("app.db", open_mode::open_existing)` | `connection::open_file("app.db", {.mode = open_mode::open_existing})` |
| `connection("app.db", open_mode::open_readonly)` | `connection::open_file("app.db", {.mode = open_mode::open_readonly})` |
| `connection("app.db", open_mode::always_create)` | `connection::open_file("app.db", {.mode = open_mode::always_create})` |

The string constructors stay available: they classify the argument (`":memory:"`, `"file:"` URI, or
literal filename) and route it to the same internal open paths as the factories.

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

Need to persist an in-memory database or hydrate a fixture from bytes? With `#include <sqlite/serialization.hpp>` you can call `sqlite::serialize(conn)` to obtain a `std::vector<unsigned char>` snapshot and `sqlite::deserialize(conn, image)` to restore it later (requires `SQLITE_ENABLE_DESERIALIZE`). This keeps golden images in memory-friendly buffers and lets tests fast-forward between prebuilt schemas without temporary files.

## JSON & FTS Utilities

`#include <sqlite/json_fts.hpp>` ships opt-in helpers for two popular SQLite extensions:

- `sqlite::json::path()` builds JSON paths fluently and `json::contains_expression()`/`json::extract_expression()` format ready-to-use SQL fragments.
- `sqlite::json::register_contains_function()` registers a deterministic `json_contains_value(doc, path, value)` UDF (implemented in terms of JSON1) so application code can reuse the same predicate everywhere.
- `sqlite::fts::match_expression()` stitches together safe `MATCH` clauses, while `sqlite::fts::register_rank_function()` exposes a ready-to-use ranking helper for FTS5 tables (skips automatically when FTS5 is unavailable).

## Statement Cache

High-traffic workloads often bounce through the same SQL repeatedly. Enable the built-in LRU cache to reuse prepared statements automatically:

```cpp
conn.configure_statement_cache({.capacity = 64, .enabled = true});
// subsequent sqlite::command/sqlite::query objects will reuse cached sqlite3_stmt*
```

Cached statements reset/clear bindings on checkout, and the cache is cleared whenever the connection closes or you reconfigure it.
