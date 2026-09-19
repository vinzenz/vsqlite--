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
#include <sqlite/private/connection_state.hpp>

#include <sqlite3.h>

#include <utility>

namespace sqlite {
inline namespace v2 {
    connection_state::connection_state(filesystem_adapter_ptr fs) :
        handle(nullptr),
        filesystem(std::move(fs) ? std::move(fs) : std::make_shared<default_filesystem_adapter>()),
        cache(), keep_alive(), live_statements(0) {}

    connection_state::~connection_state() {
        // Destructors must not throw. This one runs only after the public
        // facade and every dependent statement or result released the state,
        // so each checked-out statement was already returned to the cache
        // (which resets it) or finalized by its owner. Clearing the cache
        // finalizes every remaining entry exactly once; sqlite3_close_v2 then
        // consumes the handle without a busy error and without leaking a
        // zombie connection even if SQLite still held an internal reference.
        if (handle) {
            cache.clear(handle);
            sqlite3_close_v2(handle);
            handle = nullptr;
        }
    }
} // namespace v2
} // namespace sqlite
