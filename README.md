[![Build Status](https://travis-ci.org/vinzenz/vsqlite--.png?branch=master)](https://travis-ci.org/vinzenz/vsqlite--)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/1976/badge.svg)](https://scan.coverity.com/projects/1976)

VSQLite++ - A welldesigned and portable SQLite3 Wrapper for C++
(C) 2006-2014 by virtuosic bytes  - vinzenz.feenstra@gmail.com

Author: Vinzenz Feenstra

License: BSD-3

# Website: http://vsqlite.virtuosic-bytes.com

[![Join the chat at https://gitter.im/vinzenz/vsqlite--](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/vinzenz/vsqlite--?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

# Supported Compilers
- g++ 4.x
- MSVC 2005-2013
- Clang 3.x

# Operating Systems
- Linux
- Windows
- Mac OS X

# Dependencies
- A C++20-capable compiler (GCC 11+, Clang 14+, MSVC 19.30+)
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
