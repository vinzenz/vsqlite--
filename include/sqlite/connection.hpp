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
#ifndef GUARD_SQLITE_CONNECTION_HPP_INCLUDED
#define GUARD_SQLITE_CONNECTION_HPP_INCLUDED
#include <string>
#include <boost/noncopyable.hpp>
struct sqlite3;

namespace sqlite{
    enum class open_mode {
        open_existing,  ///!> Opens an existing database or fails
        open_or_create, ///!> Opens an existing database or creates it
        always_create,  ///!> Deletes database if exists and recreates it
    };

    /** \brief connection is used to open, close, attach and detach a database.
      * Further it has to be passed to all classes since it represents the
      * connection to the database and contains the internal needed handle, so
      * you can see a connection object as handle to the database
      * An object of this class is not copyable
      */
    struct connection : boost::noncopyable{
        /** \brief constructor opens the database
          * \param db filename of the database file
          *           if the given file already exists the file will be opened
          *           as database.
          *           If the file does not exist a new database will be created
          */
        connection(std::string const & db);

        /** \brief constructor opens the database
          * \param db filename of the database file
          * \param open_mode How to behave when opening the database
          *
          * \remarks
          * If open_mode::always_create is specified and the file
          * exists but cannot be removed, this will throw a
          * database_system_error exception with the system error code causing
          * the failure.
          *
          * If the database does not exist and open_mode::open_existing was
          * specified, a database_exception will be thrown.
          */
        connection(std::string const & db, sqlite::open_mode open_mode);

        /** \brief destructor closes the database automatically
          *
          */
        ~connection();

        /** \brief attaches another database file to the database represented by
          * the object of this class. It is possible to attach up to 10 times
          * the same database file with different aliases
          * \param db database filename of the database should be attached
          * \param database_alias alias which should be used
          */
        void attach(std::string const & db, std::string const & database_alias);

        /** \brief detaches a database via alias, if the same database was
          * attached with several names they will be still present
          * \param database_alias of the database (must be the same alias which
          * was passed in the attach() call)
          */
        void detach(std::string const & database_alias);
    private:
        friend struct private_accessor;
    private:
        void open(std::string const & db);
        void open(std::string const & db, sqlite::open_mode open_mode);
        void close();
        void access_check();
    private:
        sqlite3 * handle;
    };
}
#endif //GUARD_SQLITE_CONNECTION_HPP_INCLUDED

