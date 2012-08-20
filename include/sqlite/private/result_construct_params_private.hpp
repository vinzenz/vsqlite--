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
#ifndef GUARD_SQLITE_RESULT_CONSTRUCT_PARAMS_PRIVATE_HPP_INCLUDED
#define GUARD_SQLITE_RESULT_CONSTRUCT_PARAMS_PRIVATE_HPP_INCLUDED

#include <boost/function/function0.hpp>

struct sqlite3;
struct sqlite3_stmt;

namespace sqlite{
    struct query;
    struct result_construct_params_private{
        sqlite3 * db;
        sqlite3_stmt * statement;
        int row_count;
        boost::function0<void> access_check;
        boost::function0<bool> step;
    };
}


#endif //GUARD_SQLITE_RESULT_CONSTRUCT_PARAMS_PRIVATE_HPP_INCLUDED
