/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2024 Vinzenz Feenstra
                         and contributors
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
 * Neither the name of virtuosic bytes nor the names of its contributors may
   be used to endorse or promote products derived from this software without
   specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.

##############################################################################*/
#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/private/private_accessor.hpp>
#include <sqlite/query.hpp>
#include <sqlite/snapshot.hpp>

#include <sqlite3.h>

#include "dynamic_symbols.hpp"

namespace {
std::string normalize_schema(std::string_view schema) {
    if (schema.empty()) {
        return "main";
    }
    return std::string(schema);
}

sqlite3 *to_handle(sqlite::connection &con) {
    sqlite::private_accessor::acccess_check(con);
    return sqlite::private_accessor::get_handle(con);
}

std::string to_lower(std::string_view value) {
    std::string out(value.begin(), value.end());
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

sqlite::wal_mode parse_journal_mode(std::string_view value) {
    auto normalized = to_lower(value);
    if (normalized == "delete")
        return sqlite::wal_mode::rollback;
    if (normalized == "truncate")
        return sqlite::wal_mode::truncate;
    if (normalized == "persist")
        return sqlite::wal_mode::persist;
    if (normalized == "memory")
        return sqlite::wal_mode::memory;
    if (normalized == "wal")
        return sqlite::wal_mode::wal;
    if (normalized == "wal2")
        return sqlite::wal_mode::wal2;
    throw sqlite::database_exception("Unknown journal mode reported by SQLite: " +
                                     std::string(value));
}

std::string journal_sql(sqlite::wal_mode mode) {
    return std::string(sqlite::to_string(mode));
}

std::string format_snapshot_error(sqlite3 *db, int rc, std::string_view schema) {
    std::string msg;
    if (db) {
        auto const *zMsg = sqlite3_errmsg(db);
        if (zMsg && *zMsg) {
            msg.assign(zMsg);
        }
    }
    if (msg.empty()) {
        msg = "Snapshot operation failed";
    }
    msg.append(" (rc=").append(std::to_string(rc)).append(", errstr=");
    auto const *zErrStr = sqlite3_errstr(rc);
    msg.append(zErrStr ? zErrStr : "unknown");
    if (db) {
        msg.append(", xrc=").append(std::to_string(sqlite3_extended_errcode(db)));
    }
    msg.append(") [schema=").append(schema).push_back(']');
    return msg;
}

struct snapshot_api {
    using get_fn  = int (*)(sqlite3 *, const char *, sqlite3_snapshot **);
    using open_fn = int (*)(sqlite3 *, const char *, sqlite3_snapshot *);
    using free_fn = void (*)(sqlite3_snapshot *);

    get_fn get   = nullptr;
    open_fn open = nullptr;
    free_fn free = nullptr;
};

snapshot_api const &snapshot_symbols() {
    static snapshot_api api = [] {
        snapshot_api loaded;
        loaded.get =
            sqlite::detail::load_sqlite_symbol<snapshot_api::get_fn>("sqlite3_snapshot_get");
        loaded.open =
            sqlite::detail::load_sqlite_symbol<snapshot_api::open_fn>("sqlite3_snapshot_open");
        loaded.free =
            sqlite::detail::load_sqlite_symbol<snapshot_api::free_fn>("sqlite3_snapshot_free");
        return loaded;
    }();
    return api;
}

void ensure_snapshot_available() {
    if (!sqlite::snapshots_supported()) {
        throw sqlite::database_exception("SQLite snapshot APIs are not available in this build.");
    }
}
} // namespace

namespace sqlite {
inline namespace v2 {
    snapshot::snapshot(sqlite3_snapshot *handle) : handle_(handle) {}

    snapshot::~snapshot() {
        reset();
    }

    snapshot::snapshot(snapshot &&other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    snapshot &snapshot::operator=(snapshot &&other) noexcept {
        if (this != &other) {
            reset();
            handle_       = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    void snapshot::reset() noexcept {
        if (handle_) {
            if (auto free_fn = snapshot_symbols().free) {
                free_fn(handle_);
            }
            handle_ = nullptr;
        }
    }

    snapshot snapshot::take(connection &con, std::string_view schema) {
        ensure_snapshot_available();
        auto normalized       = normalize_schema(schema);
        auto *db              = to_handle(con);
        sqlite3_snapshot *raw = nullptr;
        auto get_fn           = snapshot_symbols().get;
        int rc                = get_fn ? get_fn(db, normalized.c_str(), &raw) : SQLITE_ERROR;
        if (rc != SQLITE_OK) {
            throw database_exception_code(format_snapshot_error(db, rc, normalized), rc);
        }
        return snapshot(raw);
    }

    void snapshot::open(connection &con, std::string_view schema) const {
        ensure_snapshot_available();
        if (!handle_) {
            throw database_exception("Cannot open an empty snapshot.");
        }
        auto normalized = normalize_schema(schema);
        auto open_fn    = snapshot_symbols().open;
        auto *db        = to_handle(con);
        int rc          = open_fn ? open_fn(db, normalized.c_str(), handle_) : SQLITE_ERROR;
        if (rc == SQLITE_BUSY) {
            throw database_exception("Snapshot is too old and cannot be opened.");
        }
        if (rc != SQLITE_OK) {
            throw database_exception_code(format_snapshot_error(db, rc, normalized), rc);
        }
    }

    std::string_view to_string(wal_mode mode) {
        switch (mode) {
        case wal_mode::rollback:
            return "DELETE";
        case wal_mode::truncate:
            return "TRUNCATE";
        case wal_mode::persist:
            return "PERSIST";
        case wal_mode::memory:
            return "MEMORY";
        case wal_mode::wal:
            return "WAL";
        case wal_mode::wal2:
            return "WAL2";
        default:
            break;
        }
        throw database_exception("Unknown WAL mode requested.");
    }

    wal_mode set_wal_mode(connection &con, wal_mode mode) {
        auto pragma = "PRAGMA journal_mode = " + journal_sql(mode) + ";";
        query q(con, pragma);
        auto result = q.get_result();
        if (!result->next_row()) {
            throw database_exception("Failed to set journal_mode.");
        }
        auto reported = result->get<std::string>(0);
        auto parsed   = parse_journal_mode(reported);
        if (mode == wal_mode::wal2 && parsed != wal_mode::wal2) {
            throw database_exception("SQLite rejected WAL2 mode (reported '" + reported + "').");
        }
        return parsed;
    }

    wal_mode enable_wal(connection &con, bool prefer_wal2) {
        if (prefer_wal2) {
            try {
                return set_wal_mode(con, wal_mode::wal2);
            } catch (database_exception const &) {
                // Fallback to WAL if WAL2 is not available yet.
            }
        }
        return set_wal_mode(con, wal_mode::wal);
    }

    wal_mode get_wal_mode(connection &con) {
        query q(con, "PRAGMA journal_mode;");
        auto result = q.get_result();
        if (!result->next_row()) {
            throw database_exception("Failed to read journal_mode.");
        }
        return parse_journal_mode(result->get<std::string>(0));
    }

    bool snapshots_supported() noexcept {
        auto const &api = snapshot_symbols();
        return api.get && api.open && api.free;
    }
} // namespace v2
} // namespace sqlite
