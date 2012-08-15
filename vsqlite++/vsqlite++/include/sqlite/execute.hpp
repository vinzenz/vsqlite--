/*##############################################################################   
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2012 Vinzenz Feenstra vinzenz.feenstra@gmail.com
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
#ifndef GUARD_SQLITE_EXECUTE_HPP_INCLUDED
#define GUARD_SQLITE_EXECUTE_HPP_INCLUDED

#include <sqlite/command.hpp>

namespace sqlite{

    /** \brief execute can be used for SQL commands which should executed 
      * the constructor is defined in a way that it can be used like a function
      * An object of this class is not copyable
      */
    struct execute : command{
        /** \brief constructor
          * \param con reference to the connection object which should be used
          * \param sql the SQL statement which should be executed
          * \param immediately if it is true the sql command will be immediately 
          *        executed if it is false the command will be executed after an
          *        emit or operator() call
          */
        execute(connection & con, std::string const & sql, bool immediately = false);

        /** \brief destructor
          *
          */
        virtual ~execute();
    };
}

#endif //GUARD_SQLITE_EXECUTE_HPP_INCLUDED

