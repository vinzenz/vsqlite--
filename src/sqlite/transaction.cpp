/*##############################################################################
 VSQLite++ - virtuosic bytes SQLite3 C++ wrapper

 Copyright (c) 2006-2014 Vinzenz Feenstra vinzenz.feenstra@gmail.com
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
#include <sqlite/connection.hpp>
#include <sqlite/execute.hpp>
#include <sqlite/transaction.hpp>
#include <string>

namespace sqlite{
    transaction::transaction(connection & con, transaction_type type)
        : m_con(con){
        begin(type);
    }

    transaction::~transaction(){
        // when the transaction is stuck ending, the commit/rollback failed
        // and we don't want to take further action, we must assume there is
        // nothing we can do.
        if (m_isEnding) {
            return;
        }
        if (m_isActive) {
            try {
                rollback();
            } catch (...) {
                // We can't recover in the destructor, hope for the best and continue.
            }
        }
    }

    void transaction::begin(transaction_type type){
        std::string sql("BEGIN ");
        switch (type) {
          case transaction_type::deferred:  sql += "DEFERRED " ; break;
          case transaction_type::immediate: sql += "IMMEDIATE "; break;
          case transaction_type::exclusive: sql += "EXCLUSIVE "; break;
          case transaction_type::undefined: ; /* noop */
        }
        sql += "TRANSACTION";
        exec(sql);
        m_isActive = true;
    }

    void transaction::end(){
        m_isEnding = true;
        exec("END TRANSACTION");
        m_isEnding = false;
        m_isActive = false;
    }

    void transaction::commit(){
        m_isEnding = true;
        exec("COMMIT TRANSACTION");
        m_isEnding = false;
        m_isActive = false;
    }

    void transaction::rollback(){
        m_isEnding = true;
        exec("ROLLBACK TRANSACTION");
        m_isEnding = false;
        m_isActive = false;
    }

    void transaction::exec(std::string const & cmd){
        execute(m_con,cmd,true);
    }
}
