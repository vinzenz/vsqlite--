/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2014 Vinzenz Feenstra vinzenz.feenstra@gmail.com
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

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
#ifndef GUARD_SQLITE_RESULT_HPP_INCLUDED
#define GUARD_SQLITE_RESULT_HPP_INCLUDED

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

#include <sqlite/database_exception.hpp>
#include <sqlite/deprecated.hpp>
#include <sqlite/detail/type_helpers.hpp>
#include <sqlite/ext/variant.hpp>

/**
 * @file sqlite/result.hpp
 * @brief Row-oriented cursor and typed accessors returned by `sqlite::query`.
 *
 * The header defines the `sqlite::result` type along with helpers such as `result_type`, the
 * templated `get<T>` conversion logic, and tuple extraction utilities.
 */
namespace sqlite {
inline namespace v2 {
struct query;
struct result_construct_params_private;
namespace detail {
bool end(result_construct_params_private const &);
void reset(result_construct_params_private &);
} // namespace detail

/**
 * @brief Forward-only cursor over the rows produced by a prepared statement.
 *
 * Instances are created and owned by @ref query and encapsulate the currently
 * bound statement plus its execution state. The cursor is non-copyable, but it
 * keeps the underlying resources alive through shared ownership until both the
 * query and the result go out of scope.
 */
struct result {
private:
  typedef std::shared_ptr<result_construct_params_private> construct_params;
  friend struct query;
  result(construct_params);
  result(result const &) = delete;
  result &operator=(result const &) = delete;

public:
  /// Destroys the cursor but leaves the underlying statement intact if it is still shared.
  ~result();

  /**
   * @brief Advances to the next row.
   *
   * @returns true when a fresh row is available, false when the result set is exhausted.
   * @throws std::runtime_error if the underlying statement was already destroyed.
   */
  bool next_row();

  /**
   * @brief Checks whether @ref next_row already consumed the last row.
   *
   * @returns true when the cursor is positioned after the final row.
   * @throws std::runtime_error if the result is no longer valid.
   */
  inline bool end() const {
    if (!m_params)
      throw std::runtime_error("Invalid memory access");
    return detail::end(*m_params);
  }

  /**
   * @brief Resets the cursor to the beginning without re-binding parameters.
   *
   * Use this when you need to re-iterate the same result set after calling @ref next_row.
   * @throws std::runtime_error if the result is no longer valid.
   */
  void reset() {
    if (!m_params)
      throw std::runtime_error("Invalid memory access");
    detail::reset(*m_params);
  }

  /**
   * @deprecated This reflects sqlite3_changes() and therefore the count of rows written,
   *             not the total rows returned by a SELECT.
   */
  VSQLITE_DEPRECATED int get_row_count();

  /**
   * @brief Returns the number of columns exposed by the current statement.
   *
   * @returns Number of columns (>= 0). This value never changes while the result lives.
   */
  int get_column_count();

  /**
   * @brief Reports the SQLite storage class for the value at index @p idx.
   *
   * @param idx Zero-based column index.
   * @returns A value from the @ref type enumeration.
   * @throws std::out_of_range when @p idx is outside `[0, get_column_count())`.
   */
  type get_column_type(int idx);

  /**
   * @brief Returns the declared type of the column at index @p idx.
   *
   * The result mirrors `sqlite3_column_decltype` and therefore reflects the schema's
   * declared affinity (e.g. `"INTEGER"` or `"TEXT"`).
   */
  std::string get_column_decltype(int idx);

  /**
   * @brief Materializes the current value as @ref variant_t.
   *
   * @param index Zero-based column index.
   * @returns A populated variant storing one of the supported SQLite types.
   */
  variant_t get_variant(int index);

  /**
   * @brief Interprets the column at @p idx as a 32-bit integer.
   *
   * @param idx Zero-based column index.
   * @returns The integer value; NULL columns translate to 0 to match SQLite's C API behaviour.
   */
  int get_int(int idx);

  /**
   * @brief Interprets the column at @p idx as a 64-bit integer.
   *
   * @param idx Zero-based column index.
   * @returns The integer value; suitable for INTEGER PRIMARY KEY columns and epoch timestamps.
   */
  std::int64_t get_int64(int idx);

  /**
   * @brief Copies the text at @p idx into an owning std::string.
   *
   * @param idx Zero-based column index.
   * @returns `"NULL"` when the database value is NULL.
   */
  std::string get_string(int idx);

  /**
   * @brief Presents the UTF-8 text at @p idx as a non-owning std::string_view.
   *
   * The view stays valid until @ref next_row or @ref reset is called.
   */
  std::string_view get_string_view(int idx);

  /**
   * @brief Interprets the column at @p idx as a double precision floating value.
   *
   * NULL columns are reported as `0.0`.
   */
  double get_double(int idx);

  /**
   * @brief Reports the number of bytes stored in column @p idx.
   *
   * @param idx Zero-based column index.
   * @returns Length in bytes, or 0 when the column is NULL.
   * Only meaningful for BLOB/TEXT columns; other types return the storage size SQLite reports.
   */
  size_t get_binary_size(int idx);

  /**
   * @brief Copies a blob column into the caller-provided buffer.
   *
   * @param idx Zero-based column index.
   * @param buf Destination memory.
   * @param buf_size Capacity of @p buf in bytes.
   * @throws buffer_too_small_exception when @p buf_size is smaller than the blob.
   */
  void get_binary(int idx, void *buf, size_t buf_size);

  /**
   * @brief Retrieves a blob column into a std::vector<unsigned char>.
   *
   * @param idx Zero-based column index.
   * @param vec Destination buffer that will be resized to match the blob length.
   */
  void get_binary(int idx, std::vector<unsigned char> &vec);

  /**
   * @brief Returns a span that references the blob contents without copying.
   *
   * The span becomes invalid as soon as the cursor advances or resets.
   */
  std::span<const unsigned char> get_binary_span(int idx);

  /**
   * @brief Returns the UTF-8 column name declared in the statement.
   *
   * @param idx Zero-based column index.
   */
  std::string get_column_name(int idx);

  /**
   * @brief Tests whether the value at column @p idx is SQL NULL.
   *
   * @param idx Zero-based column index.
   */
  bool is_null(int idx);

  /**
   * @brief Extracts the column at @p idx into an arbitrary C++ type.
   *
   * Supported conversions include:
   * - arithmetic types (integral, floating point, enums, bool)
   * - std::string and std::string_view
   * - std::chrono::duration / time_point stored as microseconds
   * - std::optional<T> for nullable fields
   * - byte arrays (`std::vector<unsigned char>`, `std::span<const unsigned char>`, `std::span<const std::byte>`)
   *
   * @param idx Zero-based column index.
   * @tparam T Desired destination type.
   * @throws database_exception or std::out_of_range when @p idx is invalid.
   * @throws buffer_too_small_exception if the requested type requires more space than available.
   */
  template <typename T> T get(int idx);

  /**
   * @brief Collects a contiguous slice of columns into a std::tuple.
   *
   * @param start_column First column to include; defaults to 0.
   * @tparam Ts Types to extract, matched positionally from @p start_column.
   * @returns Tuple whose arity matches the template parameters.
   * @throws database_exception when the tuple would exceed the column count.
   */
  template <typename... Ts> std::tuple<Ts...> get_tuple(int start_column = 0);

private:
  void access_check(int);

private:
  construct_params m_params;
  int m_columns;
  int m_row_count;
};

/// Shared-pointer alias used by legacy APIs that transfer result ownership.
typedef std::shared_ptr<result> result_type;

namespace detail {
template <typename... Ts, std::size_t... Index>
std::tuple<Ts...> tuple_from_row(result &res, int start,
                                 std::index_sequence<Index...>) {
  return std::tuple<Ts...>(
      res.template get<Ts>(start + static_cast<int>(Index))...);
}
} // namespace detail

template <typename T> T result::get(int idx) {
  using decayed = detail::decay_t<T>;
  if constexpr (detail::is_optional_v<decayed>) {
    if (is_null(idx)) {
      return std::nullopt;
    }
    using value_type = typename detail::optional_value<decayed>::type;
    return get<value_type>(idx);
  } else if constexpr (detail::is_duration_v<decayed>) {
    auto micros = std::chrono::microseconds{get_int64(idx)};
    return std::chrono::duration_cast<decayed>(micros);
  } else if constexpr (detail::is_time_point_v<decayed>) {
    auto micros = std::chrono::microseconds{get_int64(idx)};
    auto duration =
        std::chrono::duration_cast<typename decayed::duration>(micros);
    return decayed(duration);
  } else if constexpr (std::is_enum_v<decayed>) {
    return static_cast<decayed>(get_int64(idx));
  } else if constexpr (std::is_integral_v<decayed> &&
                       !std::is_same_v<decayed, bool>) {
    return static_cast<decayed>(get_int64(idx));
  } else if constexpr (std::is_same_v<decayed, bool>) {
    return get_int(idx) != 0;
  } else if constexpr (std::is_floating_point_v<decayed>) {
    return static_cast<decayed>(get_double(idx));
  } else if constexpr (std::is_same_v<decayed, std::string>) {
    return get_string(idx);
  } else if constexpr (std::is_same_v<decayed, std::string_view>) {
    return get_string_view(idx);
  } else if constexpr (detail::is_byte_vector_v<decayed>) {
    std::vector<unsigned char> buffer;
    get_binary(idx, buffer);
    return buffer;
  } else if constexpr (detail::is_unsigned_char_span_v<decayed>) {
    return get_binary_span(idx);
  } else if constexpr (detail::is_byte_span_v<decayed>) {
    auto blob = get_binary_span(idx);
    auto ptr = reinterpret_cast<std::byte const *>(blob.data());
    return std::span<const std::byte>(ptr, blob.size());
  } else {
    static_assert(detail::always_false_v<decayed>,
                  "Unsupported type for sqlite::result::get<T>");
  }
}

template <typename... Ts>
std::tuple<Ts...> result::get_tuple(int start_column) {
  if (start_column < 0 ||
      start_column + static_cast<int>(sizeof...(Ts)) > m_columns) {
    throw database_exception("Tuple columns exceed result column count.");
  }
  return detail::tuple_from_row<Ts...>(*this, start_column,
                                       std::index_sequence_for<Ts...>{});
}
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_RESULT_HPP_INCLUDED
