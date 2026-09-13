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

#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <type_traits>
#include <variant>
#include <memory>
#include <sqlite/command.hpp>
#include <sqlite/detail/type_helpers.hpp>
#include <sqlite/ext/variant.hpp>
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
        query(connection &con, std::string const &sql);

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

            class iterator;

            class row_view {
            public:
                row_view() = default;
                row_view(result *res, std::shared_ptr<column_cache> cache) :
                    res_(res), cache_(std::move(cache)) {}

                bool valid() const noexcept {
                    return res_ != nullptr;
                }

                result &raw() const {
                    if (!res_) {
                        throw std::runtime_error("row_view is not bound to a row");
                    }
                    return *res_;
                }

                template <typename T> T get(std::string_view name) const {
                    return raw().get<T>(column(name));
                }

                template <typename T> T get(int idx) const {
                    return raw().get<T>(idx);
                }

                template <typename T> T operator[](std::string_view name) const {
                    return get<T>(name);
                }

            private:
                int column(std::string_view name) const;

                result *res_ = nullptr;
                std::shared_ptr<column_cache> cache_;
            };

            /**
             * \brief Owning snapshot of a single result row.
             *
             * The snapshot is taken while the row is still current and remains readable after the
             * iterator advanced, e.g. when dereferencing the result of the postfix increment
             * operator (`*it++`). Values are stored as \ref variant_t, one entry per column, so
             * the snapshot is independent of the underlying forward-only \ref result cursor.
             *
             * Conversions mirror \ref result::get with one exception: because the original SQLite
             * storage class is preserved per column, reading a value with a type from a different
             * storage class (e.g. `get<std::string>` on an INTEGER column, which SQLite would
             * render as text) throws \ref database_exception instead of applying SQLite's implicit
             * coercion. Numeric conversions between INTEGER and REAL as well as NULL handling
             * (`0`, `0.0`, `"NULL"`, empty blobs, `std::nullopt` for optionals) match \ref result.
             *
             * Views returned by `get<std::string_view>` and the blob spans reference data owned by
             * the snapshot and stay valid as long as the `row` object lives.
             */
            class row {
            public:
                row() = default;

                /// Returns true when the snapshot was taken from an actual result row.
                bool valid() const noexcept {
                    return cache_ != nullptr;
                }

                /// Extracts the column at @p idx into an arbitrary C++ type (see class comment).
                template <typename T> T get(int idx) const {
                    access_check(idx);
                    return get_from_variant<T>(values_[static_cast<std::size_t>(idx)]);
                }

                /// Extracts the named column into an arbitrary C++ type (see class comment).
                template <typename T> T get(std::string_view name) const {
                    return get<T>(column(name));
                }

                /// Extracts the named column into an arbitrary C++ type (see class comment).
                template <typename T> T operator[](std::string_view name) const {
                    return get<T>(name);
                }

            private:
                friend class iterator;

                row(result &res, std::shared_ptr<column_cache> cache);

                void access_check(int idx) const {
                    if (!valid()) {
                        throw std::runtime_error("row does not hold any values");
                    }
                    if (idx < 0 || idx >= static_cast<int>(values_.size())) {
                        throw std::out_of_range("no such column index");
                    }
                }

                int column(std::string_view name) const;

                static std::int64_t as_int64(variant_t const &value);
                static double as_double(variant_t const &value);
                static std::string as_string(variant_t const &value);
                static std::string_view as_string_view(variant_t const &value);
                static std::span<const unsigned char> as_bytes(variant_t const &value);

                template <typename T> static T get_from_variant(variant_t const &value) {
                    using decayed = detail::decay_t<T>;
                    if constexpr (detail::is_optional_v<decayed>) {
                        if (std::holds_alternative<null_t>(value)) {
                            return std::nullopt;
                        }
                        using value_type = typename detail::optional_value<decayed>::type;
                        return get_from_variant<value_type>(value);
                    } else if constexpr (detail::is_duration_v<decayed>) {
                        auto micros = std::chrono::microseconds{as_int64(value)};
                        return std::chrono::duration_cast<decayed>(micros);
                    } else if constexpr (detail::is_time_point_v<decayed>) {
                        auto micros   = std::chrono::microseconds{as_int64(value)};
                        auto duration = std::chrono::duration_cast<typename decayed::duration>(micros);
                        return decayed(duration);
                    } else if constexpr (std::is_enum_v<decayed>) {
                        return static_cast<decayed>(as_int64(value));
                    } else if constexpr (std::is_integral_v<decayed> && !std::is_same_v<decayed, bool>) {
                        return static_cast<decayed>(as_int64(value));
                    } else if constexpr (std::is_same_v<decayed, bool>) {
                        return as_int64(value) != 0;
                    } else if constexpr (std::is_floating_point_v<decayed>) {
                        return static_cast<decayed>(as_double(value));
                    } else if constexpr (std::is_same_v<decayed, std::string>) {
                        return as_string(value);
                    } else if constexpr (std::is_same_v<decayed, std::string_view>) {
                        return as_string_view(value);
                    } else if constexpr (detail::is_byte_vector_v<decayed>) {
                        auto bytes = as_bytes(value);
                        return std::vector<unsigned char>(bytes.begin(), bytes.end());
                    } else if constexpr (detail::is_unsigned_char_span_v<decayed>) {
                        return as_bytes(value);
                    } else if constexpr (detail::is_byte_span_v<decayed>) {
                        auto bytes = as_bytes(value);
                        return std::span<const std::byte>(reinterpret_cast<std::byte const *>(bytes.data()),
                                                          bytes.size());
                    } else {
                        static_assert(detail::always_false_v<decayed>,
                                      "Unsupported type for sqlite::query::result_range::row::get<T>");
                    }
                }

                std::vector<variant_t> values_;
                std::shared_ptr<column_cache> cache_;
            };

            /**
             * \brief Forward-only input iterator over the rows of a \ref result_range.
             *
             * Dereferencing yields a \ref row_view that references the current row of the shared
             * \ref result. As with every input iterator, the referenced row is only valid until
             * the iterator is incremented.
             *
             * The postfix increment operator returns a \ref postfix_proxy that owns a \ref row
             * snapshot of the row that preceded the increment, so `*it++` observes the old row
             * instead of the row the iterator was advanced to.
             */
            class iterator {
            public:
                using iterator_category = std::input_iterator_tag;
                using value_type        = row_view;
                using difference_type   = std::ptrdiff_t;
                using pointer           = row_view *;
                using reference         = row_view &;

                /**
                 * \brief Result of the postfix increment operator.
                 *
                 * Owns the row snapshot taken before the increment. Dereferencing yields that
                 * row; the object deliberately does not convert back to an iterator because the
                 * iterator itself has already advanced past the snapshotted row.
                 */
                class postfix_proxy {
                public:
                    explicit postfix_proxy(row r) : row_(std::move(r)) {}

                    row const &operator*() const {
                        return row_;
                    }

                    row const *operator->() const {
                        return &row_;
                    }

                private:
                    row row_;
                };

                iterator();
                iterator(result_type res, std::shared_ptr<column_cache> cache, bool end);
                reference operator*() const;
                pointer operator->() const;
                iterator &operator++();
                postfix_proxy operator++(int);
                bool operator==(iterator const &other) const;
                bool operator!=(iterator const &other) const;

            private:
                row current_row() const;
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

        /** \brief returns the results (needs a previous step_once() call)
         * \return result_type which is std::shared_ptr<result>
         */
        result_type get_result();

        /** \brief Returns a range view for range-based for loops. */
        result_range each();

        /** \brief Binds any supplied parameters and returns a range view.
         *
         * Mirrors the variadic <code>command::operator()</code>: arguments are bound via the
         * streaming operator and the query remains otherwise untouched, so callers can mix manual
         * <code>%</code> binding with this overload.
         */
        template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) > 0)>>
        result_range each(Args &&...args) {
            ((void)(*this % std::forward<Args>(args)), ...);
            return each();
        }

    private:
        friend struct result;
        void access_check();
        bool step();
    };
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_QUERY_HPP_INCLUDED
