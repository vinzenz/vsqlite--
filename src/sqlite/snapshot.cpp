/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2024 Vinzenz Feenstra
                         and contributors
 All rights reserved.

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

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

namespace {
#if defined(SQLITE_ENABLE_SNAPSHOT)
std::string normalize_schema(std::string_view schema) {
    if(schema.empty()) {
        return "main";
    }
    return std::string(schema);
}

sqlite3 * to_handle(sqlite::connection & con) {
    sqlite::private_accessor::acccess_check(con);
    return sqlite::private_accessor::get_handle(con);
}
#endif

std::string to_lower(std::string_view value) {
    std::string out(value.begin(), value.end());
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

sqlite::wal_mode parse_journal_mode(std::string_view value) {
    auto normalized = to_lower(value);
    if(normalized == "delete") return sqlite::wal_mode::rollback;
    if(normalized == "truncate") return sqlite::wal_mode::truncate;
    if(normalized == "persist") return sqlite::wal_mode::persist;
    if(normalized == "memory") return sqlite::wal_mode::memory;
    if(normalized == "wal") return sqlite::wal_mode::wal;
    if(normalized == "wal2") return sqlite::wal_mode::wal2;
    throw sqlite::database_exception("Unknown journal mode reported by SQLite: " + std::string(value));
}

std::string journal_sql(sqlite::wal_mode mode) {
    return std::string(sqlite::to_string(mode));
}

#if defined(SQLITE_ENABLE_SNAPSHOT)
std::string format_snapshot_error(char const * zMsg, std::string_view schema) {
    std::string msg = zMsg ? std::string(zMsg) : std::string("Snapshot operation failed");
    msg.append(" [schema=").append(schema).push_back(']');
    return msg;
}
#endif
} // namespace

#if !defined(SQLITE_ENABLE_SNAPSHOT)
constexpr char kSnapshotUnavailable[] = "SQLite library was built without SQLITE_ENABLE_SNAPSHOT.";
#endif

namespace sqlite {
inline namespace v2 {
    snapshot::snapshot(sqlite3_snapshot * handle)
        : handle_(handle) {}

    snapshot::~snapshot() {
        reset();
    }

    snapshot::snapshot(snapshot && other) noexcept
        : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    snapshot & snapshot::operator=(snapshot && other) noexcept {
        if(this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    void snapshot::reset() noexcept {
        if(handle_) {
#if defined(SQLITE_ENABLE_SNAPSHOT)
            sqlite3_snapshot_free(handle_);
#endif
            handle_ = nullptr;
        }
    }

    snapshot snapshot::take(connection & con, std::string_view schema) {
#if !defined(SQLITE_ENABLE_SNAPSHOT)
        (void)con;
        (void)schema;
        throw database_exception(kSnapshotUnavailable);
#else
        auto normalized = normalize_schema(schema);
        sqlite3_snapshot * raw = nullptr;
        int rc = sqlite3_snapshot_get(to_handle(con), normalized.c_str(), &raw);
        if(rc != SQLITE_OK) {
            throw database_exception_code(
                format_snapshot_error(sqlite3_errmsg(to_handle(con)), normalized),
                rc
            );
        }
        return snapshot(raw);
#endif
    }

    void snapshot::open(connection & con, std::string_view schema) const {
#if !defined(SQLITE_ENABLE_SNAPSHOT)
        (void)con;
        (void)schema;
        throw database_exception(kSnapshotUnavailable);
#else
        if(!handle_) {
            throw database_exception("Cannot open an empty snapshot.");
        }
        auto normalized = normalize_schema(schema);
        int rc = sqlite3_snapshot_open(to_handle(con), normalized.c_str(), handle_);
        if(rc == SQLITE_BUSY) {
            throw database_exception("Snapshot is too old and cannot be opened.");
        }
        if(rc != SQLITE_OK) {
            throw database_exception_code(
                format_snapshot_error(sqlite3_errmsg(to_handle(con)), normalized),
                rc
            );
        }
#endif
    }

    std::string_view to_string(wal_mode mode) {
        switch(mode) {
            case wal_mode::rollback: return "DELETE";
            case wal_mode::truncate: return "TRUNCATE";
            case wal_mode::persist:  return "PERSIST";
            case wal_mode::memory:   return "MEMORY";
            case wal_mode::wal:      return "WAL";
            case wal_mode::wal2:     return "WAL2";
            default: break;
        }
        throw database_exception("Unknown WAL mode requested.");
    }

    wal_mode set_wal_mode(connection & con, wal_mode mode) {
        auto pragma = "PRAGMA journal_mode = " + journal_sql(mode) + ";";
        query q(con, pragma);
        auto result = q.get_result();
        if(!result->next_row()) {
            throw database_exception("Failed to set journal_mode.");
        }
        auto reported = result->get_string(0);
        auto parsed = parse_journal_mode(reported);
        if(mode == wal_mode::wal2 && parsed != wal_mode::wal2) {
            throw database_exception(
                "SQLite rejected WAL2 mode (reported '" + reported + "')."
            );
        }
        return parsed;
    }

    wal_mode enable_wal(connection & con, bool prefer_wal2) {
        if(prefer_wal2) {
            try {
                return set_wal_mode(con, wal_mode::wal2);
            }
            catch(database_exception const &) {
                // Fallback to WAL if WAL2 is not available yet.
            }
        }
        return set_wal_mode(con, wal_mode::wal);
    }

    wal_mode get_wal_mode(connection & con) {
        query q(con, "PRAGMA journal_mode;");
        auto result = q.get_result();
        if(!result->next_row()) {
            throw database_exception("Failed to read journal_mode.");
        }
        return parse_journal_mode(result->get_string(0));
    }
} // namespace v2
} // namespace sqlite
