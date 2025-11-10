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

namespace sqlite {
inline namespace v2 {
    struct connection;

    namespace json {
        class path_builder {
        public:
            path_builder();
            path_builder & key(std::string_view segment);
            path_builder & index(std::size_t idx);
            std::string const & str() const noexcept { return path_; }
        private:
            std::string path_;
        };

        inline path_builder path() { return path_builder(); }

        std::string extract_expression(std::string_view json_expr, path_builder const & path);
        std::string contains_expression(std::string_view json_expr,
                                        path_builder const & path,
                                        std::string_view value_expr);

        bool available(connection & con);
        void register_contains_function(connection & con,
                                        std::string_view function_name = "json_contains_value");
    } // namespace json

    namespace fts {
        bool available(connection & con);
        std::string match_expression(std::string_view column_or_table,
                                     std::string_view query_expr = "?");
        void register_rank_function(connection & con,
                                    std::string_view function_name = "fts_rank");
    } // namespace fts
} // namespace v2
} // namespace sqlite

#endif //GUARD_SQLITE_JSON_FTS_HPP_INCLUDED
