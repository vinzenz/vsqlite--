/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2024 Vinzenz Feenstra
                         and contributors
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

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
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <sqlite/connection.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/private/private_accessor.hpp>
#include <sqlite/serialization.hpp>

#include <sqlite3.h>

#include "dynamic_symbols.hpp"

namespace {
sqlite3 *get_handle(sqlite::connection &con) {
    sqlite::private_accessor::acccess_check(con);
    return sqlite::private_accessor::get_handle(con);
}

std::string normalize_schema(std::string_view schema) {
    if (schema.empty()) {
        return "main";
    }
    return std::string(schema);
}
struct serialization_api {
    using serialize_fn   = decltype(&sqlite3_serialize);
    using deserialize_fn = decltype(&sqlite3_deserialize);

    serialize_fn serialize     = nullptr;
    deserialize_fn deserialize = nullptr;
};

serialization_api const &serialization_symbols() {
    static serialization_api api = [] {
        serialization_api loaded;
        loaded.serialize = sqlite::detail::load_sqlite_symbol<serialization_api::serialize_fn>(
            "sqlite3_serialize");
        loaded.deserialize = sqlite::detail::load_sqlite_symbol<serialization_api::deserialize_fn>(
            "sqlite3_deserialize");
        return loaded;
    }();
    return api;
}

void ensure_serialization_available() {
    if (!sqlite::serialization_supported()) {
        throw sqlite::database_exception(
            "SQLite serialization APIs are not available in this build.");
    }
}
} // namespace

namespace sqlite {
inline namespace v2 {
    bool serialization_supported() noexcept {
        auto const &api = serialization_symbols();
        return api.serialize != nullptr && api.deserialize != nullptr;
    }

    std::vector<unsigned char> serialize(connection &con, std::string_view schema,
                                         unsigned int flags) {
        ensure_serialization_available();
        sqlite3_int64 size  = 0;
        auto normalized     = normalize_schema(schema);
        auto fn             = serialization_symbols().serialize;
        unsigned char *blob = fn ? fn(get_handle(con), normalized.c_str(), &size, flags) : nullptr;
        if (!blob) {
            throw database_exception("Failed to serialize database image.");
        }
        std::vector<unsigned char> out(blob, blob + size);
        sqlite3_free(blob);
        return out;
    }

    void deserialize(connection &con, std::span<const unsigned char> image, std::string_view schema,
                     bool read_only) {
        ensure_serialization_available();
        if (image.empty()) {
            throw database_exception("Serialized database image is empty.");
        }
        auto normalized       = normalize_schema(schema);
        auto size             = static_cast<sqlite3_int64>(image.size());
        unsigned char *buffer = static_cast<unsigned char *>(sqlite3_malloc64(image.size()));
        if (!buffer) {
            throw database_exception("Failed to allocate buffer for sqlite3_deserialize.");
        }
        std::memcpy(buffer, image.data(), image.size());
        unsigned flags = SQLITE_DESERIALIZE_FREEONCLOSE;
        if (read_only) {
            flags |= SQLITE_DESERIALIZE_READONLY;
        }
        auto fn = serialization_symbols().deserialize;
        int rc =
            fn ? fn(get_handle(con), normalized.c_str(), buffer, size, size, flags) : SQLITE_ERROR;
        if (rc != SQLITE_OK) {
            sqlite3_free(buffer);
            throw database_exception_code(sqlite3_errmsg(get_handle(con)), rc);
        }
    }
} // namespace v2
} // namespace sqlite
