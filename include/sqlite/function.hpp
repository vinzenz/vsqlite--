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
#ifndef GUARD_SQLITE_FUNCTION_HPP_INCLUDED
#define GUARD_SQLITE_FUNCTION_HPP_INCLUDED

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <sqlite/database_exception.hpp>
#include <sqlite/private/private_accessor.hpp>

#include <sqlite3.h>

namespace sqlite {
inline namespace v2 {

    struct function_options {
        int arity = -1;
        int text_representation = SQLITE_UTF8;
        bool deterministic = false;
        bool direct_only = false;
        bool innocuous = false;
    };

    struct null_type;

    namespace detail {
        template <typename Callable>
        struct function_holder {
            std::decay_t<Callable> callable;
        };

        template <typename Callable, typename Enable = void>
        struct callable_traits;

        template <typename R, typename... Args>
        struct callable_traits<R(Args...), void> {
            using return_type = R;
            using arguments_tuple = std::tuple<Args...>;
            static constexpr std::size_t arity = sizeof...(Args);

            template <std::size_t Index>
            using argument = std::tuple_element_t<Index, arguments_tuple>;
        };

        template <typename R, typename... Args>
        struct callable_traits<R(*)(Args...), void> : callable_traits<R(Args...)> {};

        template <typename R, typename... Args>
        struct callable_traits<R(&)(Args...), void> : callable_traits<R(Args...)> {};

        template <typename Class, typename R, typename... Args>
        struct callable_traits<R(Class::*)(Args...), void> : callable_traits<R(Args...)> {};

        template <typename Class, typename R, typename... Args>
        struct callable_traits<R(Class::*)(Args...) const, void> : callable_traits<R(Args...)> {};

#if defined(__cpp_noexcept_function_type) && __cpp_noexcept_function_type >= 201510
        template <typename Class, typename R, typename... Args>
        struct callable_traits<R(Class::*)(Args...) noexcept, void> : callable_traits<R(Args...)> {};

        template <typename Class, typename R, typename... Args>
        struct callable_traits<R(Class::*)(Args...) const noexcept, void> : callable_traits<R(Args...)> {};

        template <typename R, typename... Args>
        struct callable_traits<R(*)(Args...) noexcept, void> : callable_traits<R(Args...)> {};

        template <typename R, typename... Args>
        struct callable_traits<R(&)(Args...) noexcept, void> : callable_traits<R(Args...)> {};
#endif

        template <typename Callable>
        struct callable_traits<Callable, std::void_t<decltype(&Callable::operator())>>
            : callable_traits<decltype(&Callable::operator())> {};

        template <typename Callable>
        struct callable_traits<Callable const, std::enable_if_t<!std::is_function_v<Callable>, void>>
            : callable_traits<Callable> {};

        template <typename Callable>
        struct callable_traits<Callable volatile, std::enable_if_t<!std::is_function_v<Callable>, void>>
            : callable_traits<Callable> {};

        template <typename Callable>
        struct callable_traits<Callable const volatile, std::enable_if_t<!std::is_function_v<Callable>, void>>
            : callable_traits<Callable> {};

        template <typename Callable>
        struct callable_traits<Callable &, std::enable_if_t<!std::is_function_v<std::remove_reference_t<Callable>>, void>>
            : callable_traits<std::remove_reference_t<Callable>> {};

        template <typename Callable>
        struct callable_traits<Callable &&, std::enable_if_t<!std::is_function_v<std::remove_reference_t<Callable>>, void>>
            : callable_traits<std::remove_reference_t<Callable>> {};

        template <typename T>
        using decay_t = std::remove_cvref_t<T>;

        template <typename T>
        struct is_optional : std::false_type {};

        template <typename U>
        struct is_optional<std::optional<U>> : std::true_type {};

        inline void throw_null_argument_error() {
            throw database_exception("NULL passed to SQL function argument but callable parameter is not nullable.");
        }

        template <typename T, typename Enable = void>
        struct argument_converter;

        template <typename T>
            requires (std::is_integral_v<decay_t<T>> && !std::is_same_v<decay_t<T>, bool>)
        struct argument_converter<T> {
            static decay_t<T> convert(sqlite3_value * value) {
                if(sqlite3_value_type(value) == SQLITE_NULL) {
                    throw_null_argument_error();
                }
                return static_cast<decay_t<T>>(sqlite3_value_int64(value));
            }
        };

        template <>
        struct argument_converter<bool> {
            static bool convert(sqlite3_value * value) {
                if(sqlite3_value_type(value) == SQLITE_NULL) {
                    throw_null_argument_error();
                }
                return sqlite3_value_int(value) != 0;
            }
        };

        template <typename T>
            requires std::is_floating_point_v<decay_t<T>>
        struct argument_converter<T> {
            static decay_t<T> convert(sqlite3_value * value) {
                if(sqlite3_value_type(value) == SQLITE_NULL) {
                    throw_null_argument_error();
                }
                return static_cast<decay_t<T>>(sqlite3_value_double(value));
            }
        };

        template <>
        struct argument_converter<std::string_view> {
            static std::string_view convert(sqlite3_value * value) {
                if(sqlite3_value_type(value) == SQLITE_NULL) {
                    throw_null_argument_error();
                }
                auto const len = sqlite3_value_bytes(value);
                auto const * text = reinterpret_cast<char const *>(sqlite3_value_text(value));
                if(!text) {
                    return std::string_view();
                }
                return std::string_view(text, static_cast<std::size_t>(len));
            }
        };

        template <typename Text>
            requires (!std::is_same_v<decay_t<Text>, std::string_view>
                && std::constructible_from<decay_t<Text>, std::string_view>)
        struct argument_converter<Text> {
            static decay_t<Text> convert(sqlite3_value * value) {
                return decay_t<Text>(argument_converter<std::string_view>::convert(value));
            }
        };

        template <>
        struct argument_converter<std::span<const unsigned char>> {
            static std::span<const unsigned char> convert(sqlite3_value * value) {
                if(sqlite3_value_type(value) == SQLITE_NULL) {
                    throw_null_argument_error();
                }
                auto const len = sqlite3_value_bytes(value);
                auto const * data = static_cast<unsigned char const *>(sqlite3_value_blob(value));
                return std::span<const unsigned char>(data, static_cast<std::size_t>(len));
            }
        };

        template <>
        struct argument_converter<std::span<const std::byte>> {
            static std::span<const std::byte> convert(sqlite3_value * value) {
                auto bytes = argument_converter<std::span<const unsigned char>>::convert(value);
                auto ptr = reinterpret_cast<std::byte const *>(bytes.data());
                return std::span<const std::byte>(ptr, bytes.size());
            }
        };

        template <typename Vector>
            requires (std::is_same_v<decay_t<Vector>, std::vector<unsigned char>>
                || std::is_same_v<decay_t<Vector>, std::vector<std::byte>>)
        struct argument_converter<Vector> {
            static decay_t<Vector> convert(sqlite3_value * value) {
                auto span = argument_converter<
                    std::conditional_t<std::is_same_v<decay_t<Vector>, std::vector<unsigned char>>,
                        std::span<const unsigned char>,
                        std::span<const std::byte>>>::convert(value);
                return decay_t<Vector>(span.begin(), span.end());
            }
        };

        template <>
        struct argument_converter<sqlite3_value *> {
            static sqlite3_value * convert(sqlite3_value * value) {
                return value;
            }
        };

        template <>
        struct argument_converter<sqlite3_value const *> {
            static sqlite3_value const * convert(sqlite3_value * value) {
                return value;
            }
        };

        template <typename T>
        struct argument_converter<std::optional<T>> {
            static_assert(!is_optional<decay_t<T>>::value, "Nested std::optional values are not supported.");
            static std::optional<T> convert(sqlite3_value * value) {
                if(sqlite3_value_type(value) == SQLITE_NULL) {
                    return std::nullopt;
                }
                return argument_converter<T>::convert(value);
            }
        };

        template <typename T, typename Enable = void>
        struct result_writer;

        template <>
        struct result_writer<void> {
            static void apply(sqlite3_context * ctx, void const *) {
                sqlite3_result_null(ctx);
            }
        };

        template <>
        struct result_writer<null_type> {
            static void apply(sqlite3_context * ctx, null_type const &) {
                sqlite3_result_null(ctx);
            }
        };

        template <>
        struct result_writer<std::nullptr_t> {
            static void apply(sqlite3_context * ctx, std::nullptr_t) {
                sqlite3_result_null(ctx);
            }
        };

        template <typename T>
            requires (std::is_integral_v<decay_t<T>> && !std::is_same_v<decay_t<T>, bool>)
        struct result_writer<T> {
            static void apply(sqlite3_context * ctx, T value) {
                sqlite3_result_int64(ctx, static_cast<sqlite3_int64>(value));
            }
        };

        template <>
        struct result_writer<bool> {
            static void apply(sqlite3_context * ctx, bool value) {
                sqlite3_result_int(ctx, value ? 1 : 0);
            }
        };

        template <typename T>
            requires std::is_floating_point_v<decay_t<T>>
        struct result_writer<T> {
            static void apply(sqlite3_context * ctx, T value) {
                sqlite3_result_double(ctx, static_cast<double>(value));
            }
        };

        inline void result_text(sqlite3_context * ctx, std::string_view view) {
            sqlite3_result_text(ctx, view.data(), static_cast<int>(view.size()), SQLITE_TRANSIENT);
        }

        template <>
        struct result_writer<std::string_view> {
            static void apply(sqlite3_context * ctx, std::string_view value) {
                result_text(ctx, value);
            }
        };

        template <typename Text>
            requires (!std::is_same_v<decay_t<Text>, std::string_view>
                && std::constructible_from<std::string_view, decay_t<Text>>)
        struct result_writer<Text> {
            static void apply(sqlite3_context * ctx, Text const & value) {
                result_text(ctx, std::string_view(value));
            }
        };

        template <>
        struct result_writer<std::span<const unsigned char>> {
            static void apply(sqlite3_context * ctx, std::span<const unsigned char> value) {
                sqlite3_result_blob(ctx, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
            }
        };

        template <>
        struct result_writer<std::span<const std::byte>> {
            static void apply(sqlite3_context * ctx, std::span<const std::byte> value) {
                auto ptr = reinterpret_cast<unsigned char const *>(value.data());
                sqlite3_result_blob(ctx, ptr, static_cast<int>(value.size()), SQLITE_TRANSIENT);
            }
        };

        template <typename Vector>
            requires (std::is_same_v<decay_t<Vector>, std::vector<unsigned char>>
                || std::is_same_v<decay_t<Vector>, std::vector<std::byte>>)
        struct result_writer<Vector> {
            static void apply(sqlite3_context * ctx, Vector const & value) {
                sqlite3_result_blob(ctx,
                    reinterpret_cast<unsigned char const *>(value.data()),
                    static_cast<int>(value.size()),
                    SQLITE_TRANSIENT);
            }
        };

        template <typename T>
        struct result_writer<std::optional<T>> {
            static void apply(sqlite3_context * ctx, std::optional<T> const & value) {
                if(!value) {
                    sqlite3_result_null(ctx);
                    return;
                }
                result_writer<T>::apply(ctx, *value);
            }
        };

        template <typename Callable, std::size_t... Index>
        decltype(auto) invoke_with_arguments(function_holder<Callable> & holder,
                                             sqlite3_value ** argv,
                                             std::index_sequence<Index...>) {
            using traits = callable_traits<Callable>;
            if constexpr(sizeof...(Index) == 0) {
                return std::invoke(holder.callable);
            }
            else {
                return std::invoke(
                    holder.callable,
                    argument_converter<decay_t<typename traits::template argument<Index>>>::convert(argv[Index])...
                );
            }
        }

        template <typename Callable>
        void destroy_holder(void * data) {
            delete static_cast<function_holder<Callable> *>(data);
        }

        template <typename Callable>
        void function_entry(sqlite3_context * ctx, int argc, sqlite3_value ** argv) {
            using traits = callable_traits<Callable>;
            auto * holder = static_cast<function_holder<Callable> *>(sqlite3_user_data(ctx));
            if(!holder) {
                sqlite3_result_error(ctx, "SQL function metadata missing.", -1);
                return;
            }
            if(argc != static_cast<int>(traits::arity)) {
                sqlite3_result_error(ctx, "Unexpected number of arguments for SQL function.", -1);
                return;
            }
            try {
                if constexpr(std::is_void_v<typename traits::return_type>) {
                    invoke_with_arguments<Callable>(
                        *holder,
                        argv,
                        std::make_index_sequence<traits::arity>{}
                    );
                    sqlite3_result_null(ctx);
                }
                else {
                    auto result = invoke_with_arguments<Callable>(
                        *holder,
                        argv,
                        std::make_index_sequence<traits::arity>{}
                    );
                    result_writer<typename traits::return_type>::apply(ctx, result);
                }
            }
            catch(database_exception const & ex) {
                sqlite3_result_error(ctx, ex.what(), -1);
            }
            catch(std::exception const & ex) {
                sqlite3_result_error(ctx, ex.what(), -1);
            }
            catch(...) {
                sqlite3_result_error(ctx, "Unhandled exception in SQL function.", -1);
            }
        }

        inline int compose_text_rep(function_options const & options) {
            int rep = options.text_representation;
#ifdef SQLITE_DETERMINISTIC
            if(options.deterministic) {
                rep |= SQLITE_DETERMINISTIC;
            }
#else
            (void)options.deterministic;
#endif
#ifdef SQLITE_DIRECTONLY
            if(options.direct_only) {
                rep |= SQLITE_DIRECTONLY;
            }
#else
            (void)options.direct_only;
#endif
#ifdef SQLITE_INNOCUOUS
            if(options.innocuous) {
                rep |= SQLITE_INNOCUOUS;
            }
#else
            (void)options.innocuous;
#endif
            return rep;
        }

        inline std::string make_function_error(std::string_view name) {
            if(name.empty()) {
                return "Failed to register SQL function.";
            }
            std::string message("Failed to register SQL function '");
            message.append(name);
            message.push_back('\'');
            message.push_back('.');
            return message;
        }
    } // namespace detail

    template <typename Callable>
    void create_function(connection & con, std::string_view name, Callable && callable,
                         function_options options = {}) {
        using callable_t = std::decay_t<Callable>;
        using traits = detail::callable_traits<callable_t>;
        static_assert(traits::arity <= static_cast<std::size_t>(std::numeric_limits<int>::max()),
            "SQL functions cannot expose more than INT_MAX parameters.");

        constexpr auto max_arity = static_cast<int>(traits::arity);
        int arity = options.arity;
        if(arity < 0) {
            arity = max_arity;
        }
        if(arity != max_arity) {
            throw database_exception("Explicit arity override does not match callable signature.");
        }

        auto holder = std::make_unique<detail::function_holder<callable_t>>(std::forward<Callable>(callable));

        private_accessor::acccess_check(con);
        auto handle = private_accessor::get_handle(con);
        std::string name_buffer(name);
        auto text_rep = detail::compose_text_rep(options);

        int rc = sqlite3_create_function_v2(
            handle,
            name_buffer.c_str(),
            arity,
            text_rep,
            holder.get(),
            &detail::function_entry<callable_t>,
            nullptr,
            nullptr,
            &detail::destroy_holder<callable_t>
        );

        if(rc != SQLITE_OK) {
            auto err = sqlite3_errmsg(handle);
            throw database_exception_code(
                err ? err : detail::make_function_error(name),
                rc
            );
        }

        holder.release();
    }

} // namespace v2
} // namespace sqlite

#endif //GUARD_SQLITE_FUNCTION_HPP_INCLUDED
