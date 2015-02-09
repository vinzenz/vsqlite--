/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2015 Vinzenz Feenstra vinzenz.feenstra@gmail.com
                         and contributors
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
#include <boost/filesystem.hpp>

namespace sqlite{
    connection::connection(std::string const & db)
        : handle(0){
            open(db);
            sqlite3_extended_result_codes(handle, 1);
    }

    connection::connection(std::string const & db, sqlite::open_mode open_mode)
    : handle(0) {
        open(db, open_mode);
        sqlite3_extended_result_codes(handle, 1);
    }

    connection::~connection(){
        try{
            close();
        }
        catch(...){
        }
    }

    void connection::open(const std::string &db){
        int err = sqlite3_open(db.c_str(),&handle);
        if(err != SQLITE_OK)
            throw database_exception_code("Could not open database", err);
    }

  void connection::open(const std::string &db, bool readonly){
    int err = sqlite3_open_v2(db.c_str(),&handle,
                  readonly?SQLITE_OPEN_READONLY:SQLITE_OPEN_READWRITE,
                  NULL);
        if(err != SQLITE_OK)
            throw database_exception_code(readonly
                      ?"Could not open read-only database"
                      :"Could not open database", err);
    }

    void connection::open(std::string const & db, sqlite::open_mode open_mode){
        boost::system::error_code ec;
        bool exists = boost::filesystem::exists(db, ec);
        exists = exists && !ec;
        switch(open_mode) {
          case sqlite::open_mode::open_readonly:
             if (!exists) {
                throw database_exception(
                    "Read-only database '" + db + "' does not exist"
                );
            }
            open(db, true);    // read-only
            return;        // we already opened it!
            case sqlite::open_mode::open_existing:
                if(!exists) {
                    throw database_exception(
                        "Database '" + db + "' does not exist"
                    );
                }
                break;
            case sqlite::open_mode::always_create:
                if(exists) {
                    boost::filesystem::remove(db, ec);
                    if(ec) {
                        throw database_system_error(
                            "Failed to remove existing database '" + db + "'",
                            ec.value()
                        );
                    }
                }
                // fall-through
            case sqlite::open_mode::open_or_create:
                // Default behaviour
                break;
            default:
                break;
        };
        open(db);
    }

    void connection::close(){
        access_check();
        int err = sqlite3_close(handle);
        if(err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(handle), err);
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

    boost::int64_t connection::get_last_insert_rowid(){
        if(!handle)
            throw database_exception("Database is not open.");
        return static_cast<boost::int64_t>(
                sqlite3_last_insert_rowid(handle));
    }
}
