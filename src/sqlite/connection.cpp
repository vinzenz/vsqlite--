/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2015 Vinzenz Feenstra vinzenz.feenstra@gmail.com
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
#include <filesystem>
#include <format>
#include <string_view>
#include <system_error>
#include <sqlite/command.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/filesystem_adapter.hpp>
#include <sqlite3.h>

#ifndef SQLITE_OPEN_NOFOLLOW
#define SQLITE_OPEN_NOFOLLOW 0x08000000
#endif

namespace {
bool is_special_database(std::string_view db) {
    if (db == ":memory:") {
        return true;
    }
    if (db.rfind("file:", 0) == 0) {
        auto query_pos = db.find('?');
        if (query_pos != std::string_view::npos) {
            auto params = db.substr(query_pos + 1);
            if (params.find("mode=memory") != std::string_view::npos) {
                return true;
            }
        }
    }
    return false;
}

std::string describe_path(std::filesystem::path const &path) {
    return path.empty() ? std::string(".") : path.string();
}

void ensure_parent_directory_safe(std::filesystem::path const &path, std::string const &original_db,
                                  sqlite::filesystem_adapter_ptr const &fs) {
    auto parent = path.parent_path();
    if (parent.empty()) {
        return;
    }
    auto entry = fs->status(parent);
    if (entry.error) {
        throw sqlite::database_system_error("Failed to inspect directory '" +
                                                describe_path(parent) + "' for database '" +
                                                original_db + "'",
                                            entry.error.value());
    }
    if (entry.status.type() == std::filesystem::file_type::not_found) {
        throw sqlite::database_exception("Directory '" + describe_path(parent) +
                                         "' for database '" + original_db + "' does not exist");
    }
    if (!std::filesystem::is_directory(entry.status)) {
        throw sqlite::database_exception("Path '" + describe_path(parent) +
                                         "' is not a directory (required for database '" +
                                         original_db + "')");
    }
    if (std::filesystem::is_symlink(entry.status)) {
        throw sqlite::database_exception("Directory '" + describe_path(parent) +
                                         "' for database '" + original_db +
                                         "' must not be a symlink");
    }
}

void validate_db_path(std::string const &db, bool require_exists,
                      sqlite::filesystem_adapter_ptr const &fs) {
    if (is_special_database(db)) {
        return;
    }
    if (db.empty()) {
        throw sqlite::database_exception("Database path must not be empty.");
    }
    std::filesystem::path path(db);
    ensure_parent_directory_safe(path, db, fs);
    auto entry     = fs->status(path);
    auto not_found = std::make_error_code(std::errc::no_such_file_or_directory);
    // Compare values; categories are different (system vs. general) on MSVC so operator== would
    // consider them non-equal.
    if (entry.error && entry.error.value() != not_found.value()) {
        throw sqlite::database_system_error("Failed to inspect database '" + db + "'",
                                            entry.error.value());
    }
    if (entry.status.type() == std::filesystem::file_type::not_found) {
        if (require_exists) {
            throw sqlite::database_exception("Database '" + db + "' does not exist");
        }
        return;
    }
    if (std::filesystem::is_symlink(entry.status)) {
        throw sqlite::database_exception("Database path '" + db + "' must not be a symlink");
    }
    if (!std::filesystem::is_regular_file(entry.status)) {
        throw sqlite::database_exception("Database path '" + db + "' must refer to a regular file");
    }
}

int make_open_flags(bool readonly, bool allow_create) {
    int flags = SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_NOFOLLOW;
    if (readonly) {
        flags |= SQLITE_OPEN_READONLY;
    } else {
        flags |= SQLITE_OPEN_READWRITE;
        if (allow_create) {
            flags |= SQLITE_OPEN_CREATE;
        }
    }
    return flags;
}

std::string quote_identifier(std::string_view identifier) {
    std::string quoted;
    quoted.reserve(identifier.size() + 2);
    quoted.push_back('"');
    for (char c : identifier) {
        if (c == '"') {
            quoted.push_back('"');
        }
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}
} // namespace

namespace sqlite {
inline namespace v2 {
    connection::connection(std::string const &db) :
        connection(db, std::make_shared<default_filesystem_adapter>()) {}

    connection::connection(std::string const &db, filesystem_adapter_ptr fs) :
        handle(0),
        filesystem(std::move(fs) ? std::move(fs) : std::make_shared<default_filesystem_adapter>()),
        cache_() {
        open(db);
    }

    connection::connection(std::string const &db, sqlite::open_mode open_mode) :
        connection(db, open_mode, std::make_shared<default_filesystem_adapter>()) {}

    connection::connection(std::string const &db, sqlite::open_mode open_mode,
                           filesystem_adapter_ptr fs) :
        handle(0),
        filesystem(std::move(fs) ? std::move(fs) : std::make_shared<default_filesystem_adapter>()),
        cache_() {
        open(db, open_mode);
    }

    connection::~connection() {
        try {
            close();
        } catch (...) {
        }
    }

    void connection::open(const std::string &db) {
        validate_db_path(db, false, filesystem);
        open_with_flags(db, make_open_flags(false, true));
    }

    void connection::open(const std::string &db, bool readonly) {
        validate_db_path(db, readonly, filesystem);
        open_with_flags(db, make_open_flags(readonly, !readonly));
    }

    void connection::open(std::string const &db, sqlite::open_mode open_mode) {
        bool special = is_special_database(db);
        if (!special) {
            validate_db_path(db,
                             open_mode == sqlite::open_mode::open_existing ||
                                 open_mode == sqlite::open_mode::open_readonly,
                             filesystem);
        }

        std::filesystem::path disk_path =
            special ? std::filesystem::path() : std::filesystem::path(db);
        std::error_code ec;
        bool exists = special ? false : std::filesystem::exists(disk_path, ec);
        if (ec && !special) {
            throw database_system_error("Failed to inspect database '" + db + "'", ec.value());
        }

        switch (open_mode) {
        case sqlite::open_mode::open_readonly:
            if (!exists && !special) {
                throw database_exception("Read-only database '" + db + "' does not exist");
            }
            open_with_flags(db, make_open_flags(true, false));
            return;
        case sqlite::open_mode::open_existing:
            if (!exists && !special) {
                throw database_exception("Database '" + db + "' does not exist");
            }
            open_with_flags(db, make_open_flags(false, false));
            return;
        case sqlite::open_mode::always_create:
            if (exists && !special) {
                auto entry = filesystem->status(disk_path);
                if (entry.error) {
                    throw database_system_error("Failed to inspect existing database '" + db + "'",
                                                entry.error.value());
                }
                if (std::filesystem::is_symlink(entry.status)) {
                    throw database_exception("Refusing to remove symlinked database '" + db + "'");
                }
                if (!std::filesystem::is_regular_file(entry.status)) {
                    throw database_exception("Refusing to remove non-regular database target '" +
                                             db + "'");
                }
                if (!filesystem->remove(disk_path, ec) || ec) {
                    throw database_system_error("Failed to remove existing database '" + db + "'",
                                                ec.value());
                }
            }
            [[fallthrough]];
        case sqlite::open_mode::open_or_create:
        default:
            open_with_flags(db, make_open_flags(false, true));
            return;
        }
    }

    void connection::open_with_flags(std::string const &db, int flags) {
        sqlite3 *tmp = nullptr;
        int err      = sqlite3_open_v2(db.c_str(), &tmp, flags, nullptr);
        if (err != SQLITE_OK) {
            std::string message = tmp ? sqlite3_errmsg(tmp) : "Could not open database";
            if (tmp) {
                sqlite3_close(tmp);
            }
            throw database_exception_code(message, err);
        }
        handle = tmp;
        sqlite3_extended_result_codes(handle, 1);
    }

    void connection::close() {
        access_check();
        cache_.clear(handle);
        int err = sqlite3_close(handle);
        if (err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(handle), err);
        handle = 0;
    }

    void connection::access_check() {
        if (!handle)
            throw database_exception("Database is not open.");
    }

    void connection::attach(std::string const &db, std::string const &alias) {
        if (alias.empty()) {
            throw database_exception("Database alias must not be empty.");
        }
        validate_db_path(db, false, filesystem);
        command cmd(*this, std::format("ATTACH DATABASE ? AS {};", quote_identifier(alias)));
        cmd % db;
        cmd.step_once();
    }

    void connection::detach(std::string const &alias) {
        if (alias.empty()) {
            throw database_exception("Database alias must not be empty.");
        }
        auto sql = std::format("DETACH DATABASE {};", quote_identifier(alias));
        execute(*this, sql, true);
    }

    std::int64_t connection::get_last_insert_rowid() {
        if (!handle)
            throw database_exception("Database is not open.");
        return static_cast<std::int64_t>(sqlite3_last_insert_rowid(handle));
    }

    void connection::configure_statement_cache(statement_cache_config const &cfg) {
        cache_.reset(cfg);
    }

    statement_cache_config connection::statement_cache_settings() const {
        return cache_.config();
    }

    sqlite3_stmt *connection::acquire_cached_statement(std::string const &sql) {
        if (!handle)
            return nullptr;
        return cache_.acquire(handle, sql);
    }

    void connection::release_cached_statement(std::string const &sql, sqlite3_stmt *stmt) {
        if (!stmt)
            return;
        if (!handle) {
            sqlite3_finalize(stmt);
            return;
        }
        cache_.release(sql, stmt);
    }

    void connection::clear_statement_cache() {
        cache_.clear(handle);
    }
} // namespace v2
} // namespace sqlite
