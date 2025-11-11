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
#ifndef GUARD_SQLITE_CONNECTION_POOL_HPP_INCLUDED
#define GUARD_SQLITE_CONNECTION_POOL_HPP_INCLUDED

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <sqlite/connection.hpp>

/**
 * @file sqlite/connection_pool.hpp
 * @brief Cooperative pool that multiplexes a bounded number of `sqlite::connection` instances.
 *
 * The pool hands out scoped leases that automatically return the connection to the pool when
 * destroyed, allowing applications to share a small set of connections across many callers.
 */
namespace sqlite {
inline namespace v2 {

    /// Thread-safe pool for leasing reusable SQLite connections.
    class connection_pool {
    public:
        using connection_factory = std::function<std::shared_ptr<connection>()>;

        /// Scoped handle returned by @ref connection_pool::acquire that returns the connection on
        /// destruction.
        class lease {
        public:
            lease() = default;
            lease(connection_pool *pool, std::shared_ptr<connection> conn);
            lease(lease &&other) noexcept;
            lease &operator=(lease &&other) noexcept;
            lease(lease const &)            = delete;
            lease &operator=(lease const &) = delete;
            ~lease();

            connection &operator*() const;
            connection *operator->() const;
            std::shared_ptr<connection> shared() const;

        private:
            struct shared_state;
            void release();
            std::shared_ptr<shared_state> state_;
            std::shared_ptr<connection> connection_;
        };

        /**
         * @brief Constructs a pool with a maximum @p capacity and a @p factory used to create new
         * connections.
         */
        connection_pool(std::size_t capacity, connection_factory factory);

        /**
         * @brief Helper that captures the parameters for creating connections inside a pool
         * factory.
         */
        static connection_factory make_factory(std::string db,
                                               open_mode mode = open_mode::open_or_create,
                                               filesystem_adapter_ptr fs = {});

        /**
         * @brief Blocks until a connection is available and returns a scoped lease.
         */
        lease acquire();

        /// Maximum number of concurrent connections the pool will create.
        std::size_t capacity() const;

        /// Number of idle connections currently waiting in the pool.
        std::size_t idle_count() const;

        /// Number of connections that have been created so far.
        std::size_t created_count() const;

    private:
        friend class lease;
        void release(std::shared_ptr<connection> conn);

        connection_factory factory_;
        std::size_t capacity_;
        std::size_t created_ = 0;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::vector<std::shared_ptr<connection>> idle_;
    };

} // namespace v2
} // namespace sqlite

#endif
