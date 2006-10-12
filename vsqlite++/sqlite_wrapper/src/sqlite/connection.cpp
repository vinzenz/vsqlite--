/*##############################################################################   
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006 Vinzenz Feenstra vinzenz.feenstra@virtuosic-bytes.com
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
#include <boost/format.hpp>
#include <sqlite/database_exception.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/connection.hpp>
#include <sqlite3.h>

namespace sqlite{
    connection::connection(std::string const & db)
        : handle(0){
            open(db);
    }

    connection::~connection(){
        try{
            close();
        }
        catch(...){
        }
    }
    
    void connection::open(const std::string &db){
        if(sqlite3_open(db.c_str(),&handle) != SQLITE_OK)
            throw database_exception("Could not open database");
    }

    void connection::close(){
        access_check();
        if(sqlite3_close(handle) != SQLITE_OK)
            throw database_exception(sqlite3_errmsg(handle));
        handle = 0;
    }

    void connection::access_check(){
        if(!handle)
            throw database_exception("Database is not open.");
    }    
    
    void connection::attach(std::string const & db, std::string const & alias){
        boost::format fmt("ATTACH DATABASE %1% AS %2%;");
        fmt % db % alias;
        execute(*this,fmt.str(),true);
    }

    void connection::detach(std::string const & alias){
        boost::format fmt("DETACH DATABASE %1%;");
        fmt % alias;
        execute(*this,fmt.str(),true);
    }

}
