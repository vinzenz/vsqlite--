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

#include <sqlite/database_exception.hpp>
#include <sqlite/command.hpp>
#include <sqlite/private/private_accessor.hpp>
#include <sqlite3.h>

namespace sqlite{

    null_type nil = null_type();

    command::command(connection & con, std::string const & sql)
    : m_con(con),m_sql(sql),stmt(0),last_arg_idx(0){
        private_accessor::acccess_check(con);
        prepare();
    }

    command::~command(){
        try{
            finalize();
        }
        catch(...){
        }
    }

    void command::finalize(){
        access_check();
        int err = sqlite3_finalize(stmt);
        if(err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err);
        stmt = 0;
    }

    void command::clear(){
        sqlite3_reset(stmt);
        last_arg_idx = 0;
        sqlite3_reset(stmt);
    }

    void command::prepare(){
        private_accessor::acccess_check(m_con);
        if(stmt)
            finalize();
        const char * tail = 0;
        int err = sqlite3_prepare(get_handle(),m_sql.c_str(),-1,&stmt,&tail);
        if(err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err);
    }

    bool command::emit(){
        return step();
    }

    bool command::step(){
        access_check();
        int err = sqlite3_step(stmt);
        switch(err){
        case SQLITE_ROW:
            return true;
        case SQLITE_DONE:
            return false;
        case SQLITE_MISUSE:
            throw database_misuse_exception_code(sqlite3_errmsg(get_handle()), err);
        default:
            throw database_exception_code(sqlite3_errmsg(get_handle()), err);
        }
        return false;
    }

    bool command::operator()(){
        return step();
    }

    void command::bind(int idx){
        access_check();
        int err = sqlite3_bind_null(stmt,idx);
        if(err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err);
    }

    void command::bind(int idx, int v){
        access_check();
        int err = sqlite3_bind_int(stmt,idx,v);
        if(err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err);
    }

    void command::bind(int idx, std::int64_t v){
        access_check();
        int err = sqlite3_bind_int64(stmt,idx,v);
        if(err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err);
    }

    void command::bind(int idx, double v){
        access_check();
        int err = sqlite3_bind_double(stmt,idx,v);
        if(err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err);
    }

    void command::bind(int idx, std::string const & v){
        access_check();
        static char const dummy = 0;
        int err = sqlite3_bind_text(stmt,idx,v.empty() ? &dummy : v.c_str(),int(v.size()),SQLITE_TRANSIENT);
        if(err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err);
    }

    void command::bind(int idx, void const * v , size_t vn){
        access_check();
        int err = sqlite3_bind_blob(stmt,idx,v,int(vn),SQLITE_TRANSIENT);
        if(err != SQLITE_OK)
            throw database_exception_code(sqlite3_errmsg(get_handle()), err);
    }

    void command::bind(int idx, std::vector<unsigned char> const & v)
    {
        static const unsigned char dummy = 0;
        bind(idx, v.empty() ? &dummy : &v.at(0),v.size());
    }

    void command::access_check(){
        private_accessor::acccess_check(m_con);
        if(!stmt)
            throw database_exception("command was not prepared or is invalid");
    }

    command & command::operator % (null_type const &){
        bind(++last_arg_idx);
        return *this;
    }

    command & command::operator % (int v){
        bind(++last_arg_idx,v);
        return *this;
    }

    command & command::operator % (std::int64_t v){
        bind(++last_arg_idx,v);
        return *this;
    }

    command & command::operator % (double v){
        bind(++last_arg_idx,v);
        return *this;
    }

    command & command::operator % (std::string const & v){
        bind(++last_arg_idx,v);
        return *this;
    }

    command & command::operator % (std::vector<unsigned char> const & v)
    {
        bind(++last_arg_idx,v);
        return *this;
    }

    struct sqlite3 * command::get_handle(){
        return private_accessor::get_handle(m_con);
    }
}
