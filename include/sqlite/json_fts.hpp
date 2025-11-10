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
#ifndef GUARD_SQLITE_JSON_FTS_HPP_INCLUDED
#define GUARD_SQLITE_JSON_FTS_HPP_INCLUDED

#include <string>
#include <string_view>

/**
 * @file sqlite/json_fts.hpp
 * @brief Convenience helpers for composing JSON1 expressions and working with FTS5 ranks.
 *
 * The functions declared here are light wrappers around SQLite's JSON + FTS extensions and are
 * intended to be opt-in utilities for applications that enable those compile-time features.
 */
namespace sqlite {
inline namespace v2 {
    struct connection;

    namespace json {
        /**
         * @brief Helper for composing JSON path expressions compatible with SQLite's JSON1 dialect.
         *
         * Instances start at the root (`$`) and can be extended via @ref key and @ref index.
         * The builder keeps the JSON path in UTF-8 form and automatically quotes segments
         * that require escaping (spaces, punctuation, etc.).
         */
        class path_builder {
        public:
            /// Constructs a builder positioned at the JSON root (`$`).
            path_builder();

            /**
             * @brief Appends an object key to the path.
             *
             * The key is automatically quoted when it contains characters that need escaping.
             *
             * @param segment Key to append.
             * @return Reference to the builder for chaining.
             */
            path_builder &key(std::string_view segment);

            /**
             * @brief Appends an array index access to the path.
             *
             * @param idx Zero-based array index to append.
             * @return Reference to the builder for chaining.
             */
            path_builder &index(std::size_t idx);

            /// Returns the current JSON path representation (e.g. `$.user.name`).
            std::string const &str() const noexcept {
                return path_;
            }

        private:
            std::string path_;
        };

        /// Convenience shortcut for starting a new JSON path builder rooted at `$`.
        inline path_builder path() {
            return path_builder();
        }

        /**
         * @brief Produces a SQL expression that extracts a JSON value at the given path.
         *
         * @param json_expr SQL expression that yields JSON text.
         * @param path JSON path built with @ref path_builder.
         * @return SQL snippet invoking `json_extract(json_expr, path)`.
         */
        std::string extract_expression(std::string_view json_expr, path_builder const &path);

        /**
         * @brief Produces a SQL expression that compares a JSON value at @p path with @p
         * value_expr.
         *
         * This expands to `json_extract(json_expr, path) = value_expr` so it can be embedded
         * inside WHERE clauses or CHECK constraints.
         */
        std::string contains_expression(std::string_view json_expr, path_builder const &path,
                                        std::string_view value_expr);

        /**
         * @brief Detects whether the connected SQLite build exposes the JSON1 extension.
         *
         * @returns true when JSON functions (e.g. `json_extract`) can be executed, false otherwise.
         */
        bool available(connection &con);

        /**
         * @brief Registers a deterministic SQL scalar function that checks for JSON containment.
         *
         * @param con Open connection that already enabled JSON1.
         * @param function_name Optional SQL symbol name (defaults to `json_contains_value`).
         *
         * @throws database_exception when JSON1 is unavailable or the function cannot be created.
         */
        void register_contains_function(connection &con,
                                        std::string_view function_name = "json_contains_value");
    } // namespace json

    namespace fts {
        /**
         * @brief Detects whether the connected SQLite build exposes the FTS5 extension.
         *
         * @returns true when FTS5 APIs are discoverable, false otherwise.
         */
        bool available(connection &con);

        /**
         * @brief Builds a SQL `MATCH` expression for FTS tables or columns.
         *
         * @param column_or_table Identifier on the left-hand side of MATCH.
         * @param query_expr SQL expression for the right-hand side (defaults to a parameter
         * placeholder).
         * @return SQL snippet like `col MATCH ?`.
         */
        std::string match_expression(std::string_view column_or_table,
                                     std::string_view query_expr = "?");

        /**
         * @brief Registers a custom ranking function compatible with `fts5` queries.
         *
         * The installed function uses a simple heuristic that counts matched phrases, and can be
         * referenced inside `ORDER BY` clauses as `fts_rank(matchinfo)`.
         *
         * @param con Connection with FTS5 available.
         * @param function_name Optional SQL symbol name (defaults to `fts_rank`).
         *
         * @throws database_exception when FTS5 is unavailable or registration fails.
         */
        void register_rank_function(connection &con, std::string_view function_name = "fts_rank");
    } // namespace fts
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_JSON_FTS_HPP_INCLUDED
