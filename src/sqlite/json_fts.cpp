/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2024 Vinzenz Feenstra
                         and contributors
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

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
#include <algorithm>
#include <cctype>
#include <string>

#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/json_fts.hpp>
#include <sqlite/private/private_accessor.hpp>

#include <sqlite3.h>

#if !defined(FTS5_TOKENIZE_QUERY)
// This is to avoid to have compile time errors if it doesn't exist.
struct fts5_api {
    int (*xCreateFunction)(fts5_api *pApi, const char *zName, void *pUserData,
                           fts5_extension_function xFunction, void (*xDestroy)(void *));
};

#endif

namespace {
bool needs_quoted_key(std::string_view key) {
    if (key.empty()) {
        return true;
    }
    return std::any_of(key.begin(), key.end(),
                       [](unsigned char c) { return !(std::isalnum(c) || c == '_'); });
}

std::string quote_key(std::string_view key) {
    std::string out;
    out.reserve(key.size() + 2);
    out.push_back('"');
    for (char c : key) {
        if (c == '"') {
            out.push_back('"');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

sqlite3 *db_handle(sqlite::connection &con) {
    sqlite::private_accessor::acccess_check(con);
    return sqlite::private_accessor::get_handle(con);
}

bool probe_json(sqlite3 *db) {
    sqlite3_stmt *stmt = nullptr;
    const char *sql    = "SELECT json('null');";
    int rc             = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return false;
    }
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_ROW;
}

void json_contains(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
    if (argc != 3) {
        sqlite3_result_error(ctx, "json_contains_value(json, path, value) expects 3 arguments.",
                             -1);
        return;
    }
    sqlite3 *db        = sqlite3_context_db_handle(ctx);
    sqlite3_stmt *stmt = nullptr;
    const char *sql    = "SELECT CASE "
                         "WHEN json_extract(?1, ?2) IS NULL THEN 0 "
                         "WHEN json_extract(?1, ?2) = ?3 THEN 1 "
                         "ELSE 0 END;";
    int rc             = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_result_error(ctx, sqlite3_errmsg(db), -1);
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_bind_value(stmt, 1, argv[0]);
    sqlite3_bind_value(stmt, 2, argv[1]);
    sqlite3_bind_value(stmt, 3, argv[2]);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        sqlite3_result_int(ctx, sqlite3_column_int(stmt, 0));
    } else {
        sqlite3_result_error(ctx, sqlite3_errmsg(db), -1);
    }
    sqlite3_finalize(stmt);
}

struct rank_context {};

void simple_rank(const Fts5ExtensionApi *api, Fts5Context *fts, sqlite3_context *ctx, int,
                 sqlite3_value **) {
    double score     = 0.0;
    int phrase_count = api->xPhraseCount(fts);
    for (int i = 0; i < phrase_count; ++i) {
        Fts5PhraseIter iter;
        int col = 0;
        int off = 0;
        api->xPhraseFirst(fts, i, &iter, &col, &off);
        while (col >= 0) {
            score += 1.0;
            api->xPhraseNext(fts, &iter, &col, &off);
        }
    }
    sqlite3_result_double(ctx, score);
}

fts5_api *sqlite3_fts5_api_from_db(sqlite3 *db) {
    fts5_api *pRet      = nullptr;
    sqlite3_stmt *pStmt = nullptr;

    if (SQLITE_OK == sqlite3_prepare_v2(db, "SELECT fts5(?1)", -1, &pStmt, 0)) {
        sqlite3_bind_pointer(pStmt, 1, (void *)&pRet, "fts5_api_ptr", NULL);
        sqlite3_step(pStmt);
    }
    sqlite3_finalize(pStmt);
    return pRet;
}

bool probe_fts(sqlite3 *db) {
    auto *api = sqlite3_fts5_api_from_db(db);
    return api != nullptr;
}
} // namespace

namespace sqlite {
inline namespace v2 {
    namespace json {
        path_builder::path_builder() : path_("$") {}

        path_builder &path_builder::key(std::string_view segment) {
            path_.push_back('.');
            if (needs_quoted_key(segment)) {
                path_.append(quote_key(segment));
            } else {
                path_.append(segment);
            }
            return *this;
        }

        path_builder &path_builder::index(std::size_t idx) {
            path_.push_back('[');
            path_.append(std::to_string(idx));
            path_.push_back(']');
            return *this;
        }

        std::string extract_expression(std::string_view json_expr, path_builder const &path) {
            std::string sql("json_extract(");
            sql.append(json_expr);
            sql.append(", '");
            sql.append(path.str());
            sql.append("')");
            return sql;
        }

        std::string contains_expression(std::string_view json_expr, path_builder const &path,
                                        std::string_view value_expr) {
            std::string sql("json_extract(");
            sql.append(json_expr);
            sql.append(", '");
            sql.append(path.str());
            sql.append("') = ");
            sql.append(value_expr);
            return sql;
        }

        bool available(connection &con) {
            return probe_json(db_handle(con));
        }

        void register_contains_function(connection &con, std::string_view function_name) {
            auto *db = db_handle(con);
            if (!probe_json(db)) {
                throw database_exception(
                    "SQLite JSON1 extension is not available in this connection.");
            }
            std::string owned(function_name);
            int rc =
                sqlite3_create_function_v2(db, owned.c_str(), 3, SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                           nullptr, &json_contains, nullptr, nullptr, nullptr);
            if (rc != SQLITE_OK) {
                throw database_exception_code(sqlite3_errmsg(db), rc);
            }
        }
    } // namespace json

    namespace fts {
        bool available(connection &con) {
            return probe_fts(db_handle(con));
        }

        std::string match_expression(std::string_view column_or_table,
                                     std::string_view query_expr) {
            std::string sql;
            sql.reserve(column_or_table.size() + query_expr.size() + 7);
            sql.append(column_or_table);
            sql.append(" MATCH ");
            sql.append(query_expr);
            return sql;
        }

        void register_rank_function(connection &con, std::string_view function_name) {
            auto *db  = db_handle(con);
            auto *api = sqlite3_fts5_api_from_db(db);
            if (!api) {
                throw database_exception(
                    "SQLite FTS5 extension is not available in this connection.");
            }
            std::string owned(function_name);
            int rc = api->xCreateFunction(api, owned.c_str(), nullptr, &simple_rank, nullptr);
            if (rc != SQLITE_OK) {
                throw database_exception_code(sqlite3_errmsg(db), rc);
            }
        }
    } // namespace fts
} // namespace v2
} // namespace sqlite
