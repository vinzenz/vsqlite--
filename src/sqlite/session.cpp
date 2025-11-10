/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2024 Vinzenz Feenstra
                         and contributors
 All rights reserved.

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
 * Redistributions in binary forms must reproduce the above copyright notice,
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
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/private/private_accessor.hpp>
#include <sqlite/session.hpp>

#include <sqlite3.h>

namespace {
constexpr char kSessionsUnavailable[] = "SQLite was built without SQLITE_ENABLE_SESSION.";

#if defined(SQLITE_ENABLE_SESSION)
sqlite3 * handle(sqlite::connection & con) {
    sqlite::private_accessor::acccess_check(con);
    return sqlite::private_accessor::get_handle(con);
}

std::string normalize_schema(std::string_view schema) {
    if(schema.empty()) {
        return "main";
    }
    return std::string(schema);
}

std::vector<unsigned char> take_buffer(
    int rc,
    int size,
    void * data,
    char const * error_message
) {
    if(rc != SQLITE_OK) {
        sqlite3_free(data);
        throw sqlite::database_exception(error_message ? error_message : "Failed to collect session changes.");
    }
    unsigned char * bytes = static_cast<unsigned char *>(data);
    std::vector<unsigned char> out(bytes, bytes + size);
    sqlite3_free(data);
    return out;
}
#endif
} // namespace

namespace sqlite {
inline namespace v2 {
    session::session(connection & con, std::string_view schema, session_options options)
        : con_(&con)
        , handle_(nullptr) {
#if defined(SQLITE_ENABLE_SESSION)
        sqlite3_session * raw = nullptr;
        auto normalized = normalize_schema(schema);
        int rc = sqlite3session_create(handle(con), normalized.c_str(), &raw);
        if(rc != SQLITE_OK) {
            throw database_exception_code(sqlite3_errmsg(handle(con)), rc);
        }
        handle_ = raw;
        if(options.indirect) {
            sqlite3session_indirect(static_cast<sqlite3_session *>(handle_), 1);
        }
#else
        (void)schema;
        (void)options;
        throw database_exception(kSessionsUnavailable);
#endif
    }

    session::~session() {
#if defined(SQLITE_ENABLE_SESSION)
        if(handle_) {
            sqlite3session_delete(static_cast<sqlite3_session *>(handle_));
        }
#endif
        handle_ = nullptr;
        con_ = nullptr;
    }

    session::session(session && other) noexcept
        : con_(other.con_)
        , handle_(other.handle_) {
        other.con_ = nullptr;
        other.handle_ = nullptr;
    }

    session & session::operator=(session && other) noexcept {
        if(this != &other) {
#if defined(SQLITE_ENABLE_SESSION)
            if(handle_) {
                sqlite3session_delete(static_cast<sqlite3_session *>(handle_));
            }
#endif
            con_ = other.con_;
            handle_ = other.handle_;
            other.con_ = nullptr;
            other.handle_ = nullptr;
        }
        return *this;
    }

    void session::attach(std::string_view table) {
#if defined(SQLITE_ENABLE_SESSION)
        if(!handle_) {
            throw database_exception("Session handle is not initialized.");
        }
        std::string owned = table.empty() ? std::string() : std::string(table);
        auto rc = sqlite3session_attach(static_cast<sqlite3_session *>(handle_),
                                        owned.empty() ? nullptr : owned.c_str());
        if(rc != SQLITE_OK) {
            throw database_exception_code(sqlite3_errmsg(handle(*con_)), rc);
        }
#else
        (void)table;
        throw database_exception(kSessionsUnavailable);
#endif
    }

    void session::attach_all() {
        attach(std::string_view());
    }

    void session::enable(bool value) {
#if defined(SQLITE_ENABLE_SESSION)
        if(!handle_) throw database_exception("Session handle is not initialized.");
        sqlite3session_enable(static_cast<sqlite3_session *>(handle_), value ? 1 : 0);
#else
        (void)value;
        throw database_exception(kSessionsUnavailable);
#endif
    }

    void session::set_indirect(bool value) {
#if defined(SQLITE_ENABLE_SESSION)
        if(!handle_) throw database_exception("Session handle is not initialized.");
        sqlite3session_indirect(static_cast<sqlite3_session *>(handle_), value ? 1 : 0);
#else
        (void)value;
        throw database_exception(kSessionsUnavailable);
#endif
    }

    std::vector<unsigned char> session::collect(bool patchset) {
#if defined(SQLITE_ENABLE_SESSION)
        if(!handle_) {
            throw database_exception("Session handle is not initialized.");
        }
        int size = 0;
        void * data = nullptr;
        int rc = patchset
            ? sqlite3session_patchset(static_cast<sqlite3_session *>(handle_), &size, &data)
            : sqlite3session_changeset(static_cast<sqlite3_session *>(handle_), &size, &data);
        return take_buffer(rc, size, data, sqlite3_errmsg(handle(*con_)));
#else
        (void)patchset;
        throw database_exception(kSessionsUnavailable);
#endif
    }

    std::vector<unsigned char> session::changeset() {
        return collect(false);
    }

    std::vector<unsigned char> session::patchset() {
        return collect(true);
    }

    void * session::native_handle() const noexcept {
        return handle_;
    }

    void apply_changeset(connection & con, std::span<const unsigned char> data) {
#if defined(SQLITE_ENABLE_SESSION)
        if(data.empty()) {
            return;
        }
        auto rc = sqlite3changeset_apply(handle(con),
                                         static_cast<int>(data.size()),
                                         data.data(),
                                         nullptr,
                                         nullptr,
                                         nullptr);
        if(rc != SQLITE_OK) {
            throw database_exception_code(sqlite3_errmsg(handle(con)), rc);
        }
#else
        (void)con;
        (void)data;
        throw database_exception(kSessionsUnavailable);
#endif
    }

    void apply_patchset(connection & con, std::span<const unsigned char> data) {
#if defined(SQLITE_ENABLE_SESSION)
        if(data.empty()) {
            return;
        }
        auto rc = sqlite3changeset_apply(handle(con),
                                         static_cast<int>(data.size()),
                                         data.data(),
                                         nullptr,
                                         nullptr,
                                         nullptr);
        if(rc != SQLITE_OK) {
            throw database_exception_code(sqlite3_errmsg(handle(con)), rc);
        }
#else
        (void)con;
        (void)data;
        throw database_exception(kSessionsUnavailable);
#endif
    }
} // namespace v2
} // namespace sqlite
