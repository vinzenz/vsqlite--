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
#include <format>
#include <string_view>
#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/view.hpp>

namespace {
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

std::string schema_prefix(std::string const &database) {
    if (database.empty()) {
        return std::string();
    }
    return quote_identifier(database) + ".";
}
} // namespace

namespace sqlite {
inline namespace v2 {
    view::view(connection &con) : m_con(con) {}

    view::~view() {}
    void view::create(bool temporary, std::string const &database, std::string const &alias,
                      std::string const &sql_query) {
        if (database.empty()) {
            throw database_exception(
                "Database name must not be empty when creating a qualified view.");
        }
        if (alias.empty()) {
            throw database_exception("View alias must not be empty.");
        }
        const std::string temp = temporary ? "TEMPORARY " : "";
        auto sql = std::format("CREATE {}VIEW {}{} AS {};", temp, schema_prefix(database),
                               quote_identifier(alias), sql_query);
        execute(m_con, sql, true);
    }

    void view::create(bool temporary, std::string const &alias, std::string const &sql_query) {
        if (alias.empty()) {
            throw database_exception("View alias must not be empty.");
        }
        const std::string temp = temporary ? "TEMPORARY " : "";
        auto sql = std::format("CREATE {}VIEW {} AS {};", temp, quote_identifier(alias), sql_query);
        execute(m_con, sql, true);
    }

    void view::drop(std::string const &alias) {
        if (alias.empty()) {
            throw database_exception("View alias must not be empty.");
        }
        auto sql = std::format("DROP VIEW {};", quote_identifier(alias));
        execute(m_con, sql, true);
    }

    void view::drop(std::string const &database, std::string const &alias) {
        if (database.empty()) {
            throw database_exception(
                "Database name must not be empty when dropping a qualified view.");
        }
        if (alias.empty()) {
            throw database_exception("View alias must not be empty.");
        }
        auto sql = std::format("DROP VIEW {}{};", schema_prefix(database), quote_identifier(alias));
        execute(m_con, sql, true);
    }
} // namespace v2
} // namespace sqlite
