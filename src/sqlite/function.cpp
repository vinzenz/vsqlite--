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
#include <format>
#include <string>
#include <string_view>

#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/detail/function_registration.hpp>
#include <sqlite/private/private_accessor.hpp>

#include <sqlite3.h>

namespace sqlite {
inline namespace v2 {
    namespace detail {
        namespace {
            std::string make_function_error(std::string_view name) {
                if (name.empty()) {
                    return "Failed to register SQL function.";
                }
                return std::format("Failed to register SQL function '{}'.", name);
            }
        } // namespace

        void register_scalar_function(connection &con, std::string_view name, int arity,
                                      int text_representation, function_user_data user_data,
                                      void (*func)(sqlite3_context *, int, sqlite3_value **)) {
            std::string const name_buffer(name);

            private_accessor::acccess_check(con);
            auto *handle = private_accessor::get_handle(con);

            // Hand ownership to SQLite before checking the result: its xDestroy callback runs
            // on failed registration, on replacement, and on connection close alike.
            int rc = sqlite3_create_function_v2(handle, name_buffer.c_str(), arity,
                                                text_representation, user_data.get(), func,
                                                nullptr, nullptr, user_data.get_deleter());
            user_data.release();

            if (rc != SQLITE_OK) {
                auto err = sqlite3_errmsg(handle);
                throw database_exception_code(err ? err : make_function_error(name_buffer), rc);
            }
        }
    } // namespace detail
} // namespace v2
} // namespace sqlite
