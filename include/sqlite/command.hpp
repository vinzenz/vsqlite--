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
#ifndef GUARD_SQLITE_COMMAND_HPP_INCLUDED
#define GUARD_SQLITE_COMMAND_HPP_INCLUDED

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <sqlite/connection.hpp>
#include <sqlite/detail/type_helpers.hpp>

struct sqlite3_stmt;

/**
 * @file sqlite/command.hpp
 * @brief Parameter binding helpers and the `sqlite::command` base class for executing statements.
 *
 * Commands own a prepared statement, expose strongly typed `bind` overloads, and provide the
 * streaming-style `%` syntax that higher-level convenience APIs (e.g. `query`) are built upon.
 */
namespace sqlite {
inline namespace v2 {
    template <typename T> struct named_parameter {
        std::string name;
        T value;
    };

    template <typename T> named_parameter<std::decay_t<T>> named(std::string_view name, T &&value) {
        return {std::string(name), std::forward<T>(value)};
    }

    /** \brief \a null_type is an empty type used to represent NULL
     * values
     */
    struct null_type {};

    /** \brief \a nil is used instead of NULL within the operator %
     *   syntax in this wrapper
     */
    extern null_type nil;

    /** \brief \a command is the base class of all sql command classes
     * An object of this class is not copyable
     */
    struct command {
        /** \brief \a command constructor
         * \param con takes a reference to the database connection type
         *        \a connection
         * \param sql is the SQL string. The sql string can contain placeholder
         *        the question mark '?' is used as placeholder the
         *        \a command::bind methods or command::operator% syntax must be
         *        used to replace the placeholders
         */
        command(connection &con, std::string const &sql);
        command(command const &)            = delete;
        command &operator=(command const &) = delete;

        /** \brief \a command destructor
         */
        virtual ~command();

        /** \brief clear is used if you'd like to reuse a command object
         */
        void clear();
        void reset_statement();

        /** \brief step_once executes the sql command a single time.
         * If you have used placeholders you must have replaced all
         * placeholders
         */
        bool step_once();

        /** \brief works exactly like @ref command::step_once
         *
         */
        bool operator()();

        /** \brief Binds all supplied arguments (if any) and executes the statement.
         *
         * Existing bindings (set via <code>operator%</code> or <code>bind</code>) are preserved so
         * you can pre-bind a subset before forwarding the remaining arguments. Once the statement
         * executes, the positional index resets so the next invocation starts from the first
         * placeholder again.
         */
        template <typename... Args> bool operator()(Args &&...args) {
            if constexpr (sizeof...(Args) == 0) {
                return operator()();
            }
            reset_statement();
            ((void)(*this % std::forward<Args>(args)), ...);
            auto result = step();
            last_arg_idx = 0;
            return result;
        }

        /** \brief binds NULL to the given 1 based index
         *
         */
        void bind(int idx);

        /** \brief binds the 32-Bit integer v to the given 1 based index
         * \param idx 1 based index of the placeholder within the sql statement
         * \param v 32-Bit integer value which should replace the placeholder
         */
        void bind(int idx, int v);

        /** \brief binds the 64-Bit integer v to the given 1 based index
         * \param idx 1 based index of the placeholder within the sql statement
         * \param v 64-Bit integer value which should replace the placeholder
         */
        void bind(int idx, std::int64_t v);

        /** \brief binds the double v to the given 1 based index
         * \param idx 1 based index of the placeholder within the sql statement
         * \param v double value which should replace the placeholder
         */
        void bind(int idx, double v);

        /** \brief binds the text/string v to the given 1 based index
         * \param idx 1 based index of the placeholder within the sql statement
         * \param v text/string value which should replace the placeholder
         */
        template <typename Text>
            requires std::convertible_to<Text, std::string_view>
        void bind(int idx, Text &&text) {
            bind_text_impl(idx, std::string_view(std::forward<Text>(text)));
        }

        /** \brief binds the binary/blob buf to the given 1 based index
         * \param idx 1 based index of the placeholder within the sql statement
         * \param buf binary/blob buf which should replace the placeholder
         * \param buf_size size in bytes of the binary buffer
         */
        void bind(int idx, void const *buf, size_t buf_size);

        /** \brief binds the binary/blob v to the given 1 based index
         * \param idx 1 based index of the placeholder within the sql statement
         * \param v binary/blob buffer which should replace the placeholder
         *        v is a std::vector<unsigned char> const &
         */
        void bind(int idx, std::vector<unsigned char> const &v);
        void bind(int idx, std::span<const unsigned char> v);
        void bind(int idx, std::span<const std::byte> v);

        template <typename Value> void bind_value(int idx, Value &&value);

        int parameter_index(std::string_view name) const;

        template <typename Value> void bind(std::string_view name, Value &&value) {
            bind(parameter_index(name), std::forward<Value>(value));
        }

        template <typename Value>
            requires detail::needs_generic_binding_v<Value>
        void bind(int idx, Value &&value) {
            bind_value(idx, std::forward<Value>(value));
        }

        /** \brief replacement for void command::bind(int idx);
         * To use this operator% you have to use the global object
         * \a nil
         * Indexes are given automatically first call uses 1 as index, second 2
         * and so on
         * \param p should be \a nil
         */
        command &operator%(null_type const &p);

        /** \brief replacement for void command::bind(int idx,int);
         * Indexes are given automatically first call uses 1 as index, second 2
         * and so on
         * \param p should be a 32-Bit integer
         */
        command &operator%(int p);

        /** \brief replacement for void command::bind(int idx,std::int64_t);
         * Indexes are given automatically first call uses 1 as index, second 2
         * and so on
         * \param p should be a 64-Bit integer
         */
        command &operator%(std::int64_t p);

        /** \brief replacement for void command::bind(int idx,double);
         * Indexes are given automatically first call uses 1 as index, second 2
         * and so on
         * \param p a double variable
         */
        command &operator%(double p);

        /** \brief replacement for void command::bind(int idx,std::string const&);
         * Indexes are given automatically first call uses 1 as index, second 2
         * and so on
         * \param p should be a Zero Terminated C-style string (char * or
         * char const*), or a std::string object,
         */
        template <typename Text>
            requires std::convertible_to<Text, std::string_view>
        command &operator%(Text &&p) {
            bind(++last_arg_idx, std::string_view(std::forward<Text>(p)));
            return *this;
        }

        /** \brief replacement for void command::bind(int idx,std::vector<unsigned char> const&);
         * Indexes are given automatically first call uses 1 as index, second 2
         * and so on
         * \param p a constant reference to a std::vector<unsigned char> object
         * (For blob/binary data)
         */
        command &operator%(std::vector<unsigned char> const &p);
        command &operator%(std::span<const unsigned char> p);
        command &operator%(std::span<const std::byte> p);

        template <typename T> command &operator%(named_parameter<T> param) {
            bind(param.name, std::move(param.value));
            return *this;
        }

        template <typename Value>
            requires detail::needs_generic_binding_v<detail::decay_t<Value>>
        command &operator%(Value &&value) {
            bind_value(++last_arg_idx, std::forward<Value>(value));
            return *this;
        }

    protected:
        void access_check() const;
        bool step();
        struct sqlite3 *get_handle();

    private:
        void prepare();
        void finalize();
        void bind_text_impl(int idx, std::string_view text);

    private:
        connection &m_con;
        std::string m_sql;

    protected:
        sqlite3_stmt *stmt;

    private:
        int last_arg_idx;
    };

    template <typename Value> void command::bind_value(int idx, Value &&value) {
        using decayed = detail::decay_t<Value>;
        if constexpr (detail::is_optional_v<decayed>) {
            if (!value) {
                bind(idx);
            } else {
                bind_value(idx, *value);
            }
        } else if constexpr (detail::is_duration_v<decayed>) {
            auto micros = std::chrono::duration_cast<std::chrono::microseconds>(value).count();
            bind(idx, static_cast<std::int64_t>(micros));
        } else if constexpr (detail::is_time_point_v<decayed>) {
            auto micros =
                std::chrono::duration_cast<std::chrono::microseconds>(value.time_since_epoch())
                    .count();
            bind(idx, static_cast<std::int64_t>(micros));
        } else if constexpr (std::is_enum_v<decayed>) {
            bind(idx, static_cast<std::int64_t>(value));
        } else if constexpr (std::is_integral_v<decayed>) {
            bind(idx, static_cast<std::int64_t>(value));
        } else if constexpr (std::is_floating_point_v<decayed>) {
            bind(idx, static_cast<double>(value));
        } else if constexpr (detail::is_string_like_v<decayed>) {
            bind(idx, std::string_view(std::forward<Value>(value)));
        } else if constexpr (detail::is_byte_vector_v<decayed>) {
            bind(idx, value);
        } else if constexpr (detail::is_unsigned_char_span_v<decayed>) {
            bind(idx, value);
        } else if constexpr (detail::is_byte_span_v<decayed>) {
            bind(idx, value);
        } else {
            static_assert(detail::always_false_v<decayed>,
                          "Unsupported type for sqlite::command::bind_value");
        }
    }
} // namespace v2
} // namespace sqlite

#endif // GUARD_SQLITE_COMMAND_HPP_INCLUDED
