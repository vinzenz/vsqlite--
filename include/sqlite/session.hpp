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

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file sqlite/session.hpp
 * @brief Thin RAII layer over the SQLite sessions/changeset extension.
 *
 * Sessions capture row-level changes in memory so they can later be applied or merged across
 * connections, which is useful for sync workflows and replicating WAL streams.
 */
namespace sqlite {
inline namespace v2 {
    struct connection;

    /// Configuration flags supplied when opening a new change session.
    struct session_options {
        bool indirect = false; /// Track changes as indirect (ignored by session filtering)
    };

    /**
     * @brief RAII wrapper around `sqlite3_session`.
     *
     * Call `attach()` or `attach_all()` to declare which tables to monitor, then retrieve
     * accumulated changes via `changeset()` / `patchset()`.
     */
    struct session {
        session(connection &con, std::string_view schema = "main", session_options options = {});
        ~session();

        session(session &&other) noexcept;
        session &operator=(session &&other) noexcept;

        session(session const &)            = delete;
        session &operator=(session const &) = delete;

        void attach(std::string_view table);
        void attach_all();
        void enable(bool value);
        void set_indirect(bool value);
        /// Serializes the recorded changes in the standard changeset format.
        std::vector<unsigned char> changeset();
        /// Serializes the recorded changes in the smaller patchset format.
        std::vector<unsigned char> patchset();

        void *native_handle() const noexcept;

    private:
        std::vector<unsigned char> collect(bool patchset);
        connection *con_;
        void *handle_;
    };

    /** \brief Returns true when SQLite sessions API is available. */
    bool sessions_supported() noexcept;

    /// Why a conflict handler was invoked while applying a changeset or patchset.
    enum class changeset_conflict_type {
        /// A matching row exists, but its current values differ from the recorded old values.
        data,
        /// A row expected to exist for an update or delete was not found.
        not_found,
        /// Applying the change would violate a uniqueness constraint (e.g. a duplicate primary key).
        conflict,
        /// Applying the change would violate some other constraint.
        constraint,
        /// Applying the changeset would leave a foreign key constraint violated.
        foreign_key,
    };

    /// Row operation carried by a conflicting changeset entry.
    enum class changeset_operation {
        unknown, ///< Operation details are unavailable in this build.
        insert,
        remove,
        update,
    };

    /// Describes a conflict encountered while applying a changeset or patchset.
    struct changeset_conflict {
        /// Why the change conflicted.
        changeset_conflict_type type;
        /// The operation recorded for the conflicting change.
        changeset_operation operation;
        /// Name of the affected table (empty when unavailable).
        std::string_view table;

        /// Returns true when conflict_policy::replace is a valid response to this conflict.
        bool replace_supported() const noexcept {
            return type == changeset_conflict_type::data ||
                   type == changeset_conflict_type::conflict;
        }
    };

    /// How to resolve a conflicting change.
    enum class conflict_policy {
        /// Skip the conflicting change and continue with the remaining changes.
        omit,
        /// Remove conflicting rows and apply the change; only valid for `data` and
        /// `conflict` types, other conflicts treat it like `abort` (SQLITE_MISUSE).
        replace,
        /// Abort the application; changes applied so far are rolled back by SQLite.
        abort,
    };

    /// Callback invoked for every conflict encountered while applying changes.
    using conflict_handler = std::function<conflict_policy(changeset_conflict const &)>;

    /// Applies a changeset produced by @ref session::changeset onto @p con.
    ///
    /// Conflicts (duplicate primary keys, constraint violations, ...) abort the application:
    /// a database_exception is thrown and SQLite rolls back any change applied so far.
    void apply_changeset(connection &con, std::span<const unsigned char> data);
    /// Applies a changeset, resolving every conflict with the given fixed @p policy.
    void apply_changeset(connection &con, std::span<const unsigned char> data,
                         conflict_policy policy);
    /// Applies a changeset, consulting @p handler for every conflict.
    ///
    /// An empty @p std::function behaves like conflict_policy::abort. Exceptions thrown by
    /// @p handler are caught at the C boundary, abort the application, and are rethrown to
    /// the caller once SQLite has unwound.
    void apply_changeset(connection &con, std::span<const unsigned char> data,
                         conflict_handler handler);
    /// Applies a patchset produced by @ref session::patchset onto @p con.
    ///
    /// Conflicts abort the application like the two-argument @ref apply_changeset overload.
    void apply_patchset(connection &con, std::span<const unsigned char> data);
    /// Applies a patchset, resolving every conflict with the given fixed @p policy.
    void apply_patchset(connection &con, std::span<const unsigned char> data,
                        conflict_policy policy);
    /// Applies a patchset, consulting @p handler for every conflict (see @ref apply_changeset).
    void apply_patchset(connection &con, std::span<const unsigned char> data,
                        conflict_handler handler);
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_SESSION_HPP_INCLUDED
