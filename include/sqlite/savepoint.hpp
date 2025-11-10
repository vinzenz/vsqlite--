/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2014 mickey mickey.mouse-1985@libero.it
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
#ifndef GUARD_SQLITE_SAVEPOINT_HPP_INCLUDED
#define GUARD_SQLITE_SAVEPOINT_HPP_INCLUDED
#include <string>
#include <string_view>

namespace sqlite {
inline namespace v2 {
    struct connection;
    struct snapshot;

    /** \brief this is a helper class to handle transaction savepoints
      * within SQLite
      */
    struct savepoint{
    public:
        /** \brief constructor
          * \param con a reference to the connection object where the
          * transaction should be started/ended/committed or rolled back
          * \param name alias for the savepoint
          */
        savepoint(connection & con, std::string const & name);

        /** \brief destructor
          *
          */
        ~savepoint();

        /** \brief Releases a previously created savepoint
          *
          */
        void release();
        
        /** \brief Roll the database status back to the position of the current
          * saveopint.
          */
        void rollback();

        /** \brief Allow to check if savepoint handled by this object is
          * currently active.
          * \note This dues not make any checks inside SQlite's internal data
          * so if a previously-made savepoint is alredy released or rollbacked
          * (committing or rollbacking also this one) this method will continue
          * to say that the savepoint is active.
          * \return \c true if savepoint is still active, \c false otherwise
          */
        bool isActive() const { return m_isActive; }

        /** \brief Returns a string containing the current savepoint name
         * \return The alias of savepoint handled by this object
         */
        std::string getName() const { return m_name; }

        snapshot take_snapshot(std::string_view schema = "main");
        void open_snapshot(snapshot const & snap, std::string_view schema = "main");
    private:
        void exec(std::string const &);

        connection & m_con;      ///< SQlite connection handler
        std::string  m_name;     ///< The alias for the savepoint
        bool         m_isActive; ///< if \c true the savepoint with alias \c m_name is still active (not currently released)
    };
} // namespace v2
} // namespace sqlite

#endif //GUARD_SQLITE_SAVEPOINT_HPP_INCLUDED
