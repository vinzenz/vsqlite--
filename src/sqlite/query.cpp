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
#include <memory>
#include <sqlite/private/result_construct_params_private.hpp>
#include <sqlite/query.hpp>
#include <sqlite3.h>

namespace sqlite {
inline namespace v2 {
    query::query(connection & con, std::string const & sql)
    : command(con,sql) {

    }

    query::~query(){
    }

    std::shared_ptr<result> query::emit_result(){
        bool ended = !step();
        auto params = std::make_shared<result_construct_params_private>();
        params->access_check = [this]() { access_check(); };
        params->step         = [this]() -> bool { return step(); };
        params->db           = sqlite3_db_handle(stmt);
        params->row_count    = sqlite3_changes(params->db);
        params->statement    = stmt;
        params->ended        = ended;
        return std::shared_ptr<result>(new result(params));
    }

    std::shared_ptr<result> query::get_result(){
        access_check();
        auto params = std::make_shared<result_construct_params_private>();
        params->access_check = [this]() { access_check(); };
        params->step         = [this]() -> bool { return step(); };
        params->db           = sqlite3_db_handle(stmt);
        params->row_count    = sqlite3_changes(params->db);
        params->statement    = stmt;
        params->ended        = false;
        return std::shared_ptr<result>(new result(params));
    }
    void query::access_check(){
        command::access_check();
    }
    bool query::step(){
        return command::step();
    }
} // namespace v2
} // namespace sqlite
