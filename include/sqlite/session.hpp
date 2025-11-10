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
#ifndef GUARD_SQLITE_SESSION_HPP_INCLUDED
#define GUARD_SQLITE_SESSION_HPP_INCLUDED

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite {
inline namespace v2 {
    struct connection;

    struct session_options {
        bool indirect = false;   /// Track changes as indirect (ignored by session filtering)
    };

    /** \brief RAII wrapper around sqlite3_session. */
    struct session {
        session(connection & con, std::string_view schema = "main", session_options options = {});
        ~session();

        session(session && other) noexcept;
        session & operator=(session && other) noexcept;

        session(session const &) = delete;
        session & operator=(session const &) = delete;

        void attach(std::string_view table);
        void attach_all();
        void enable(bool value);
        void set_indirect(bool value);
        std::vector<unsigned char> changeset();
        std::vector<unsigned char> patchset();

        void * native_handle() const noexcept;
    private:
        std::vector<unsigned char> collect(bool patchset);
        connection * con_;
        void * handle_;
    };

    /** \brief Returns true when SQLite sessions API is available. */
    constexpr bool sessions_supported() noexcept {
#if defined(SQLITE_ENABLE_SESSION)
        return true;
#else
        return false;
#endif
    }

    void apply_changeset(connection & con, std::span<const unsigned char> data);
    void apply_patchset(connection & con, std::span<const unsigned char> data);
} // namespace v2
} // namespace sqlite

#endif //GUARD_SQLITE_SESSION_HPP_INCLUDED
