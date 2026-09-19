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
#ifndef GUARD_SQLITE_DETAIL_CONVERSION_HPP_INCLUDED
#define GUARD_SQLITE_DETAIL_CONVERSION_HPP_INCLUDED

#include <chrono>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <sqlite/database_exception.hpp>

#include <sqlite3.h>

/**
 * @file sqlite/detail/conversion.hpp
 * @brief The single conversion policy shared by parameter binding, row
 *        extraction, and SQL function callbacks.
 *
 * Three thin adapters expose the native SQLite operation each surface needs:
 * - @ref sqlite::detail::conversion::bind_adapter writes one bound parameter
 *   through `sqlite3_bind_*` (used by `sqlite::command`),
 * - @ref sqlite::detail::conversion::column_adapter reads one result column
 *   through `sqlite3_column_*` (used by `sqlite::result`),
 * - @ref sqlite::detail::conversion::value_adapter and
 *   @ref sqlite::detail::conversion::context_adapter read and write SQL
 *   function arguments and results through `sqlite3_value_*` and
 *   `sqlite3_result_*` (used by `sqlite/function.hpp`).
 *
 * Every policy decision - NULL meaning, the empty-value-with-non-null-pointer
 * rule, legacy numeric coercions, checked numeric conversions, and the
 * microsecond chrono representation - lives here exactly once. The full
 * mapping table for all three surfaces is documented in docs/conversions.md.
 */
namespace sqlite {
inline namespace v2 {
    namespace detail {
        namespace conversion {

            /// SQLite turns a null data pointer into SQL NULL, whatever the
            /// byte count says. Empty text and empty blobs must therefore pass a
            /// non-null dummy pointer with length zero so they stay distinct
            /// from NULL on every surface (binding, extraction, callbacks).
            template <typename T> inline T const *data_or_empty(T const *data, std::size_t size) {
                static constexpr T kEmpty{0};
                return size == 0 ? &kEmpty : data;
            }

            /// Writes one bound statement parameter. Returned ints are raw
            /// `sqlite3_bind_*` result codes; callers translate them.
            struct bind_adapter {
                sqlite3_stmt *stmt;
                int index;

                int bind_null() const {
                    return sqlite3_bind_null(stmt, index);
                }

                int bind_int(int value) const {
                    return sqlite3_bind_int(stmt, index, value);
                }

                int bind_int64(std::int64_t value) const {
                    return sqlite3_bind_int64(stmt, index, value);
                }

                int bind_double(double value) const {
                    return sqlite3_bind_double(stmt, index, value);
                }

                /// Applies the shared empty-value rule: zero-length text binds
                /// as an empty string, never as NULL.
                int bind_text(std::string_view text) const {
                    return sqlite3_bind_text(stmt, index, data_or_empty(text.data(), text.size()),
                                             static_cast<int>(text.size()), SQLITE_TRANSIENT);
                }

                /// Applies the shared empty-value rule: zero-length data binds
                /// as an empty blob, never as NULL.
                int bind_blob(std::span<const unsigned char> data) const {
                    return sqlite3_bind_blob(stmt, index, data_or_empty(data.data(), data.size()),
                                             static_cast<int>(data.size()), SQLITE_TRANSIENT);
                }
            };

            /// Reads one result column of the current row.
            struct column_adapter {
                sqlite3_stmt *stmt;
                int index;

                bool is_null() const {
                    return sqlite3_column_type(stmt, index) == SQLITE_NULL;
                }

                int int_value() const {
                    return sqlite3_column_int(stmt, index);
                }

                std::int64_t int64_value() const {
                    return sqlite3_column_int64(stmt, index);
                }

                double double_value() const {
                    return sqlite3_column_double(stmt, index);
                }

                char const *text_value() const {
                    return reinterpret_cast<char const *>(sqlite3_column_text(stmt, index));
                }

                unsigned char const *blob_value() const {
                    return static_cast<unsigned char const *>(sqlite3_column_blob(stmt, index));
                }

                int byte_count() const {
                    return sqlite3_column_bytes(stmt, index);
                }
            };

            /// Reads one SQL function argument.
            struct value_adapter {
                sqlite3_value *value;

                bool is_null() const {
                    return sqlite3_value_type(value) == SQLITE_NULL;
                }

                int int_value() const {
                    return sqlite3_value_int(value);
                }

                std::int64_t int64_value() const {
                    return sqlite3_value_int64(value);
                }

                double double_value() const {
                    return sqlite3_value_double(value);
                }

                char const *text_value() const {
                    return reinterpret_cast<char const *>(sqlite3_value_text(value));
                }

                unsigned char const *blob_value() const {
                    return static_cast<unsigned char const *>(sqlite3_value_blob(value));
                }

                int byte_count() const {
                    return sqlite3_value_bytes(value);
                }
            };

            /// Writes one SQL function result. Text and blob writes copy the
            /// data (SQLITE_TRANSIENT), so borrowed arguments stay valid for
            /// the invocation in which they are serialized.
            struct context_adapter {
                sqlite3_context *ctx;

                void result_null() const {
                    sqlite3_result_null(ctx);
                }

                void result_int(int value) const {
                    sqlite3_result_int(ctx, value);
                }

                void result_int64(std::int64_t value) const {
                    sqlite3_result_int64(ctx, value);
                }

                void result_double(double value) const {
                    sqlite3_result_double(ctx, value);
                }

                /// Applies the shared empty-value rule: zero-length text
                /// results stay text, never NULL.
                void result_text(std::string_view text) const {
                    sqlite3_result_text(ctx, data_or_empty(text.data(), text.size()),
                                        static_cast<int>(text.size()), SQLITE_TRANSIENT);
                }

                /// Applies the shared empty-value rule: zero-length blob
                /// results stay blobs, never NULL.
                template <typename T> void result_blob(T const *data, std::size_t size) const {
                    sqlite3_result_blob(ctx, data_or_empty(data, size), static_cast<int>(size),
                                        SQLITE_TRANSIENT);
                }
            };

            /// Legacy extraction coercion: SQL NULL reads as 0.
            inline int legacy_int(column_adapter column) {
                return column.is_null() ? 0 : column.int_value();
            }

            /// Legacy extraction coercion: SQL NULL reads as 0.
            inline std::int64_t legacy_int64(column_adapter column) {
                return column.is_null() ? std::int64_t{0} : column.int64_value();
            }

            /// Legacy extraction coercion: SQL NULL reads as 0.0.
            inline double legacy_double(column_adapter column) {
                return column.is_null() ? 0.0 : column.double_value();
            }

            /// Legacy extraction coercion: SQL NULL reads as the text "NULL".
            inline std::string_view legacy_text(column_adapter column) {
                if (column.is_null()) {
                    return std::string_view("NULL", 4);
                }
                char const *text = column.text_value();
                return std::string_view(text, static_cast<std::size_t>(column.byte_count()));
            }

            /// Legacy extraction coercion: SQL NULL reads as zero bytes.
            inline std::size_t legacy_blob_size(column_adapter column) {
                return column.is_null() ? std::size_t{0}
                                        : static_cast<std::size_t>(column.byte_count());
            }

            /// Legacy extraction coercion: SQL NULL reads as an empty span.
            inline std::span<const unsigned char> legacy_blob(column_adapter column) {
                if (column.is_null()) {
                    return {};
                }
                std::size_t size = static_cast<std::size_t>(column.byte_count());
                return std::span<const unsigned char>(column.blob_value(), size);
            }

            /// Legacy numeric cast: narrows like a C static_cast, wrapping on
            /// overflow and losing sign. Pinned by regression tests so a future
            /// change cannot happen silently.
            template <typename To> To legacy_narrow(std::int64_t value) {
                static_assert(std::is_integral_v<To>,
                              "legacy_narrow expects an integral destination type");
                return static_cast<To>(value);
            }

            /// Checked numeric conversion for the strict access path: rejects
            /// out-of-range narrowing and signedness loss instead of wrapping.
            template <typename To> To checked_narrow(std::int64_t value) {
                static_assert(std::is_integral_v<To> && !std::is_same_v<To, bool>,
                              "checked_narrow expects a non-boolean integral destination type");
                if (!std::in_range<To>(value)) {
                    throw database_exception(
                        "sqlite: checked conversion failed because the integer value does not "
                        "fit the requested type");
                }
                return static_cast<To>(value);
            }

            /// Checked floating point conversion for the strict access path:
            /// rejects values outside the representable range of a narrower
            /// floating point type instead of turning them into infinity.
            template <typename To> To checked_narrow(double value) {
                static_assert(std::is_floating_point_v<To>,
                              "checked_narrow expects a floating point destination type");
                if constexpr (sizeof(To) < sizeof(double)) {
                    To narrowed = static_cast<To>(value);
                    if (std::isinf(static_cast<double>(narrowed)) && std::isfinite(value)) {
                        throw database_exception(
                            "sqlite: checked conversion failed because the value does not fit "
                            "the requested floating point range");
                    }
                    return narrowed;
                } else {
                    return static_cast<To>(value);
                }
            }

            /// Chrono values are stored as microseconds since the epoch,
            /// truncated towards zero. Sub-microsecond precision is lost on
            /// bind and read. A count beyond the int64 range is not
            /// representable; the truncating cast wraps.
            template <typename Rep, typename Period>
            inline std::int64_t
            microseconds_count(std::chrono::duration<Rep, Period> const &value) {
                return static_cast<std::int64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(value).count());
            }

            /// @copydoc microseconds_count(std::chrono::duration<Rep, Period> const &)
            template <typename Clock, typename Duration>
            inline std::int64_t
            microseconds_count(std::chrono::time_point<Clock, Duration> const &value) {
                return static_cast<std::int64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(value.time_since_epoch())
                        .count());
            }

            /// Rebuilds a duration from the stored microsecond count. Finer
            /// destination periods scale the count up; coarser periods
            /// truncate towards zero.
            template <typename Duration>
            inline Duration duration_from_microseconds(std::int64_t micros) {
                return std::chrono::duration_cast<Duration>(std::chrono::microseconds{micros});
            }

            /// Rebuilds a time point from the stored microsecond count.
            template <typename TimePoint>
            inline TimePoint time_point_from_microseconds(std::int64_t micros) {
                return TimePoint(std::chrono::duration_cast<typename TimePoint::duration>(
                    std::chrono::microseconds{micros}));
            }

        } // namespace conversion
    } // namespace detail
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_DETAIL_CONVERSION_HPP_INCLUDED
