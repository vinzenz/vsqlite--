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
#include <memory>
#include <sqlite/ext/variant.hpp>
#include <sqlite/deprecated.hpp>
#include <stdexcept>

namespace sqlite{
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

        /** \brief Returns the column name at the given index
          * \param idx column index of the current row in the results
          * \return a std::string object containing the name of the column
          */
        std::string get_column_name(int idx);
    private:
        void access_check(int);
    private:
        construct_params m_params;
        int m_columns;
        int m_row_count;
    };

    typedef std::shared_ptr<result> result_type;
}

#endif //GUARD_SQLITE_RESULT_HPP_INCLUDED
