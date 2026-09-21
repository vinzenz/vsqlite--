/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2026 Vinzenz Feenstra vinzenz.feenstra@gmail.com
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
 CONSEQUENTIAL DAMAGES (INCLUDING BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

##############################################################################*/
#ifndef GUARD_SQLITE_PRIVATE_CONNECTION_STATE_HPP_INCLUDED
#define GUARD_SQLITE_PRIVATE_CONNECTION_STATE_HPP_INCLUDED

#include <atomic>
#include <cstddef>
#include <memory>
#include <sqlite/filesystem_adapter.hpp>
#include <sqlite/statement_cache.hpp>

struct sqlite3;

namespace sqlite {
inline namespace v2 {
    /** \brief Shared native state of one connection, not part of the public API.
     *
     * The public connection facade owns a std::shared_ptr to this state, and
     * every active statement handle retains the same state: command, query,
     * execute, result, and the prepared_statement facade all keep their native
     * statement alive through command::statement_handle, which holds the state.
     * Destroying the facade therefore defers the native cleanup until the last
     * dependent finishes instead of leaving a dangling connection reference,
     * while an already created cursor keeps working.
     *
     * The statement cache lives inside the state and its entries hold plain
     * sqlite3_stmt pointers only, so the cache and the state cannot retain
     * each other.
     *
     * keep_alive is the pool lease token: connection_pool::lease installs it,
     * and statement handles created while the token is alive retain a strong
     * reference to it. A pooled connection therefore returns to its pool only
     * after the public lease and every dependent operation released the
     * token. The state itself holds only a weak reference, so the token, the
     * facade, and the state cannot form an ownership cycle.
     *
     * live_statements counts the statement handles that currently own a
     * prepared statement of this connection. An explicit close() rejects the
     * call while the count is non-zero; facade destruction defers the cleanup
     * instead of rejecting it.
     */
    struct connection_state {
        explicit connection_state(filesystem_adapter_ptr fs);

        connection_state(connection_state const &)            = delete;
        connection_state &operator=(connection_state const &) = delete;

        /// Finalizes the remaining cache entries exactly once and consumes the
        /// native handle. Runs only after the facade and every dependent
        /// statement released the state, so all checked-out statements were
        /// already returned to the cache or finalized by their owners.
        ~connection_state();

        sqlite3 *handle = nullptr;
        filesystem_adapter_ptr filesystem;
        statement_cache cache;
        std::weak_ptr<void> keep_alive;
        std::atomic<std::size_t> live_statements{0};
    };
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_PRIVATE_CONNECTION_STATE_HPP_INCLUDED
