/*##############################################################################   
VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

Copyright (c) 2006-2012 Vinzenz Feenstra vinzenz.feenstra@gmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy 
of this software and associated documentation files (the "Software"), to deal 
in the Software without restriction, including without limitation the rights 
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
copies of the Software, and to permit persons to whom the Software is 
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in 
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE                                                                     
##############################################################################*/
#ifndef GUARD_SQLITE_PRIVATE_PRIVATE_ACCESSOR_HPP_INCLUDED
#define GUARD_SQLITE_PRIVATE_PRIVATE_ACCESSOR_HPP_INCLUDED

#include <sqlite/connection.hpp>

namespace sqlite{    
    /** \brief A internal used class, shall not be used from users
      *
      */
    struct private_accessor{
        static struct sqlite3 * get_handle(connection & m_con){
            return m_con.handle;
        }
        static void acccess_check(connection & m_con){
            m_con.access_check();
        }
    };
}

#endif// GUARD_SQLITE_PRIVATE_PRIVATE_ACCESSOR_HPP_INCLUDED
 
