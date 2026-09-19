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
#ifndef GUARD_SQLITE_STATEMENT_CACHE_HPP_INCLUDED
#define GUARD_SQLITE_STATEMENT_CACHE_HPP_INCLUDED

#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

struct sqlite3;
struct sqlite3_stmt;

/**
 * @file sqlite/statement_cache.hpp
 * @brief LRU cache for prepared statements shared across `sqlite::connection`.
 *
 * Keeping commonly used statements around avoids parse/prepare overhead and honors SQLite's
 * recommendation to reuse `sqlite3_stmt*` objects whenever possible.
 */
namespace sqlite {
inline namespace v2 {
    struct connection;

    /// Configuration knobs for the built-in LRU statement cache.
    struct statement_cache_config {
        std::size_t capacity = 32;   ///< Maximum cached statements.
        bool enabled         = true; ///< Disable caching without destroying existing entries.
    };

    /// Signature of the hook that receives diagnostics when a statement returned to
    /// the cache cannot be reset cleanly. The hook receives the `sqlite3_reset`
    /// error text describing the statement's last evaluation, is invoked without
    /// the cache lock held, and must not throw.
    using statement_cache_error_hook = std::function<void(std::string)>;

    /// Tracks prepared statements by SQL text and hands them out on demand.
    class statement_cache {
    public:
        explicit statement_cache(statement_cache_config cfg = {});

        sqlite3_stmt *acquire(sqlite3 *db, std::string_view sql);
        /// Returns a statement to the cache. The statement is reset and its bindings are
        /// cleared before it is retained, so it releases any locks it still held and
        /// reports inactive through `sqlite3_stmt_busy()`. This path is noexcept
        /// because it runs from the destruction of a statement's final owner:
        /// whenever the statement cannot be retained (cache disabled, duplicate SQL
        /// text, a reset failure reported through the error hook, or a bookkeeping
        /// allocation failure) it is finalized instead of stored.
        void release(std::string_view sql, sqlite3_stmt *stmt) noexcept;
        void clear(sqlite3 *db);
        void reset(statement_cache_config cfg);
        statement_cache_config config() const noexcept {
            return config_;
        }

        /// Installs the hook notified when a returned statement is discarded because
        /// `sqlite3_reset` reported an error. Without a hook, reset failures are
        /// written to `std::cerr` in debug builds and ignored otherwise.
        void set_error_hook(statement_cache_error_hook hook);

    private:
        struct entry {
            std::string sql;
            sqlite3_stmt *stmt = nullptr;
        };

        using lru_list = std::list<entry>;
        using iterator = lru_list::iterator;

        statement_cache_config config_;
        lru_list lru_;
        std::unordered_map<std::string, iterator> map_;
        statement_cache_error_hook error_hook_; ///< guarded by mutex_
        mutable std::mutex mutex_;
    };
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_STATEMENT_CACHE_HPP_INCLUDED
