/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2014 Vinzenz Feenstra vinzenz.feenstra@gmail.com
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

#include <array>
#include <cctype>
#include <sqlite/database_exception.hpp>
#include <sqlite/command.hpp>
#include <sqlite/private/private_accessor.hpp>
#include <sqlite3.h>

namespace sqlite {
inline namespace v2 {

    null_type nil = null_type();

    namespace {
        std::string_view trim_left(std::string const &sql) {
            auto view = std::string_view(sql);
            auto pos  = view.find_first_not_of(" \t\r\n");
            if (pos == std::string_view::npos) {
                return {};
            }
            view.remove_prefix(pos);
            return view;
        }

        std::string first_token_upper(std::string const &sql) {
            auto view = trim_left(sql);
            std::string token;
            for (char ch : view) {
                if (std::isspace(static_cast<unsigned char>(ch)) || ch == ';' || ch == '(') {
                    break;
                }
                token.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
            }
            return token;
        }

        bool is_schema_changing_statement(std::string const &sql) {
            static constexpr std::array<const char *, 5> keywords = {"ATTACH", "DETACH", "CREATE",
                                                                     "DROP", "ALTER"};
            auto token                                            = first_token_upper(sql);
            if (token.empty()) {
                return false;
            }
            for (auto keyword : keywords) {
                if (token == keyword) {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    command::command(connection &con, std::string const &sql) :
        m_con(con), m_sql(sql), stmt(0), last_arg_idx(0) {
        private_accessor::acccess_check(con);
        prepare();
    }

    command::~command() {
        try {
            finalize();
        } catch (...) {
        }
    }

    void command::finalize() {
        access_check();
        if (!stmt) {
            return;
        }
        if (is_schema_changing_statement(m_sql)) {
            sqlite3_finalize(stmt);
        } else {
            private_accessor::release_cached_statement(m_con, m_sql, stmt);
        }
        stmt = 0;
    }

    void command::clear() {
        sqlite3_reset(stmt);
        last_arg_idx = 0;
        sqlite3_reset(stmt);
    }

    void command::prepare() {
        private_accessor::acccess_check(m_con);
        bool schema_change = is_schema_changing_statement(m_sql);
        if (schema_change) {
            private_accessor::clear_statement_cache(m_con);
        }
        if (stmt)
            finalize();
        if (!schema_change) {
            stmt = private_accessor::acquire_cached_statement(m_con, m_sql);
            if (stmt) {
                return;
            }
        }
        const char *tail = 0;
        int err          = sqlite3_prepare_v2(get_handle(), m_sql.c_str(), -1, &stmt, &tail);
        if (err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err, m_sql);
    }

    bool command::emit() {
        return step();
    }

    bool command::step() {
        access_check();
        int err = sqlite3_step(stmt);
        switch (err) {
        case SQLITE_ROW:
            return true;
        case SQLITE_DONE:
            return false;
        case SQLITE_MISUSE:
            throw database_misuse_exception_code(sqlite3_errmsg(get_handle()), err, m_sql);
        default:
            throw database_exception_code(sqlite3_errmsg(get_handle()), err, m_sql);
        }
        return false;
    }

    bool command::operator()() {
        return step();
    }

    void command::bind(int idx) {
        access_check();
        int err = sqlite3_bind_null(stmt, idx);
        if (err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err, m_sql);
    }

    void command::bind(int idx, int v) {
        access_check();
        int err = sqlite3_bind_int(stmt, idx, v);
        if (err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err, m_sql);
    }

    void command::bind(int idx, std::int64_t v) {
        access_check();
        int err = sqlite3_bind_int64(stmt, idx, v);
        if (err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err, m_sql);
    }

    void command::bind(int idx, double v) {
        access_check();
        int err = sqlite3_bind_double(stmt, idx, v);
        if (err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err, m_sql);
    }

    namespace {
        const char *text_or_dummy(std::string_view view, char const *&dummy_holder) {
            static char const kDummy = 0;
            if (view.empty()) {
                dummy_holder = &kDummy;
                return dummy_holder;
            }
            return view.data();
        }

        const unsigned char *blob_or_dummy(std::span<const unsigned char> view,
                                           unsigned char const *&dummy_holder) {
            static const unsigned char kDummy = 0;
            if (view.empty()) {
                dummy_holder = &kDummy;
                return dummy_holder;
            }
            return view.data();
        }
    } // namespace

    void command::bind_text_impl(int idx, std::string_view v) {
        access_check();
        char const *dummy = nullptr;
        auto ptr          = text_or_dummy(v, dummy);
        int err = sqlite3_bind_text(stmt, idx, ptr, static_cast<int>(v.size()), SQLITE_TRANSIENT);
        if (err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err, m_sql);
    }

    void command::bind(int idx, void const *v, size_t vn) {
        access_check();
        int err = sqlite3_bind_blob(stmt, idx, v, int(vn), SQLITE_TRANSIENT);
        if (err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err, m_sql);
    }

    void command::bind(int idx, std::vector<unsigned char> const &v) {
        bind(idx, std::span<const unsigned char>(v.data(), v.size()));
    }

    void command::bind(int idx, std::span<const unsigned char> v) {
        access_check();
        unsigned char const *dummy = nullptr;
        auto ptr                   = blob_or_dummy(v, dummy);
        int err = sqlite3_bind_blob(stmt, idx, ptr, static_cast<int>(v.size()), SQLITE_TRANSIENT);
        if (err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err, m_sql);
    }

    void command::bind(int idx, std::span<const std::byte> v) {
        auto data = std::as_bytes(v);
        auto ptr  = std::span<const unsigned char>(
            reinterpret_cast<unsigned char const *>(data.data()), data.size());
        bind(idx, ptr);
    }

    int command::parameter_index(std::string_view name) const {
        access_check();
        std::string owned(name);
        auto idx = sqlite3_bind_parameter_index(stmt, owned.c_str());
        if (idx == 0) {
            throw database_exception("no such parameter: " + owned);
        }
        return idx;
    }

    void command::access_check() const {
        private_accessor::acccess_check(m_con);
        if (!stmt)
            throw database_exception("command was not prepared or is invalid");
    }

    command &command::operator%(null_type const &) {
        bind(++last_arg_idx);
        return *this;
    }

    command &command::operator%(int v) {
        bind(++last_arg_idx, v);
        return *this;
    }

    command &command::operator%(std::int64_t v) {
        bind(++last_arg_idx, v);
        return *this;
    }

    command &command::operator%(double v) {
        bind(++last_arg_idx, v);
        return *this;
    }

    command &command::operator%(std::vector<unsigned char> const &v) {
        bind(++last_arg_idx, v);
        return *this;
    }

    command &command::operator%(std::span<const unsigned char> v) {
        bind(++last_arg_idx, v);
        return *this;
    }

    command &command::operator%(std::span<const std::byte> v) {
        bind(++last_arg_idx, v);
        return *this;
    }

    struct sqlite3 *command::get_handle() {
        return private_accessor::get_handle(m_con);
    }
} // namespace v2
} // namespace sqlite
