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
#ifndef GUARD_SQLITE_CAPABILITIES_HPP_INCLUDED
#define GUARD_SQLITE_CAPABILITIES_HPP_INCLUDED

/**
 * @file sqlite/capabilities.hpp
 * @brief Describes which optional SQLite API groups the library build can use.
 */
namespace sqlite {
inline namespace v2 {

    /**
     * @brief The optional SQLite API groups this library's build can use.
     *
     * The values come from the same detection that decides how the wrappers resolve the
     * symbols of each optional API: groups the build system verified for the selected
     * SQLite implementation report `true` directly (their symbols resolved at link time,
     * which works for static and shared linking alike); all other groups report whether
     * the runtime module lookup that the wrappers themselves use finds them. The values
     * therefore never contradict the behavior of the corresponding operations.
     *
     * A `true` capability states that the API group exists in this build. It does not
     * state that a particular connection is in a state that permits every operation:
     * for example, snapshots additionally require an open read transaction on a WAL
     * database. Such connection-state preconditions are reported as operation errors
     * carrying the SQLite result code, not through this struct.
     */
    struct connection_capabilities {
        /// The session/changeset APIs (`SQLITE_ENABLE_SESSION` plus
        /// `SQLITE_ENABLE_PREUPDATE_HOOK`).
        bool sessions = false;
        /// The WAL snapshot APIs (`SQLITE_ENABLE_SNAPSHOT`).
        bool snapshots = false;
        /// The serialize/deserialize APIs (absent when SQLite omits them through
        /// `SQLITE_OMIT_DESERIALIZE`).
        bool serialization = false;
    };
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_CAPABILITIES_HPP_INCLUDED
