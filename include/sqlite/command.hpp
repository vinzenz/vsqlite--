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

#include <boost/cstdint.hpp>
#include <boost/noncopyable.hpp>
#include <sqlite/connection.hpp>
#include <vector>

struct sqlite3_stmt;

namespace sqlite{
    /** \brief \a null_type is an empty type used to represent NULL
      * values
      */
    struct null_type{};

    /** \brief \a nil is used instead of NULL within the operator %
    *   syntax in this wrapper
    */
    extern null_type nil;


    /** \brief \a command is the base class of all sql command classes
      * An object of this class is not copyable
      */
    struct command : boost::noncopyable{
        /** \brief \a command constructor
          * \param con takes a reference to the database connection type
          *        \a connection
          * \param sql is the SQL string. The sql string can contain placeholder
          *        the question mark '?' is used as placeholder the
          *        \a command::bind methods or command::operator% syntax must be
          *        used to replace the placeholders
          */
        command(connection & con, std::string const & sql);

        /** \brief \a command destructor
          */
        virtual ~command();

        /** \brief clear is used if you'd like to reuse a command object
          */
        void clear();

        /** \brief emit executes the sql command
          * If you have used placeholders you must have replaced all
          * placeholders
          */
        bool emit();

        /** \brief works exactly like the method \a command::emit
          *
          */
        bool operator()();

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
        void bind(int idx, boost::int64_t v);

        /** \brief binds the double v to the given 1 based index
          * \param idx 1 based index of the placeholder within the sql statement
          * \param v double value which should replace the placeholder
          */
        void bind(int idx, double v);

        /** \brief binds the text/string v to the given 1 based index
          * \param idx 1 based index of the placeholder within the sql statement
          * \param v text/string value which should replace the placeholder
          */
        void bind(int idx, std::string const & v);

        /** \brief binds the binary/blob buf to the given 1 based index
          * \param idx 1 based index of the placeholder within the sql statement
          * \param buf binary/blob buf which should replace the placeholder
          * \param buf_size size in bytes of the binary buffer
          */
        void bind(int idx, void const * buf, size_t buf_size);

        /** \brief binds the binary/blob v to the given 1 based index
          * \param idx 1 based index of the placeholder within the sql statement
          * \param v binary/blob buffer which should replace the placeholder
          *        v is a std::vector<unsigned char> const &
          */
        void bind(int idx, std::vector<unsigned char> const & v);

        /** \brief replacement for void command::bind(int idx);
          * To use this operator% you have to use the global object
          * \a nil
          * Indexes are given automatically first call uses 1 as index, second 2
          * and so on
          * \param p should be \a nil
          */
        command & operator % (null_type const & p);

        /** \brief replacement for void command::bind(int idx,int);
          * Indexes are given automatically first call uses 1 as index, second 2
          * and so on
          * \param p should be a 32-Bit integer
          */
        command & operator % (int p);

        /** \brief replacement for void command::bind(int idx,boost::int64_t);
          * Indexes are given automatically first call uses 1 as index, second 2
          * and so on
          * \param p should be a 64-Bit integer
          */
        command & operator % (boost::int64_t p);

        /** \brief replacement for void command::bind(int idx,double);
          * Indexes are given automatically first call uses 1 as index, second 2
          * and so on
          * \param p a double variable
          */
        command & operator % (double p);

        /** \brief replacement for void command::bind(int idx,std::string const&);
          * Indexes are given automatically first call uses 1 as index, second 2
          * and so on
          * \param p should be a Zero Terminated C-style string (char * or
          * char const*), or a std::string object,
          */
        command & operator % (std::string const & p);

        /** \brief replacement for void command::bind(int idx,std::vector<unsigned char> const&);
          * Indexes are given automatically first call uses 1 as index, second 2
          * and so on
          * \param p a constant reference to a std::vector<unsigned char> object
          * (For blob/binary data)
          */
        command & operator % (std::vector<unsigned char> const & p);

    protected:
        void access_check();
        bool step();
        struct sqlite3 * get_handle();
    private:
        void prepare();
        void finalize();
    private:
        connection &   m_con;
        std::string    m_sql;
    protected:
        sqlite3_stmt * stmt;
    private:
        int            last_arg_idx;
    };
}

#endif //GUARD_SQLITE_COMMAND_HPP_INCLUDED

