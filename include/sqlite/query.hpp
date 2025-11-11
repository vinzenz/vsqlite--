/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2014 Vinzenz Feenstra vinzenz.feenstra@gmail.com
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
#ifndef GUARD_SQLITE_QUERY_HPP_INCLUDED
#define GUARD_SQLITE_QUERY_HPP_INCLUDED

#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>
#include <sqlite/command.hpp>
#include <sqlite/result.hpp>

/**
 * @file sqlite/query.hpp
 * @brief Prepared-statement helper for SELECTs that produces `sqlite::result` cursors.
 *
 * The `sqlite::query` class inherits `sqlite::command` so you can bind parameters with the same
 * interface and then enumerate rows via `each()` (range-based for) or by manually consuming a
 * `sqlite::result` from `get_result()`.
 */
namespace sqlite {
inline namespace v2 {

    /** \brief query should be used to execute SQL queries
      * An object of this class is not copyable
      */
    struct query : command {
        /** \brief constructor
          * \param con reference to the connection which should be used
          * \param sql is the SQL query statement
          */
        query(connection & con, std::string const & sql);

        /** \brief destructor
          *
          */
        virtual ~query();

        class result_range {
        public:
            struct column_cache {
                std::vector<std::string> names;
                std::unordered_map<std::string, int> lookup;
                int index_of(std::string_view name) const;
            };

            class row_view {
            public:
                row_view() = default;
                row_view(result * res, std::shared_ptr<column_cache> cache)
                    : res_(res)
                    , cache_(std::move(cache)) {}

                bool valid() const noexcept { return res_ != nullptr; }

                result & raw() const {
                    if(!res_) {
                        throw std::runtime_error("row_view is not bound to a row");
                    }
                    return *res_;
                }

                template <typename T>
                T get(std::string_view name) const {
                    return raw().get<T>(column(name));
                }

                template <typename T>
                T get(int idx) const {
                    return raw().get<T>(idx);
                }

                template <typename T>
                T operator[](std::string_view name) const {
                    return get<T>(name);
                }

            private:
                int column(std::string_view name) const;

                result * res_ = nullptr;
                std::shared_ptr<column_cache> cache_;
            };

            class iterator {
            public:
                using iterator_category = std::input_iterator_tag;
                using value_type = row_view;
                using difference_type = std::ptrdiff_t;
                using pointer = row_view *;
                using reference = row_view &;

                iterator();
                iterator(result_type res,
                         std::shared_ptr<column_cache> cache,
                         bool end);
                reference operator*() const;
                pointer operator->() const;
                iterator & operator++();
                iterator operator++(int);
                bool operator==(iterator const & other) const;
                bool operator!=(iterator const & other) const;
            private:
                void advance();
                void prime_cache();
                result_type result_;
                bool end_ = true;
                std::shared_ptr<column_cache> cache_;
                row_view current_;
            };

            result_range();
            explicit result_range(result_type res);

            iterator begin();
            iterator end() const;

        private:
            result_type result_;
            bool begin_called_ = false;
            std::shared_ptr<column_cache> cache_;
        };

        /** \brief executes the sql command (deprecated, prefer each())
          * \return result_type which is std::shared_ptr<result>
          */
        VSQLITE_DEPRECATED result_type emit_result();

        /** \brief returns the results (needs a previous emit() call)
          * \return result_type which is std::shared_ptr<result>
          */
        result_type get_result();

        /** \brief Returns a range view for range-based for loops. */
        result_range each();
    private:
        friend struct result;
        void access_check();
        bool step();
    };
} // namespace v2
} // namespace sqlite

#endif //GUARD_SQLITE_QUERY_HPP_INCLUDED
