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
#ifndef GUARD_SQLITE_DETAIL_FUNCTION_REGISTRATION_HPP_INCLUDED
#define GUARD_SQLITE_DETAIL_FUNCTION_REGISTRATION_HPP_INCLUDED

#include <memory>
#include <string_view>

struct sqlite3_context;
struct sqlite3_value;

namespace sqlite {
inline namespace v2 {
    struct connection;

    namespace detail {
        /// Owned callback state handed over to SQLite by `register_scalar_function`. The
        /// deleter matches the `xDestroy` signature of `sqlite3_create_function_v2`.
        using function_user_data = std::unique_ptr<void, void (*)(void *)>;

        /**
         * @brief Compiled part of `sqlite::create_function`: registers a type-erased scalar
         *        SQL function on the native handle of @p con.
         *
         * Kept out of the public templates so that consumers of `sqlite/function.hpp` do not
         * depend on the connection's private internals.
         *
         * Once the registration request reaches SQLite, @p user_data is released to it: its
         * xDestroy callback runs on failed registration, on replacement, and on connection
         * close alike. When registration cannot be attempted, @p user_data is destroyed here
         * instead, so ownership never falls between the cracks.
         *
         * @throws database_exception when the connection is not usable or registration fails.
         */
        void register_scalar_function(connection &con, std::string_view name, int arity,
                                      int text_representation, function_user_data user_data,
                                      void (*func)(sqlite3_context *, int, sqlite3_value **));
    } // namespace detail
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_DETAIL_FUNCTION_REGISTRATION_HPP_INCLUDED
