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
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/private/private_accessor.hpp>
#include <sqlite/session.hpp>

#include <sqlite3.h>

#include "dynamic_symbols.hpp"

struct sqlite3_session;
struct sqlite3_changeset_iter;

// Conflict-handler codes from the session extension. sqlite3.h only declares them when it is
// processed with SQLITE_ENABLE_SESSION defined, which the wrapper build does not do.
#if !defined(SQLITE_CHANGESET_OMIT)
#define SQLITE_CHANGESET_OMIT 0
#endif
#if !defined(SQLITE_CHANGESET_REPLACE)
#define SQLITE_CHANGESET_REPLACE 1
#endif
#if !defined(SQLITE_CHANGESET_ABORT)
#define SQLITE_CHANGESET_ABORT 2
#endif
#if !defined(SQLITE_CHANGESET_DATA)
#define SQLITE_CHANGESET_DATA 1
#endif
#if !defined(SQLITE_CHANGESET_NOTFOUND)
#define SQLITE_CHANGESET_NOTFOUND 2
#endif
#if !defined(SQLITE_CHANGESET_CONFLICT)
#define SQLITE_CHANGESET_CONFLICT 3
#endif
#if !defined(SQLITE_CHANGESET_CONSTRAINT)
#define SQLITE_CHANGESET_CONSTRAINT 4
#endif
#if !defined(SQLITE_CHANGESET_FOREIGN_KEY)
#define SQLITE_CHANGESET_FOREIGN_KEY 5
#endif

namespace {
constexpr char kSessionsUnavailable[] = "SQLite session APIs are not available in this build.";

sqlite3 *handle(sqlite::connection &con) {
    sqlite::private_accessor::acccess_check(con);
    return sqlite::private_accessor::get_handle(con);
}

std::string normalize_schema(std::string_view schema) {
    if (schema.empty()) {
        return "main";
    }
    return std::string(schema);
}

std::vector<unsigned char> take_buffer(int rc, int size, void *data, char const *error_message) {
    if (rc != SQLITE_OK) {
        sqlite3_free(data);
        throw sqlite::database_exception(error_message ? error_message
                                                       : "Failed to collect session changes.");
    }
    unsigned char *bytes = static_cast<unsigned char *>(data);
    std::vector<unsigned char> out(bytes, bytes + size);
    sqlite3_free(data);
    return out;
}

struct session_api {
    using create_fn    = int (*)(sqlite3 *, const char *, sqlite3_session **);
    using delete_fn    = void (*)(sqlite3_session *);
    using attach_fn    = int (*)(sqlite3_session *, const char *);
    using enable_fn    = int (*)(sqlite3_session *, int);
    using indirect_fn  = int (*)(sqlite3_session *, int);
    using changeset_fn = int (*)(sqlite3_session *, int *, void **);
    using patchset_fn  = int (*)(sqlite3_session *, int *, void **);
    using apply_fn     = int (*)(sqlite3 *, int, void *, int (*)(void *, const char *),
                             int (*)(void *, int, sqlite3_changeset_iter *), void *);
    using iter_op_fn   = int (*)(sqlite3_changeset_iter *, const char **, int *, int *, int *);

    create_fn create         = nullptr;
    delete_fn destroy        = nullptr;
    attach_fn attach         = nullptr;
    enable_fn enable         = nullptr;
    indirect_fn indirect     = nullptr;
    changeset_fn changeset   = nullptr;
    patchset_fn patchset     = nullptr;
    apply_fn apply_changeset = nullptr;
    // Optional: only used to fill in changeset_conflict details for conflict handlers.
    iter_op_fn iter_op       = nullptr;
};

session_api const &session_symbols() {
    static session_api api = [] {
        session_api loaded{};
        loaded.create =
            sqlite::detail::load_sqlite_symbol<session_api::create_fn>("sqlite3session_create");
        loaded.destroy =
            sqlite::detail::load_sqlite_symbol<session_api::delete_fn>("sqlite3session_delete");
        loaded.attach =
            sqlite::detail::load_sqlite_symbol<session_api::attach_fn>("sqlite3session_attach");
        loaded.enable =
            sqlite::detail::load_sqlite_symbol<session_api::enable_fn>("sqlite3session_enable");
        loaded.indirect =
            sqlite::detail::load_sqlite_symbol<session_api::indirect_fn>("sqlite3session_indirect");
        loaded.changeset = sqlite::detail::load_sqlite_symbol<session_api::changeset_fn>(
            "sqlite3session_changeset");
        loaded.patchset =
            sqlite::detail::load_sqlite_symbol<session_api::patchset_fn>("sqlite3session_patchset");
        loaded.apply_changeset =
            sqlite::detail::load_sqlite_symbol<session_api::apply_fn>("sqlite3changeset_apply");
        loaded.iter_op =
            sqlite::detail::load_sqlite_symbol<session_api::iter_op_fn>("sqlite3changeset_op");
        return loaded;
    }();
    return api;
}

void ensure_session_available() {
    if (!sqlite::sessions_supported()) {
        throw sqlite::database_exception(kSessionsUnavailable);
    }
}

sqlite::changeset_conflict_type map_conflict_type(int reason) {
    switch (reason) {
        case SQLITE_CHANGESET_DATA: return sqlite::changeset_conflict_type::data;
        case SQLITE_CHANGESET_NOTFOUND: return sqlite::changeset_conflict_type::not_found;
        case SQLITE_CHANGESET_CONFLICT: return sqlite::changeset_conflict_type::conflict;
        case SQLITE_CHANGESET_FOREIGN_KEY: return sqlite::changeset_conflict_type::foreign_key;
        case SQLITE_CHANGESET_CONSTRAINT:
        default: return sqlite::changeset_conflict_type::constraint;
    }
}

sqlite::changeset_operation map_operation(int op) {
    switch (op) {
        case SQLITE_INSERT: return sqlite::changeset_operation::insert;
        case SQLITE_UPDATE: return sqlite::changeset_operation::update;
        case SQLITE_DELETE: return sqlite::changeset_operation::remove;
        default: return sqlite::changeset_operation::unknown;
    }
}

int map_policy(sqlite::conflict_policy policy) {
    switch (policy) {
        case sqlite::conflict_policy::omit: return SQLITE_CHANGESET_OMIT;
        case sqlite::conflict_policy::replace: return SQLITE_CHANGESET_REPLACE;
        case sqlite::conflict_policy::abort: break;
    }
    return SQLITE_CHANGESET_ABORT;
}

struct conflict_context {
    sqlite::conflict_handler handler;
    std::exception_ptr pending_exception;
    session_api::iter_op_fn iter_op = nullptr;
};

/// Never returns without a decision: a missing handler or one that throws aborts the
/// application instead of leaking exceptions through SQLite's C boundary.
extern "C" int conflict_trampoline(void *ctx, int reason, sqlite3_changeset_iter *iter) {
    auto *context = static_cast<conflict_context *>(ctx);
    if (!context || !context->handler) {
        return SQLITE_CHANGESET_ABORT;
    }
    try {
        sqlite::changeset_conflict conflict{map_conflict_type(reason),
                                            sqlite::changeset_operation::unknown, {}};
        // For foreign key conflicts SQLite provides no current row: only
        // sqlite3changeset_fk_conflicts() may be called on the iterator.
        if (reason != SQLITE_CHANGESET_FOREIGN_KEY && context->iter_op) {
            char const *table = nullptr;
            int columns       = 0;
            int op            = 0;
            int indirect      = 0;
            if (context->iter_op(iter, &table, &columns, &op, &indirect) == SQLITE_OK) {
                conflict.operation = map_operation(op);
                if (table) {
                    // Copy: SQLite points zTab into internal storage that is invalid
                    // once sqlite3changeset_apply returns.
                    conflict.table = table;
                }
            }
        }
        return map_policy(context->handler(conflict));
    } catch (...) {
        context->pending_exception = std::current_exception();
        return SQLITE_CHANGESET_ABORT;
    }
}

} // namespace

namespace sqlite {
inline namespace v2 {

    bool sessions_supported() noexcept {
        auto const &api = session_symbols();
        return api.create && api.destroy && api.attach && api.enable && api.indirect &&
               api.changeset && api.patchset && api.apply_changeset;
    }

    session::session(connection &con, std::string_view schema, session_options options) :
        con_(&con), handle_(nullptr) {
        ensure_session_available();
        sqlite3_session *raw = nullptr;
        auto normalized      = normalize_schema(schema);
        auto create_fn       = session_symbols().create;
        int rc = create_fn ? create_fn(handle(con), normalized.c_str(), &raw) : SQLITE_ERROR;
        if (rc != SQLITE_OK) {
            throw database_exception_code(sqlite3_errmsg(handle(con)), rc);
        }
        handle_ = raw;
        if (options.indirect) {
            session_symbols().indirect(static_cast<sqlite3_session *>(handle_), 1);
        }
    }

    session::~session() {
        if (handle_) {
            if (auto destroy_fn = session_symbols().destroy) {
                destroy_fn(static_cast<sqlite3_session *>(handle_));
            }
        }
        handle_ = nullptr;
        con_    = nullptr;
    }

    session::session(session &&other) noexcept : con_(other.con_), handle_(other.handle_) {
        other.con_    = nullptr;
        other.handle_ = nullptr;
    }

    session &session::operator=(session &&other) noexcept {
        if (this != &other) {
            if (handle_) {
                if (auto destroy_fn = session_symbols().destroy) {
                    destroy_fn(static_cast<sqlite3_session *>(handle_));
                }
            }
            con_          = other.con_;
            handle_       = other.handle_;
            other.con_    = nullptr;
            other.handle_ = nullptr;
        }
        return *this;
    }

    void session::attach(std::string_view table) {
        ensure_session_available();
        if (!handle_) {
            throw database_exception("Session handle is not initialized.");
        }
        std::string owned = table.empty() ? std::string() : std::string(table);
        auto attach_fn    = session_symbols().attach;
        int rc            = attach_fn ? attach_fn(static_cast<sqlite3_session *>(handle_),
                                       owned.empty() ? nullptr : owned.c_str())
                                      : SQLITE_ERROR;
        if (rc != SQLITE_OK) {
            throw database_exception_code(sqlite3_errmsg(handle(*con_)), rc);
        }
    }

    void session::attach_all() {
        attach(std::string_view());
    }

    void session::enable(bool value) {
        ensure_session_available();
        if (!handle_) {
            throw database_exception("Session handle is not initialized.");
        }
        session_symbols().enable(static_cast<sqlite3_session *>(handle_), value ? 1 : 0);
    }

    void session::set_indirect(bool value) {
        ensure_session_available();
        if (!handle_) {
            throw database_exception("Session handle is not initialized.");
        }
        session_symbols().indirect(static_cast<sqlite3_session *>(handle_), value ? 1 : 0);
    }

    std::vector<unsigned char> session::collect(bool patchset) {
        ensure_session_available();
        if (!handle_) {
            throw database_exception("Session handle is not initialized.");
        }
        int size   = 0;
        void *data = nullptr;
        int rc     = SQLITE_ERROR;
        if (patchset) {
            auto patch_fn = session_symbols().patchset;
            rc = patch_fn ? patch_fn(static_cast<sqlite3_session *>(handle_), &size, &data)
                          : SQLITE_ERROR;
        } else {
            auto change_fn = session_symbols().changeset;
            rc = change_fn ? change_fn(static_cast<sqlite3_session *>(handle_), &size, &data)
                           : SQLITE_ERROR;
        }
        return take_buffer(rc, size, data, sqlite3_errmsg(handle(*con_)));
    }

    std::vector<unsigned char> session::changeset() {
        return collect(false);
    }

    std::vector<unsigned char> session::patchset() {
        return collect(true);
    }

    void *session::native_handle() const noexcept {
        return handle_;
    }

    void apply_changeset(connection &con, std::span<const unsigned char> data) {
        apply_changeset(con, data, conflict_policy::abort);
    }

    void apply_changeset(connection &con, std::span<const unsigned char> data,
                         conflict_policy policy) {
        apply_changeset(con, data, [policy](changeset_conflict const &) { return policy; });
    }

    void apply_changeset(connection &con, std::span<const unsigned char> data,
                         conflict_handler handler) {
        ensure_session_available();
        if (data.empty()) {
            return;
        }
        auto apply_fn = session_symbols().apply_changeset;
        if (!apply_fn) {
            throw database_exception(kSessionsUnavailable);
        }
        conflict_context context;
        context.handler = std::move(handler);
        context.iter_op = session_symbols().iter_op;
        void *payload   = const_cast<void *>(static_cast<void const *>(data.data()));
        // xFilter stays null on purpose: SQLite treats that as "include all tables". The
        // conflict handler must never be null - SQLite calls it unconditionally on conflicts.
        int rc = apply_fn(handle(con), static_cast<int>(data.size()), payload, nullptr,
                          &conflict_trampoline, &context);
        if (rc != SQLITE_OK) {
            if (context.pending_exception) {
                std::rethrow_exception(context.pending_exception);
            }
            if (rc == SQLITE_ABORT) {
                throw database_exception_code(
                    "Applying changeset aborted by conflict handler.", SQLITE_ABORT);
            }
            throw database_exception_code(sqlite3_errmsg(handle(con)), rc);
        }
    }

    void apply_patchset(connection &con, std::span<const unsigned char> data) {
        apply_changeset(con, data, conflict_policy::abort);
    }

    void apply_patchset(connection &con, std::span<const unsigned char> data,
                        conflict_policy policy) {
        apply_changeset(con, data, policy);
    }

    void apply_patchset(connection &con, std::span<const unsigned char> data,
                        conflict_handler handler) {
        apply_changeset(con, data, std::move(handler));
    }

} // namespace v2
} // namespace sqlite
