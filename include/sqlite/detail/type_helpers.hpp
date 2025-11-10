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
#ifndef GUARD_SQLITE_DETAIL_TYPE_HELPERS_HPP_INCLUDED
#define GUARD_SQLITE_DETAIL_TYPE_HELPERS_HPP_INCLUDED

#include <chrono>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace sqlite {
inline namespace v2 {
    namespace detail {

        template <typename T> inline constexpr bool always_false_v = false;

        template <typename T> struct always_false : std::false_type {};

        template <typename T> using decay_t = std::remove_cvref_t<T>;

        template <typename T> inline constexpr bool is_optional_v = false;

        template <typename T> inline constexpr bool is_optional_v<std::optional<T>> = true;

        template <typename T> struct optional_value {
            using type = T;
        };

        template <typename T> struct optional_value<std::optional<T>> {
            using type = T;
        };

        template <typename T> inline constexpr bool is_duration_v = false;

        template <typename Rep, typename Period>
        inline constexpr bool is_duration_v<std::chrono::duration<Rep, Period>> = true;

        template <typename T> inline constexpr bool is_time_point_v = false;

        template <typename Clock, typename Duration>
        inline constexpr bool is_time_point_v<std::chrono::time_point<Clock, Duration>> = true;

        template <typename T>
        inline constexpr bool is_string_like_v = std::is_convertible_v<T, std::string_view>;

        template <typename T>
        inline constexpr bool is_byte_vector_v =
            std::is_same_v<decay_t<T>, std::vector<unsigned char>>;

        template <typename T>
        inline constexpr bool is_unsigned_char_span_v =
            std::is_same_v<decay_t<T>, std::span<const unsigned char>>;

        template <typename T>
        inline constexpr bool is_byte_span_v =
            std::is_same_v<decay_t<T>, std::span<const std::byte>>;

        template <typename T>
        inline constexpr bool needs_generic_binding_v =
            is_optional_v<decay_t<T>> || is_duration_v<decay_t<T>> || is_time_point_v<decay_t<T>> ||
            std::is_enum_v<decay_t<T>>;

        template <typename... Ts> struct pack_size;

        template <typename... Ts>
        struct pack_size<std::tuple<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)> {};

    } // namespace detail
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_DETAIL_TYPE_HELPERS_HPP_INCLUDED
