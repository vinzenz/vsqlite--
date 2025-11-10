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
#ifndef GUARD_SQLITE_SERIALIZATION_HPP_INCLUDED
#define GUARD_SQLITE_SERIALIZATION_HPP_INCLUDED

#include <span>
#include <string_view>
#include <vector>

/**
 * @file sqlite/serialization.hpp
 * @brief Wraps the optional `sqlite3_serialize` / `sqlite3_deserialize` APIs.
 *
 * These helpers snapshot an entire schema into memory or hydrate a connection from an in-memory
 * image, which is useful for testing and for shipping pre-populated databases.
 */
namespace sqlite {
inline namespace v2 {
    struct connection;

    /// Indicates whether the current build of SQLite exposes the serialize/deserialise feature.
    constexpr bool serialization_supported() noexcept {
#if defined(SQLITE_ENABLE_DESERIALIZE)
        return true;
#else
        return false;
#endif
    }

    /**
     * @brief Copies the complete database image for @p schema into a byte vector.
     *
     * @param con Open connection whose schema should be serialized.
     * @param schema Logical database name (e.g. `"main"` or `"temp"`).
     * @param flags Optional SQLite serialization flags.
     * @throws database_exception when serialization is unavailable or fails.
     */
    std::vector<unsigned char> serialize(connection & con,
                                         std::string_view schema = "main",
                                         unsigned int flags = 0);

    /**
     * @brief Replaces the contents of @p schema with the supplied serialized image.
     *
     * @param con Connection that should host the deserialized database.
     * @param image Serialized bytes previously produced by @ref serialize or another SQLite source.
     * @param schema Logical database name.
     * @param read_only When true the connection treats the schema as immutable.
     */
    void deserialize(connection & con,
                     std::span<const unsigned char> image,
                     std::string_view schema = "main",
                     bool read_only = false);
} // namespace v2
} // namespace sqlite

#endif //GUARD_SQLITE_SERIALIZATION_HPP_INCLUDED
