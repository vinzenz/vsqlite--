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
#ifndef GUARD_SQLITE_RESULT_HPP_INCLUDED
#define GUARD_SQLITE_RESULT_HPP_INCLUDED

#include <cstdint>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>
#include <sqlite/database_exception.hpp>
#include <sqlite/ext/variant.hpp>
#include <sqlite/deprecated.hpp>
#include <stdexcept>
#include <sqlite/detail/type_helpers.hpp>

namespace sqlite {
inline namespace v2 {
    struct query;
    struct result_construct_params_private;
    namespace detail {
        bool end(result_construct_params_private const &);
        void reset(result_construct_params_private &);
    }

    /** \brief result can only be created by a query object.
      * An object of this class is not copyable.
      *
      */
    struct result {
    private:
        typedef std::shared_ptr<result_construct_params_private> construct_params;
        friend struct query;
        result(construct_params);
        result(result const &) = delete;
        result & operator=(result const &) = delete;
    public:
        /** \brief destructor
          *
          */
        ~result();

        /** \brief Increases the row index
          * \return returns false if there is no more row, otherwise it returns
          *         true
          */
        bool next_row();

        /** \brief Returns true when the last row has been reached.
         *  \return returns true when the last row has been already reached.
         */
        inline bool end() const {
            if(!m_params) throw std::runtime_error("Invalid memory access");
            return detail::end(*m_params);
        }

        /** \brief Resets the result cursor to reiterate over the results
         */
        void reset() {
            if(!m_params) throw std::runtime_error("Invalid memory access");
            detail::reset(*m_params);
        }

        /** \brief Returns the number of rows in the result
          * \return an integer
          *
          * \note: DEPRECATED: This function does not work as documented. Do
          *        not use it to retrieve the amount of rows in the result. The
          *        value returned actually only indicates the number of rows
          *        changed in the database (via INSERT/UPDATE).
          */
        VSQLITE_DEPRECATED int get_row_count();

        /** \brief Returns the number of columns
          * \return an integer
          */
        int get_column_count();

        /** \brief Returns the type of the column
          * \param idx column index of the current row in the results
          * \return the column type
          */
        type get_column_type(int idx);

        /** \brief Returns the type of the column
          * \param idx column index of the current row in the results
          * \return a string
          */
        std::string get_column_decltype(int idx);

        /** \brief Retrieves a the current typ into variant_t
          * \param idx column index of the current row in the results
          * \return a value of variant_t
          */
        variant_t get_variant(int index);

        /** \brief Returns the data at the given index as 32-Bit Integer
          * \return a 32-Bit Integer
          */
        int get_int(int idx);

        /** \brief Returns the data at the given index as 64-Bit Integer
          * \param idx column index of the current row in the results
          * \return a 64-Bit Integer
          */
        std::int64_t get_int64(int idx);

        /** \brief Returns the data at the given index as String
          * \param idx column index of the current row in the results
          * \return a std::string object
          */
        std::string get_string(int idx);
        std::string_view get_string_view(int idx);

        /** \brief Returns the data at the given index as double
          * \param idx column index of the current row in the results
          * \return a double
          */
        double get_double(int idx);

        /** \brief Returns the size of the data at the given index in bytes
          * \param idx column index of the current row in the results
          * \return a size_t value which represents the number of bytes needed
          * for the binary data at idx
          */
        size_t get_binary_size(int idx);

        /** \brief Used to retrieve a binary value
          * \param idx column index of the current row in the results
          * \param buf pointer to the buffer which should be filled
          * \param buf_size size in bytes of the buffer
          */
        void get_binary(int idx, void * buf, size_t buf_size);

        /** \brief Used to retrieve a binary value
          * \param idx column index of the current row in the results
          * \param vec a std::vector<unsigned char> which will be filled
          *              the method will increase the allocated buffer if needed
          */
        void get_binary(int idx, std::vector<unsigned char> & vec);
        std::span<const unsigned char> get_binary_span(int idx);

        /** \brief Returns the column name at the given index
          * \param idx column index of the current row in the results
          * \return a std::string object containing the name of the column
          */
        std::string get_column_name(int idx);

        bool is_null(int idx);

        template <typename T>
        T get(int idx);

        template <typename... Ts>
        std::tuple<Ts...> get_tuple(int start_column = 0);
    private:
        void access_check(int);
    private:
        construct_params m_params;
        int m_columns;
        int m_row_count;
    };

    typedef std::shared_ptr<result> result_type;

    namespace detail {
        template <typename... Ts, std::size_t... Index>
        std::tuple<Ts...> tuple_from_row(result & res, int start, std::index_sequence<Index...>) {
            return std::tuple<Ts...>(res.template get<Ts>(start + static_cast<int>(Index))...);
        }
    }

    template <typename T>
    T result::get(int idx) {
        using decayed = detail::decay_t<T>;
        if constexpr (detail::is_optional_v<decayed>) {
            if(is_null(idx)) {
                return std::nullopt;
            }
            using value_type = typename detail::optional_value<decayed>::type;
            return get<value_type>(idx);
        }
        else if constexpr (detail::is_duration_v<decayed>) {
            auto micros = std::chrono::microseconds{get_int64(idx)};
            return std::chrono::duration_cast<decayed>(micros);
        }
        else if constexpr (detail::is_time_point_v<decayed>) {
            auto micros = std::chrono::microseconds{get_int64(idx)};
            auto duration = std::chrono::duration_cast<typename decayed::duration>(micros);
            return decayed(duration);
        }
        else if constexpr (std::is_enum_v<decayed>) {
            return static_cast<decayed>(get_int64(idx));
        }
        else if constexpr (std::is_integral_v<decayed> && !std::is_same_v<decayed, bool>) {
            return static_cast<decayed>(get_int64(idx));
        }
        else if constexpr (std::is_same_v<decayed, bool>) {
            return get_int(idx) != 0;
        }
        else if constexpr (std::is_floating_point_v<decayed>) {
            return static_cast<decayed>(get_double(idx));
        }
        else if constexpr (std::is_same_v<decayed, std::string>) {
            return get_string(idx);
        }
        else if constexpr (std::is_same_v<decayed, std::string_view>) {
            return get_string_view(idx);
        }
        else if constexpr (detail::is_byte_vector_v<decayed>) {
            std::vector<unsigned char> buffer;
            get_binary(idx, buffer);
            return buffer;
        }
        else if constexpr (detail::is_unsigned_char_span_v<decayed>) {
            return get_binary_span(idx);
        }
        else if constexpr (detail::is_byte_span_v<decayed>) {
            auto blob = get_binary_span(idx);
            auto ptr = reinterpret_cast<std::byte const *>(blob.data());
            return std::span<const std::byte>(ptr, blob.size());
        }
        else {
            static_assert(detail::always_false_v<decayed>, "Unsupported type for sqlite::result::get<T>");
        }
    }

    template <typename... Ts>
    std::tuple<Ts...> result::get_tuple(int start_column) {
        if(start_column < 0 || start_column + static_cast<int>(sizeof...(Ts)) > m_columns) {
            throw database_exception("Tuple columns exceed result column count.");
        }
        return detail::tuple_from_row<Ts...>(*this, start_column, std::index_sequence_for<Ts...>{});
    }
} // namespace v2
} // namespace sqlite

#endif //GUARD_SQLITE_RESULT_HPP_INCLUDED
