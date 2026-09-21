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
#ifndef GUARD_SQLITE_PRIVATE_PRIVATE_ACCESSOR_HPP_INCLUDED
#define GUARD_SQLITE_PRIVATE_PRIVATE_ACCESSOR_HPP_INCLUDED

#include <sqlite/command.hpp>
#include <sqlite/connection.hpp>
#include <sqlite/private/connection_state.hpp>

#include <memory>

struct sqlite3_stmt;

namespace sqlite {
inline namespace v2 {
    /** \brief A internal used class, shall not be used from users
     *
     */
    struct private_accessor {
        static struct sqlite3 *get_handle(connection &m_con) {
            return m_con.state_->handle;
        }
        static void acccess_check(connection &m_con) {
            m_con.access_check();
        }
        static void close(connection &m_con) {
            m_con.close();
        }
        /// Returns the shared state that keeps the connection's native
        /// resources alive while dependents (statements, results, cursors)
        /// are in use.
        static std::shared_ptr<connection_state> state(connection &con) {
            return con.state_;
        }
        /// Returns the pool lease token of the connection, or null when the
        /// connection is not leased from a pool.
        static std::shared_ptr<void> keep_alive(connection &con) {
            return con.state_->keep_alive.lock();
        }
        /// Installs the pool lease token; statement handles created while the
        /// token is alive retain it so the connection returns to its pool only
        /// after every dependent operation finished.
        static void set_keep_alive(connection &con, std::shared_ptr<void> token) {
            con.state_->keep_alive = std::move(token);
        }
        static sqlite3_stmt *statement(command &cmd) {
            return cmd.stmt;
        }
        static sqlite3_stmt *acquire_cached_statement(connection &con, std::string const &sql) {
            return con.acquire_cached_statement(sql);
        }
        static void release_cached_statement(connection &con, std::string const &sql,
                                             sqlite3_stmt *stmt) noexcept {
            release_cached_statement(*con.state_, sql, stmt);
        }
        /// Returns a statement to the state's cache without going through the
        /// facade, so it stays usable while the facade is already destroyed
        /// (deferred cleanup). noexcept like the cache contract it serves.
        static void release_cached_statement(connection_state &st, std::string const &sql,
                                             sqlite3_stmt *stmt) noexcept {
            if (!stmt)
                return;
            if (!st.handle) {
                sqlite3_finalize(stmt);
                return;
            }
            st.cache.release(sql, stmt);
        }
        static void clear_statement_cache(connection &con) {
            con.clear_statement_cache();
        }
    };
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_PRIVATE_PRIVATE_ACCESSOR_HPP_INCLUDED
