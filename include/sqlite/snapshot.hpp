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
#ifndef GUARD_SQLITE_SNAPSHOT_HPP_INCLUDED
#define GUARD_SQLITE_SNAPSHOT_HPP_INCLUDED

#include <string>
#include <string_view>

struct sqlite3_snapshot;

/**
 * @file sqlite/snapshot.hpp
 * @brief Wraps SQLite's snapshot and WAL-mode management APIs.
 *
 * Snapshots allow read transactions to observe a consistent view of a WAL database, while the
 * helper functions here make it easier to toggle between journal modes.
 */
namespace sqlite {
inline namespace v2 {
    struct connection;

    enum class wal_mode {
        rollback,  ///< DELETE/legacy rollback journal
        truncate,
        persist,
        memory,
        wal,
        wal2
    };

    /** \brief RAII wrapper around sqlite3_snapshot. */
    struct snapshot {
        snapshot() = default;
        ~snapshot();

        snapshot(snapshot const &) = delete;
        snapshot & operator=(snapshot const &) = delete;

        snapshot(snapshot && other) noexcept;
        snapshot & operator=(snapshot && other) noexcept;

        bool valid() const noexcept { return handle_ != nullptr; }
        explicit operator bool() const noexcept { return valid(); }

        /** \brief Release the managed sqlite3_snapshot (if owned). */
        void reset() noexcept;

        /** \brief Capture a snapshot for the provided connection/schema. */
        static snapshot take(connection & con, std::string_view schema = "main");

        /** \brief Rewind an open read transaction to this snapshot. */
        void open(connection & con, std::string_view schema = "main") const;

        sqlite3_snapshot * native_handle() const noexcept { return handle_; }
    private:
        explicit snapshot(sqlite3_snapshot * handle);
        sqlite3_snapshot * handle_ = nullptr;
    };

    /** \brief Force a specific WAL journal mode. */
    wal_mode set_wal_mode(connection & con, wal_mode mode);

    /** \brief Enable WAL, optionally preferring WAL2 with transparent fallback. */
    wal_mode enable_wal(connection & con, bool prefer_wal2 = false);

    /** \brief Query the current journal mode for a connection. */
    wal_mode get_wal_mode(connection & con);

    /** \brief Convert wal_mode to the corresponding PRAGMA token. */
    std::string_view to_string(wal_mode mode);

    /** \brief Check if the linked SQLite library exposes snapshot helpers. */
    inline constexpr bool snapshots_supported() noexcept {
    #if defined(SQLITE_ENABLE_SNAPSHOT)
        return true;
    #else
        return false;
    #endif
    }
} // namespace v2
} // namespace sqlite

#endif //GUARD_SQLITE_SNAPSHOT_HPP_INCLUDED
