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
cmd.emit();
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
