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
#ifndef GUARD_SQLITE_VIEW_HPP_INCLUDED
#define GUARD_SQLITE_VIEW_HPP_INCLUDED

#include <string>

/**
 * @file sqlite/view.hpp
 * @brief Helpers for creating and dropping SQL views on a connection.
 *
 * The `sqlite::view` helper keeps view creation centralized and offers overloads for temporary
 * views as well as database-qualified names.
 */
namespace sqlite {
inline namespace v2 {
    struct connection;
    /** \brief view is used to create views. In SQLite a view can only be
      * queried. INSERT, DELETE and UPDATE will fail on a view
      */
    struct view
    {
    public:
        /** \brief constructor
          * \param con a reference to the connection object which should be used
          */
        view(connection & con);

        /** \brief destructor
          *
          */
        ~view();

        /** \brief creates a view
          * \param temporary if this parameter is true the view will be present
          * until drop will be called or the database was closed
          * \param alias the name of this view which should be used
          * \param sql_query the SQL statement which represents the view
          */
        void create(bool temporary,
                    std::string const & alias,
                    std::string const & sql_query);

        /** \brief creates a view
          * \param temporary if this parameter is true the view will be present
          * until drop will be called or the database was closed
          * \param database name of the database where the view should be
          * created in
          * \param alias the name of this view which should be used
          * \param sql_query the SQL statement which represents the view
        */
        void create(bool temporary,
                    std::string const & database,
                    std::string const & alias,
                    std::string const & sql_query);

        /** \brief drops a view
          * \param alias name of the view which should be dropped
          */
        void drop(std::string const & alias);

        /** \brief drops a view
          * \param database name of the database where the view was created in
          * \param alias name of the view which should be dropped
          */
        void drop(std::string const & database, std::string const & alias);
    private:
        connection & m_con;
    };
} // namespace v2
} // namespace sqlite
#endif //GUARD_SQLITE_VIEW_HPP_INCLUDED
