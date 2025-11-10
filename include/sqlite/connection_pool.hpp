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

namespace sqlite {

    class connection_pool {
    public:
        using connection_factory = std::function<std::shared_ptr<connection>()>;

        class lease {
        public:
            lease() = default;
            lease(connection_pool * pool, std::shared_ptr<connection> conn);
            lease(lease && other) noexcept;
            lease & operator=(lease && other) noexcept;
            lease(lease const &) = delete;
            lease & operator=(lease const &) = delete;
            ~lease();

            connection & operator*() const;
            connection * operator->() const;
            std::shared_ptr<connection> shared() const;

        private:
            void release();
            connection_pool * pool_ = nullptr;
            std::shared_ptr<connection> connection_;
        };

        connection_pool(std::size_t capacity, connection_factory factory);

        static connection_factory make_factory(std::string db,
                                               open_mode mode = open_mode::open_or_create,
                                               filesystem_adapter_ptr fs = {});

        lease acquire();
        std::size_t capacity() const;
        std::size_t idle_count() const;
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

}

#endif
