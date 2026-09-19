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
#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <string_view>
#include <system_error>
#include <sqlite/command.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/filesystem_adapter.hpp>
#include <sqlite3.h>
#include <iostream>

namespace {
// Classification of a database name handed to connection::open() or attach().
struct database_name_info {
    bool is_uri   = false;  ///< the name is a "file:" URI
    bool memory   = false;  ///< the database lives in memory only
    bool has_mode = false;  ///< the URI query carries an explicit mode parameter
    std::string mode_value; ///< effective (last) mode parameter value, percent-decoded
    std::string path;       ///< filesystem path (percent-decoded for URIs); empty
                            ///< when the database has no file on disk
};

// SQLite interprets a database name as a URI exactly when it starts with
// "file:" and URI interpretation is enabled (see make_open_flags()).
bool is_uri_database(std::string_view db) {
    return db.rfind("file:", 0) == 0;
}

int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// Decodes %HH escapes the way SQLite does: a "%00" escape ends the component,
// and a '%' not followed by two hex digits is kept literal.
std::string percent_decode(std::string_view text) {
    std::string decoded;
    decoded.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            int high = hex_digit_value(text[i + 1]);
            int low  = hex_digit_value(text[i + 2]);
            if (high >= 0 && low >= 0) {
                char octet = static_cast<char>((high << 4) | low);
                if (octet == '\0') {
                    break;
                }
                decoded.push_back(octet);
                i += 2;
                continue;
            }
        }
        decoded.push_back(text[i]);
    }
    return decoded;
}

// Every entry point rejects embedded NUL bytes up front: sqlite3_open_v2() and
// the filesystem calls take NUL-terminated names, so a std::string with an
// embedded NUL would otherwise be silently truncated.
void reject_embedded_nul(std::string_view db) {
    if (db.find('\0') != std::string_view::npos) {
        throw sqlite::database_exception("Database name must not contain embedded NUL characters.");
    }
}

// Percent-encodes a component the wrapper generates itself (the name of a
// shared in-memory database) so SQLite's URI parser decodes it back verbatim.
std::string percent_encode_component(std::string_view text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(text.size());
    for (unsigned char c : text) {
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~' ||
                          c == '/';
        if (unreserved) {
            encoded.push_back(static_cast<char>(c));
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(hex[c >> 4]);
        encoded.push_back(hex[c & 0x0F]);
    }
    return encoded;
}

// Splits a database name the same way sqlite3_open_v2() does for URIs, so that
// in-memory detection and path validation agree with what SQLite will make of
// the name.
database_name_info classify_database_name(std::string_view db) {
    database_name_info info;
    if (db == ":memory:") {
        info.memory = true;
        return info;
    }
    if (!is_uri_database(db)) {
        return info;
    }
    info.is_uri           = true;
    std::string_view rest = db.substr(5);

    // Optional authority: SQLite accepts an empty one or "localhost" only and
    // rejects anything else when opening.
    if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/') {
        rest.remove_prefix(2);
        auto authority_end = std::min(rest.find('/'), rest.size());
        auto authority     = rest.substr(0, authority_end);
        if (!authority.empty() && authority != "localhost") {
            return info;
        }
        rest.remove_prefix(authority_end);
    }

    auto path_end = rest.find_first_of("?#");
    info.path =
        percent_decode(path_end == std::string_view::npos ? rest : rest.substr(0, path_end));
    if (info.path == ":memory:") {
        info.memory = true; // "file::memory:" names an in-memory database
        return info;
    }
    if (path_end == std::string_view::npos || rest[path_end] != '?') {
        return info; // no query component
    }

    // The query is made of &-separated name[=value] pairs; both parts are
    // percent-decoded. SQLite applies the parameters in order, so a repeated
    // "mode" parameter overrules the earlier ones.
    std::string_view query = rest.substr(path_end + 1);
    auto fragment          = query.find('#');
    if (fragment != std::string_view::npos) {
        query = query.substr(0, fragment);
    }

    std::size_t param_pos = 0;
    while (param_pos < query.size()) {
        auto next = query.find('&', param_pos);
        auto param =
            query.substr(param_pos, next == std::string_view::npos ? next : next - param_pos);
        param_pos   = next == std::string_view::npos ? query.size() : next + 1;
        auto equals = param.find('=');
        if (percent_decode(param.substr(0, equals)) != "mode") {
            continue;
        }
        info.has_mode   = true;
        info.mode_value = equals == std::string_view::npos
                              ? std::string()
                              : percent_decode(param.substr(equals + 1));
    }
    info.memory = info.has_mode && info.mode_value == "memory";
    return info;
}

// The wrapper translates open_mode into sqlite3_open_v2() flags; a URI with an
// explicit mode parameter asks SQLite for the same decision. A wrapper mode
// that would map onto different flags contradicts the URI and is rejected
// before anything is opened. Mode values the wrapper cannot map (SQLite knows
// ro, rw, memory, and create) are left to SQLite, which rejects them itself.
void check_uri_mode_conflict(std::string const &uri_mode, sqlite::open_mode requested,
                             std::string const &uri) {
    std::optional<sqlite::open_mode> uri_equivalent;
    if (uri_mode == "ro") {
        uri_equivalent = sqlite::open_mode::open_readonly;
    } else if (uri_mode == "rw") {
        uri_equivalent = sqlite::open_mode::open_existing;
    } else if (uri_mode == "create" || uri_mode == "memory") {
        uri_equivalent = sqlite::open_mode::open_or_create;
    }
    if (uri_equivalent && requested != *uri_equivalent) {
        throw sqlite::database_exception("URI mode parameter 'mode=" + uri_mode + "' in '" + uri +
                                         "' contradicts the requested open mode");
    }
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
    auto info = classify_database_name(db);
    if (info.memory) {
        return;
    }
    if (info.is_uri && info.path.empty()) {
        return; // a "file:" URI without a path opens a temporary database
    }
    if (db.empty()) {
        throw sqlite::database_exception("Database path must not be empty.");
    }
    std::filesystem::path path(info.is_uri ? info.path : db);
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

// Validates a disk-backed database name for the requested mode and performs the
// destructive recreation open_mode::always_create asks for. Shared by the
// string constructors and connection::open_file().
void prepare_disk_backed_open(std::string const &db, std::filesystem::path const &disk_path,
                              sqlite::open_mode mode, sqlite::filesystem_adapter_ptr const &fs) {
    validate_db_path(
        db, mode == sqlite::open_mode::open_existing || mode == sqlite::open_mode::open_readonly,
        fs);
    std::error_code ec;
    bool exists = std::filesystem::exists(disk_path, ec);
    if (ec) {
        throw sqlite::database_system_error("Failed to inspect database '" + db + "'", ec.value());
    }

    if ((mode == sqlite::open_mode::open_readonly || mode == sqlite::open_mode::open_existing) &&
        !exists) {
        throw sqlite::database_exception("Database '" + db + "' does not exist");
    }
    if (mode == sqlite::open_mode::always_create && exists) {
        auto entry = fs->status(disk_path);
        if (entry.error) {
            throw sqlite::database_system_error("Failed to inspect existing database '" + db + "'",
                                                entry.error.value());
        }
        if (std::filesystem::is_symlink(entry.status)) {
            throw sqlite::database_exception("Refusing to remove symlinked database '" + db + "'");
        }
        if (!std::filesystem::is_regular_file(entry.status)) {
            throw sqlite::database_exception("Refusing to remove non-regular database target '" +
                                             db + "'");
        }
        if (!fs->remove(disk_path, ec) || ec) {
            throw sqlite::database_system_error("Failed to remove existing database '" + db + "'",
                                                ec.value());
        }
    }
}

int make_open_flags(bool readonly, bool allow_create) {
    int flags = SQLITE_OPEN_FULLMUTEX;
    // Names starting with "file:" are SQLite URIs (e.g. a named in-memory
    // database "file:name?mode=memory&cache=shared"). SQLite only applies URI
    // interpretation to those names and passes every other name through
    // literally, so ordinary filenames behave exactly as before. ATTACH
    // follows the same URI rules because the flag applies to the whole
    // connection.
    flags |= SQLITE_OPEN_URI;
#ifndef VSQLITE_ALLOW_FOLLOW_SYMLINKS
    flags |= SQLITE_OPEN_NOFOLLOW;
#endif
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

// Translates an open_mode (plus the per-call nofollow option) into the
// sqlite3_open_v2() flags the explicit factories pass to SQLite.
int flags_for_open_mode(sqlite::open_mode mode, bool nofollow) {
    int flags = 0;
    switch (mode) {
    case sqlite::open_mode::open_readonly:
        flags = make_open_flags(true, false);
        break;
    case sqlite::open_mode::open_existing:
        flags = make_open_flags(false, false);
        break;
    case sqlite::open_mode::always_create:
    case sqlite::open_mode::open_or_create:
    default:
        flags = make_open_flags(false, true);
        break;
    }
    if (nofollow) {
        flags |= SQLITE_OPEN_NOFOLLOW; // only strengthens the build-wide setting
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

    // Opens without further validation; the explicit factories validate first
    // and then hand the prepared name and flags to this constructor.
    connection::connection(std::string const &db, int flags, filesystem_adapter_ptr fs) :
        handle(0),
        filesystem(std::move(fs) ? std::move(fs) : std::make_shared<default_filesystem_adapter>()),
        cache_() {
        open_with_flags(db, flags);
    }

    connection connection::open_file(std::filesystem::path const &path, open_options options) {
        std::string db = path.string();
        reject_embedded_nul(db);
        if (db.empty()) {
            throw database_exception("Database path must not be empty.");
        }
        if (is_uri_database(db)) {
            throw database_exception("Database path '" + db +
                                     "' starts with 'file:' and would be interpreted as a URI; "
                                     "use connection::open_uri().");
        }
        if (db == ":memory:") {
            throw database_exception("Database path ':memory:' names an in-memory database; use "
                                     "connection::open_memory().");
        }
        auto fs = std::make_shared<default_filesystem_adapter>();
        prepare_disk_backed_open(db, std::filesystem::path(db), options.mode, fs);
        return connection(db, flags_for_open_mode(options.mode, options.nofollow), fs);
    }

    connection connection::open_memory() {
        // The anonymous in-memory database needs no validation: it never
        // touches the filesystem.
        return connection(":memory:", make_open_flags(false, true),
                          std::make_shared<default_filesystem_adapter>());
    }

    connection connection::open_memory(std::string const &name, open_options options) {
        reject_embedded_nul(name);
        if (name.empty()) {
            throw database_exception(
                "Memory database name must not be empty; use connection::open_memory() for a "
                "private database.");
        }
        if (options.mode == sqlite::open_mode::always_create) {
            throw database_exception("open_mode::always_create deletes an existing database and is "
                                     "only supported by connection::open_file().");
        }
        if (options.mode != sqlite::open_mode::open_or_create) {
            throw database_exception("connection::open_memory() only supports "
                                     "open_mode::open_or_create.");
        }
        // The name is a pure key for the shared in-memory database; the
        // filesystem is never checked, created, or deleted.
        std::string uri = "file:" + percent_encode_component(name) + "?mode=memory&cache=shared";
        return connection(uri, make_open_flags(false, true),
                          std::make_shared<default_filesystem_adapter>());
    }

    connection connection::open_uri(std::string const &uri, open_options options) {
        reject_embedded_nul(uri);
        if (uri.empty()) {
            throw database_exception("Database URI must not be empty.");
        }
        if (!is_uri_database(uri)) {
            throw database_exception("Database URI '" + uri + "' must use the 'file:' scheme.");
        }
        if (options.mode == sqlite::open_mode::always_create) {
            throw database_exception("open_mode::always_create deletes an existing database and is "
                                     "only supported by connection::open_file().");
        }
        auto info = classify_database_name(uri);
        if (info.has_mode) {
            check_uri_mode_conflict(info.mode_value, options.mode, uri);
        }
        // SQLite owns the URI interpretation (including the vfs= parameter);
        // the wrapper adds no filesystem checks of its own here.
        return connection(uri, flags_for_open_mode(options.mode, options.nofollow),
                          std::make_shared<default_filesystem_adapter>());
    }

    connection::~connection() {
        try {
            close();
        } catch (...) {
        }
    }

    void connection::open(const std::string &db) {
        reject_embedded_nul(db);
        validate_db_path(db, false, filesystem);
        open_with_flags(db, make_open_flags(false, true));
    }

    void connection::open(const std::string &db, bool readonly) {
        reject_embedded_nul(db);
        validate_db_path(db, readonly, filesystem);
        open_with_flags(db, make_open_flags(readonly, !readonly));
    }

    void connection::open(std::string const &db, sqlite::open_mode open_mode) {
        reject_embedded_nul(db);
        auto info = classify_database_name(db);
        // In-memory databases and "file:" URIs without a path never touch the
        // disk, so they need neither path validation nor an existing file.
        bool disk_backed = !info.memory && !(info.is_uri && info.path.empty());
        if (disk_backed) {
            prepare_disk_backed_open(db, std::filesystem::path(info.is_uri ? info.path : db),
                                     open_mode, filesystem);
        }
        open_with_flags(db, flags_for_open_mode(open_mode, false));
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
        reject_embedded_nul(db);
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
