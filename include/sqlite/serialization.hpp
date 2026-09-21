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

#include <sqlite3.h>

/**
 * @file sqlite/serialization.hpp
 * @brief Wraps the optional `sqlite3_serialize` / `sqlite3_deserialize` APIs.
 *
 * These helpers snapshot an entire schema into memory or hydrate a connection from an in-memory
 * image, which is useful for testing and for shipping pre-populated databases.
 *
 * Error taxonomy:
 * - Build capability absent: when the SQLite implementation this library uses was built with
 *   `SQLITE_OMIT_DESERIALIZE`, the operations throw a `database_exception` whose message
 *   contains "not available in this build" together with the capability name (`serialization`)
 *   and the SQLite build flag that enables it. Query
 *   `sqlite::connection::capabilities()` to branch on this case without exceptions.
 * - Connection-state/operation errors: when the capability exists but the operation fails
 *   (e.g. deserializing into an unknown schema), the helpers throw a
 *   `database_exception_code` carrying the SQLite result code where SQLite reports one.
 */
namespace sqlite {
inline namespace v2 {
    struct connection;

    /// Indicates whether the linked SQLite exposes the serialize/deserialise feature.
    bool serialization_supported() noexcept;

    /**
     * @brief Typed options for @ref serialize, mapped to the safe subset of SQLite's
     * serialization flags.
     *
     * The wrapper only exposes options that keep the owning-copy contract of the result:
     * raw `SQLITE_SERIALIZE_*` flag values have no typed equivalent here.
     */
    struct serialize_options {
        /**
         * Serialize from the connection's own in-memory image instead of letting SQLite
         * allocate and fill a new buffer (maps to `SQLITE_SERIALIZE_NOCOPY`).
         *
         * The wrapper still copies the bytes into the returned vector, so the result
         * owns its data as always; the connection keeps its buffer. SQLite can only
         * honor this mode for a contiguous in-memory image (e.g. an in-memory or
         * deserialized database), and the operation fails with a `database_exception`
         * otherwise.
         */
        bool use_connection_image = false;
    };

    /**
     * @brief Copies the complete database image for @p schema into a byte vector.
     *
     * The returned vector always owns its bytes. When
     * @p options.use_connection_image is set, SQLite hands out its own in-memory image
     * without making a copy; those bytes are copied into the result and the buffer
     * remains owned by the connection. Because no allocation is made in that case, an
     * exception is thrown when no contiguous in-memory image exists (e.g. for a
     * file-backed database).
     *
     * @param con Open connection whose schema should be serialized.
     * @param schema Logical database name (e.g. `"main"` or `"temp"`).
     * @param options Typed serialization options.
     * @throws database_exception when serialization is unavailable or fails.
     */
    std::vector<unsigned char> serialize(connection &con, std::string_view schema = "main",
                                         serialize_options options = {});

    /**
     * @brief Deprecated raw-flag adapter of @ref serialize.
     *
     * @param flags SQLite serialization flags; only `SQLITE_SERIALIZE_NOCOPY` has a
     *              meaning and maps to `serialize_options::use_connection_image`.
     * @deprecated Pass @ref serialize_options instead. The typed options preserve the
     *             owning-copy contract; raw flags can request combinations the wrapper
     *             does not guarantee.
     */
    [[deprecated("pass sqlite::serialize_options instead of raw SQLITE_SERIALIZE_* flags")]]
    inline std::vector<unsigned char> serialize(connection &con, std::string_view schema,
                                                unsigned int flags) {
        serialize_options options;
        options.use_connection_image = (flags & SQLITE_SERIALIZE_NOCOPY) != 0;
        return serialize(con, schema, options);
    }

    /**
     * @brief Replaces the contents of @p schema with the supplied serialized image.
     *
     * The image is copied into a buffer whose ownership passes to SQLite, so the caller's
     * @p image may be released or reused as soon as the call returns.
     *
     * @param con Connection that should host the deserialized database.
     * @param image Serialized bytes previously produced by @ref serialize or another SQLite source.
     * @param schema Logical database name.
     * @param read_only When true the connection treats the schema as immutable and write
     *                 attempts fail. Otherwise the database is permitted to grow beyond the
     *                 original image size, with SQLite reallocating its buffer on demand.
     * @throws database_exception_code with the SQLite result code when SQLite rejects the
     *         operation (e.g. for an unknown schema).
     */
    void deserialize(connection &con, std::span<const unsigned char> image,
                     std::string_view schema = "main", bool read_only = false);
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_SERIALIZATION_HPP_INCLUDED
