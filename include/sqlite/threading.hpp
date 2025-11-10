/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2024
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
#ifndef GUARD_SQLITE_THREADING_HPP_INCLUDED
#define GUARD_SQLITE_THREADING_HPP_INCLUDED

/**
 * @file sqlite/threading.hpp
 * @brief Helpers for configuring SQLite's global threading mode.
 *
 * SQLite needs to be configured exactly once before any connections are opened; these functions
 * provide a type-safe way to do so from C++.
 */
namespace sqlite {
inline namespace v2 {

    /// Mirrors the `sqlite3_config(SQLITE_CONFIG_*)` threading options.
    enum class threading_mode {
        single_thread, ///< No internal mutexing; callers must serialize all access.
        multi_thread,  ///< Connections are thread-safe, but individual database handles are not.
        serialized     ///< Full mutexing that allows sharing a connection across threads.
    };

    /// Calls `sqlite3_config` to switch SQLite into the requested threading mode.
    bool configure_threading(threading_mode mode);

    /// Returns the currently configured `threading_mode`.
    threading_mode current_threading_mode();

} // namespace v2
} // namespace sqlite

#endif
