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
#include <sqlite/private/result_construct_params_private.hpp>
#include <sqlite/query.hpp>
#include <boost/bind.hpp>
#include <sqlite3.h>

namespace sqlite{
    query::query(connection & con, std::string const & sql)
    : command(con,sql) {

    }

    query::~query(){
    }

    boost::shared_ptr<result> query::emit_result(){
        bool ended = !step();
        result_construct_params_private * p = new result_construct_params_private();
        p->access_check = boost::bind(&query::access_check,this);
        p->step         = boost::bind(&query::step,this);
        p->db           = sqlite3_db_handle(stmt);
        p->row_count    = sqlite3_changes(p->db);
        p->statement    = stmt;
        p->ended        = ended;
        return boost::shared_ptr<result>(new result(result::construct_params(p)));
    }

    boost::shared_ptr<result> query::get_result(){
        access_check();
        result_construct_params_private * p = new result_construct_params_private();
        p->access_check = boost::bind(&query::access_check,this);
        p->step         = boost::bind(&query::step,this);
        p->db           = sqlite3_db_handle(stmt);
        p->row_count    = sqlite3_changes(p->db);
        p->statement    = stmt;
        p->ended        = false;
        return boost::shared_ptr<result>(new result(result::construct_params(p)));
    }
    void query::access_check(){
        command::access_check();
    }
    bool query::step(){
        return command::step();
    }
}

